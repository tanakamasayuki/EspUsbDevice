#include "EspUsbDeviceEndpoint.h"
#include "EspUsbAudioTransport.h"
#include "../EspUsbDevice.h"
#include "EspUsbDeviceAppDriver.h"
#include "EspUsbMemory.h"
#include "EspUsbRuntime.h"
#include "EspUsbDeviceBus.h"
#include <algorithm>
#include <string.h>
namespace {
portMUX_TYPE audioLock = portMUX_INITIALIZER_UNLOCKED;
const espusb::internal::UsbClassDriver &audioDriver();
using espusb::internal::AudioGuard;
struct Ring {
  uint8_t *data = nullptr;
  size_t capacity = 0, head = 0, used = 0;
  bool owned = false;
  bool allocate(size_t n, uint8_t *external) {
    capacity = n; data = external; owned = n && !external;
    if (owned) data = static_cast<uint8_t *>(espusb::internal::allocateUsbMemory(n));
    return !n || data;
  }
  size_t push(const uint8_t *p, size_t n) {
    n = std::min(n, capacity - used); if (!n) return 0;
    const size_t tail = (head + used) % capacity, first = std::min(n, capacity - tail);
    memcpy(data + tail, p, first); memcpy(data, p + first, n - first); used += n; return n;
  }
  size_t pop(uint8_t *p, size_t n) {
    n = std::min(n, used); if (!n) return 0;
    const size_t first = std::min(n, capacity - head);
    memcpy(p, data + head, first); memcpy(p + first, data, n - first);
    head = (head + n) % capacity; used -= n; return n;
  }
  void clear() { head = used = 0; }
  ~Ring() { if (owned) espusb::internal::freeUsbMemory(data); }
};
struct AudioPacketStream {
  Ring ring;
  uint8_t *packet = nullptr;
  const uint8_t *descriptor = nullptr;
  uint16_t packetCapacity = 0, packetSize = 0, frameBytes = 0;
  uint8_t endpoint = 0, desired = 0;
  bool active = false, draining = false, discard = false;
  uint32_t phase = 0;
  ~AudioPacketStream() { espusb::internal::freeUsbMemory(packet); }
};
}
namespace espusb { namespace internal {
AudioGuard::AudioGuard() { portENTER_CRITICAL(&audioLock); }
AudioGuard::~AudioGuard() { portEXIT_CRITICAL(&audioLock); }
} }
struct EspUsbAudioState {
  static EspUsbAudioState *first;
  EspUsbAudioState *next = nullptr;
  EspUsbAudioFunction &owner;
  AudioPacketStream playback, capture;
  uint8_t port = 0, controlInterface = 255, feedbackEndpoint = 0;
  uint8_t *feedback = nullptr;
  uint8_t control[14] = {}, alternateResponse = 0;
  bool stopping = false, fenceQueued = false;
  uint32_t epoch = 0, fenceEpoch = 0, intervals = 1000;
  espusb::internal::UsbRuntimeJob changeJob{changePump, this};
  explicit EspUsbAudioState(EspUsbAudioFunction &function) : owner(function) {}
  ~EspUsbAudioState() {
    espusb::internal::removeUsbRuntimeJob(changeJob);
    espusb::internal::freeUsbMemory(feedback);
  }
  bool allocateStream(AudioPacketStream &stream, const espusb::internal::AudioPcmFormat *format, size_t capacity, uint8_t *external) {
    if (!format) return true;
    stream.frameBytes = format->channels * format->subslotBytes;
    espusb::internal::AudioPacketRequirement packet;
    if (capacity % stream.frameBytes || espusb::internal::validateAudioFormat(*format,
        espusb::internal::UsbSpeed::Full, espusb::internal::defaultAudioBusLimits(espusb::internal::UsbSpeed::Full), &packet) != espusb::internal::AudioFormatError::None) return false;
    stream.packetCapacity = packet.maxPacketSize;
    if (!stream.ring.allocate(capacity, external)) return false;
    stream.packet = static_cast<uint8_t *>(espusb::internal::allocateUsbMemory(stream.packetCapacity, 64, true));
    return stream.packet != nullptr;
  }
  bool allocate() {
    if (owner.playbackEnabled_ && !allocateStream(playback, owner.model_.playback().format(0), owner.config_.playbackBufferFrames * (owner.model_.playback().format(0)->channels * owner.model_.playback().format(0)->subslotBytes), owner.config_.playbackBuffer)) return false;
    if (owner.captureEnabled_ && !allocateStream(capture, owner.model_.capture().format(0), owner.config_.captureBufferFrames * (owner.model_.capture().format(0)->channels * owner.model_.capture().format(0)->subslotBytes), owner.config_.captureBuffer)) return false;
    if (owner.graph_.playback.feedbackEndpoint) {
      feedback = static_cast<uint8_t *>(espusb::internal::allocateUsbMemory(4, 64, true));
      if (!feedback) return false;
    }
    return true;
  }
  void armPlayback() {
    if (!stopping && playback.active) espusb_device_edpt_xfer(port, playback.endpoint, playback.packet, playback.packetSize, false);
  }
  void armCapture() {
    if (stopping || !capture.active) return;
    capture.phase += owner.currentSampleRate();
    const size_t bytes = size_t(capture.phase / intervals) * capture.frameBytes;
    capture.phase %= intervals;
    if (bytes > capture.packetSize) return;
    const size_t copied = capture.ring.pop(capture.packet, bytes);
    if (copied < bytes) {
      memset(capture.packet + copied, 0, bytes - copied);
      owner.capture_.underrunCount_.fetch_add(1, std::memory_order_relaxed);
      owner.capture_.underrunBytes_.fetch_add(bytes - copied, std::memory_order_relaxed);
    }
    espusb_device_edpt_xfer(port, capture.endpoint, capture.packet, uint16_t(bytes), false);
  }
  void armFeedback() {
    if (stopping || !playback.active || !feedbackEndpoint) return;
    // The first transport uses a USB-derived nominal sample clock. External
    // codec-clock feedback needs a separate consumption/clock API.
    const uint32_t value = uint32_t((uint64_t(owner.currentSampleRate()) << 16) / intervals);
    for (unsigned i = 0; i < 4; ++i) feedback[i] = uint8_t(value >> (8 * i));
    espusb_device_edpt_xfer(port, feedbackEndpoint, feedback, 4, false);
  }
  bool change(AudioPacketStream &stream, uint8_t alternate, bool isPlayback) {
    stream.discard |= stream.active || !alternate;
    stream.active = false; stream.desired = alternate; stream.draining = true;
    if (!espusb_device_edpt_abort(port, stream.endpoint) || (isPlayback && feedbackEndpoint && !espusb_device_edpt_abort(port, feedbackEndpoint))) return false;
    ++epoch;
    return espusb::internal::postUsbRuntimeJob(changeJob);
  }
  static void finishChange(void *context) {
    auto *s = static_cast<EspUsbAudioState *>(context); AudioGuard guard;
    s->fenceQueued = false;
    if (s->stopping) return;
    if (s->fenceEpoch != s->epoch) { espusb::internal::postUsbRuntimeJob(s->changeJob); return; }
    for (AudioPacketStream *stream : {&s->playback, &s->capture}) {
      if (!stream->draining) continue;
      if (stream->discard) stream->ring.clear();
      stream->discard = stream->draining = false; stream->phase = 0;
      stream->active = stream->desired == 1;
      if (stream == &s->playback) { s->armPlayback(); s->armFeedback(); }
      else s->armCapture();
    }
  }
  static void changePump(void *context) {
    auto *s = static_cast<EspUsbAudioState *>(context); AudioGuard guard;
    if (s->stopping || s->fenceQueued || (!s->playback.draining && !s->capture.draining)) return;
    s->fenceEpoch = s->epoch;
    s->fenceQueued = espusb_device_try_defer(finishChange, s);
    if (!s->fenceQueued) espusb::internal::postUsbRuntimeJob(s->changeJob);
  }
  static void init() {}
  static void resetAll(uint8_t) {
    AudioGuard guard;
    for (auto *s = first; s; s = s->next) {
      ++s->epoch; s->controlInterface = 255;
      for (AudioPacketStream *stream : {&s->playback, &s->capture}) {
        stream->active = stream->draining = stream->discard = false;
        stream->desired = 0; stream->ring.clear(); stream->phase = 0;
      }
      s->owner.playbackAlternate_ = s->owner.captureAlternate_ = 0;
      s->owner.events_.clear();
    }
  }
  static bool deinit() { resetAll(0); return true; }
  static uint16_t open(uint8_t port, const EspUsbInterfaceDescriptor *itf, uint16_t available) {
    if (available < 9 || itf->bLength != 9 || itf->bInterfaceClass != 1 || itf->bInterfaceSubClass != 1) return 0;
    auto *s = first;
    while (s && (s->owner.graph_.controlInterface != itf->bInterfaceNumber || s->owner.device_.config().startMode != EspUsbDeviceStartMode::Usb)) s = s->next;
    if (!s) return 0;
    const auto *data = reinterpret_cast<const uint8_t *>(itf);
    size_t offset = 0; uint8_t current = itf->bInterfaceNumber;
    while (offset + 2 <= available) {
      const auto *d = data + offset;
      if (d[0] < 2 || d[0] > available - offset) return 0;
      if (offset && d[1] == 11) break;
      if (d[1] == 4) {
        if (d[0] != 9) return 0;
        if (offset && (d[5] != 1 || d[6] == 1)) break;
        current = d[2];
      }
      if (d[1] == 5) {
        if (d[0] < 7 || (d[3] & 3) != ESPUSB_XFER_ISOCHRONOUS) return 0;
        const uint16_t size = uint16_t(d[4] | uint16_t(d[5]) << 8);
        AudioPacketStream *stream = d[2] == s->owner.graph_.playback.dataEndpoint ? &s->playback : d[2] == s->owner.graph_.capture.dataEndpoint ? &s->capture : nullptr;
        if (stream) {
          if (!stream->packet || !size || size > stream->packetCapacity || current != (stream == &s->playback ? s->owner.graph_.playback.interfaceNumber : s->owner.graph_.capture.interfaceNumber)) return 0;
          stream->descriptor = d; stream->endpoint = d[2]; stream->packetSize = size;
        } else if (d[2] == s->owner.graph_.playback.feedbackEndpoint && s->feedback && size == 4) s->feedbackEndpoint = d[2];
        else return 0;
        EspUsbEndpointDescriptor endpoint; memcpy(&endpoint, d, 7); endpoint.bLength = 7;
        if (!espusb_device_edpt_open(port, &endpoint)) return 0;
      }
      offset += d[0];
    }
    if ((s->owner.playbackEnabled_ && !s->playback.endpoint) || (s->owner.captureEnabled_ && !s->capture.endpoint)) return 0;
    s->port = port; s->controlInterface = itf->bInterfaceNumber;
    s->intervals = espusb_device_speed() == ESPUSB_SPEED_HIGH ? 8000 : 1000;
    return uint16_t(offset);
  }
  static EspUsbAudioState *forRequest(const EspUsbControlRequest *r) {
    for (auto *s = first; s; s = s->next) {
      const uint8_t recipient = r->bmRequestType & 31;
      if (recipient == 1 && (uint8_t(r->wIndex) == s->controlInterface || (s->owner.playbackEnabled_ && uint8_t(r->wIndex) == s->owner.graph_.playback.interfaceNumber) || (s->owner.captureEnabled_ && uint8_t(r->wIndex) == s->owner.graph_.capture.interfaceNumber))) return s;
      if (recipient == 2 && !uint8_t(r->wIndex >> 8) && (r->wIndex == s->playback.endpoint || r->wIndex == s->capture.endpoint)) return s;
    }
    return nullptr;
  }
  static bool controlRequest(uint8_t port, uint8_t phase, const EspUsbControlRequest *r) {
    auto *s = forRequest(r); if (!s || s->stopping) return false;
    if (r->bmRequestType == 1 && r->bRequest == 11 && r->wValue <= 1 && !r->wLength && !(r->wIndex >> 8)) {
      if (phase != ESPUSB_CONTROL_STAGE_SETUP) return true;
      AudioGuard guard;
      AudioPacketStream *stream = s->owner.playbackEnabled_ && r->wIndex == s->owner.graph_.playback.interfaceNumber ? &s->playback : s->owner.captureEnabled_ && r->wIndex == s->owner.graph_.capture.interfaceNumber ? &s->capture : nullptr;
      if (!stream) return !r->wValue && espusb_device_control_status(port, r);
      if (!s->change(*stream, uint8_t(r->wValue), stream == &s->playback)) return false;
      s->owner.handleSetInterface(r);
      return espusb_device_control_status(port, r);
    }
    if (r->bmRequestType == 0x81 && r->bRequest == 10 && !r->wValue && r->wLength == 1 && !(r->wIndex >> 8)) {
      if (phase != ESPUSB_CONTROL_STAGE_SETUP) return true;
      s->alternateResponse = r->wIndex == s->owner.graph_.playback.interfaceNumber ? s->playback.desired : r->wIndex == s->owner.graph_.capture.interfaceNumber ? s->capture.desired : 0;
      return espusb_device_control_transfer(port, r, &s->alternateResponse, 1);
    }
    if (r->bmRequestType == 0xa1 || r->bmRequestType == 0xa2) {
      if (phase != ESPUSB_CONTROL_STAGE_SETUP) return true;
      return r->bmRequestType == 0xa1 ? s->owner.handleGetEntityRequest(port, r) : s->owner.handleGetEndpointRequest(port, r);
    }
    if ((r->bmRequestType == 0x21 || r->bmRequestType == 0x22) && r->wLength && r->wLength <= 4) {
      if (phase == ESPUSB_CONTROL_STAGE_SETUP) return espusb_device_control_transfer(port, r, s->control, r->wLength);
      if (phase == ESPUSB_CONTROL_STAGE_DATA) {
        if (espusb_device_control_received_bytes() != r->wLength) return false;
        return r->bmRequestType == 0x21 ? s->owner.handleSetEntityRequest(r, s->control) : s->owner.handleSetEndpointRequest(r, s->control);
      }
      return true;
    }
    return false;
  }
  static bool complete(uint8_t, uint8_t endpoint, EspUsbControllerResult result, uint32_t bytes) {
    AudioGuard guard;
    auto *s = first;
    while (s && endpoint != s->playback.endpoint && endpoint != s->capture.endpoint && endpoint != s->feedbackEndpoint) s = s->next;
    if (!s) return false;
    if (s->stopping) return true;
    if (endpoint == s->playback.endpoint && s->playback.active && !s->playback.draining) {
      auto &stream = s->playback;
      if (result == ESPUSB_TRANSFER_SUCCESS && bytes <= stream.packetSize) {
        const size_t framed = bytes - bytes % stream.frameBytes;
        const size_t copied = stream.ring.push(stream.packet, framed);
        if (copied < bytes) {
          s->owner.playback_.overrunCount_.fetch_add(1, std::memory_order_relaxed);
          s->owner.playback_.overrunBytes_.fetch_add(bytes - copied, std::memory_order_relaxed);
        }
        s->owner.handlePlaybackTransfer(uint16_t(bytes), 1);
      }
      s->armPlayback();
    } else if (endpoint == s->capture.endpoint && s->capture.active && !s->capture.draining) {
      if (result == ESPUSB_TRANSFER_SUCCESS) s->owner.handleCaptureTransfer(uint16_t(bytes), 1);
      s->armCapture();
    } else if (endpoint == s->feedbackEndpoint && s->playback.active && !s->playback.draining) s->armFeedback();
    return true;
  }
};
EspUsbAudioState *EspUsbAudioState::first = nullptr;
namespace {
const espusb::internal::UsbClassDriver &audioDriver() {
  static const espusb::internal::UsbClassDriver driver = {
    .init = EspUsbAudioState::init, .deinit = EspUsbAudioState::deinit,
    .reset = EspUsbAudioState::resetAll, .open = EspUsbAudioState::open,
    .control_xfer_cb = EspUsbAudioState::controlRequest, .xfer_cb = EspUsbAudioState::complete, .sof = nullptr
  };
  return driver;
}
}
namespace espusb { namespace internal {
esp_err_t AudioTransport::begin(EspUsbAudioFunction &owner) {
  size_t sizes[2] = {};
  const AudioStreamModel *streams[] = {&owner.model_.playback(), &owner.model_.capture()};
  const size_t counts[] = {owner.config_.playbackBufferFrames, owner.config_.captureBufferFrames};
  for (unsigned i = 0; i < 2; ++i) {
    if (!streams[i]->formatCount()) continue;
    const auto *format = streams[i]->format(0);
    const size_t frame = format->channels * format->subslotBytes;
    if (counts[i] > SIZE_MAX / frame) return ESP_ERR_INVALID_SIZE;
    sizes[i] = counts[i] * frame;
  }
  const uintptr_t rx = reinterpret_cast<uintptr_t>(owner.config_.playbackBuffer), tx = reinterpret_cast<uintptr_t>(owner.config_.captureBuffer);
  if ((sizes[0] && rx > UINTPTR_MAX - sizes[0]) || (sizes[1] && tx > UINTPTR_MAX - sizes[1]) ||
      (rx && tx && sizes[0] && sizes[1] && rx < tx + sizes[1] && tx < rx + sizes[0])) return ESP_ERR_INVALID_ARG;
  auto *s = newUsbObject<EspUsbAudioState>(owner);
  if (!s || !s->allocate() || (!EspUsbAudioState::first && !espUsbDeviceRegisterAppDrivers(&audioDriver(), 1))) {
    deleteUsbObject(s); return ESP_ERR_NO_MEM;
  }
  AudioGuard guard;
  s->next = EspUsbAudioState::first; EspUsbAudioState::first = s; owner.state_ = s; return ESP_OK;
}
void AudioTransport::prepareToStop(EspUsbAudioFunction &owner) { AudioGuard guard; if (owner.state_) owner.state_->stopping = true; }
void AudioTransport::end(EspUsbAudioFunction &owner) {
  EspUsbAudioState *s;
  bool empty;
  {
    AudioGuard guard; s = owner.state_; owner.state_ = nullptr;
    auto **link = &EspUsbAudioState::first;
    while (*link && *link != s) link = &(*link)->next;
    if (*link) *link = s->next;
    empty = !EspUsbAudioState::first;
  }
  deleteUsbObject(s);
  if (empty) espUsbDeviceUnregisterAppDrivers(&audioDriver(), 1);
}
size_t AudioTransport::available(const EspUsbAudioFunction &owner, bool playback) {
  AudioGuard guard; auto *s = owner.state_;
  if (!s || s->stopping) return 0;
  auto &stream = playback ? s->playback : s->capture;
  if (stream.draining) return 0;
  return playback ? stream.ring.used : stream.ring.capacity - stream.ring.used;
}
size_t AudioTransport::capacity(const EspUsbAudioFunction &owner, bool playback, bool packet) {
  AudioGuard guard; if (!owner.state_) return 0;
  const auto &stream = playback ? owner.state_->playback : owner.state_->capture;
  return packet ? stream.packetCapacity : stream.ring.capacity;
}
size_t AudioTransport::read(EspUsbAudioFunction &owner, void *data, size_t length) {
  AudioGuard guard; auto *s = owner.state_;
  if (!data || !s || s->stopping || s->playback.draining || !s->playback.frameBytes) return 0;
  length -= length % s->playback.frameBytes;
  return s->playback.ring.pop(static_cast<uint8_t *>(data), length);
}
size_t AudioTransport::write(EspUsbAudioFunction &owner, const void *data, size_t length) {
  AudioGuard guard; auto *s = owner.state_;
  if (!data || !s || s->stopping || s->capture.draining || !s->capture.frameBytes) return 0;
  length -= length % s->capture.frameBytes;
  const size_t copied = s->capture.ring.push(static_cast<const uint8_t *>(data), length);
  if (copied < length) {
    owner.capture_.overrunCount_.fetch_add(1, std::memory_order_relaxed);
    owner.capture_.overrunBytes_.fetch_add(length - copied, std::memory_order_relaxed);
  }
  return copied;
}
bool AudioTransport::clear(EspUsbAudioFunction &owner, bool playback) {
  AudioGuard guard; if (!owner.state_ || owner.state_->stopping) return false;
  (playback ? owner.state_->playback : owner.state_->capture).ring.clear(); return true;
}
bool AudioTransport::reply(EspUsbAudioFunction &owner, uint8_t port, const void *request, const void *data, size_t bytes) {
  AudioGuard guard; auto *s = owner.state_;
  if (!s || s->stopping || bytes > sizeof(s->control)) return false;
  memcpy(s->control, data, bytes);
  return espusb_device_control_transfer(port, static_cast<const EspUsbControlRequest *>(request), s->control, uint16_t(bytes));
}
} }
