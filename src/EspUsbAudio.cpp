#include "EspUsbDevice.h"
#include "internal/EspUsbAudioRequest.h"

#include <string.h>

#if __has_include("soc/soc_caps.h")
#include "soc/soc_caps.h"
#endif

#if defined(SOC_USB_OTG_SUPPORTED) && SOC_USB_OTG_SUPPORTED
#include "tusb.h"
#include "class/audio/audio_device.h"
#define ESP_USB_AUDIO_HAS_TINYUSB 1
#else
#define ESP_USB_AUDIO_HAS_TINYUSB 0
#endif

namespace {

EspUsbAudioFunction *g_activeAudio = nullptr;

#if ESP_USB_AUDIO_HAS_TINYUSB
espusb::internal::Uac2EntityRequest
toEntityRequest(const tusb_control_request_t *request)
{
  espusb::internal::Uac2EntityRequest result;
  result.request = request->bRequest;
  result.channel = static_cast<uint8_t>(request->wValue & 0xff);
  result.selector = static_cast<uint8_t>(request->wValue >> 8);
  result.interfaceNumber = static_cast<uint8_t>(request->wIndex & 0xff);
  result.entityId = static_cast<uint8_t>(request->wIndex >> 8);
  result.length = request->wLength;
  return result;
}

uint16_t correctAudioFifoOverflow(tu_fifo_t *fifo)
{
  if (!fifo)
  {
    return 0;
  }
  const uint16_t count =
      tu_ff_overflow_count(fifo->depth, fifo->wr_idx, fifo->rd_idx);
  if (count <= fifo->depth)
  {
    return 0;
  }
  const uint16_t overflow = static_cast<uint16_t>(count - fifo->depth);
  tu_fifo_correct_read_pointer(fifo);
  return overflow;
}
#endif

} // namespace

EspUsbAudioFunction::EspUsbAudioFunction(EspUsbDevice &device,
                                         EspUsbAudioProtocol protocol)
    : EspUsbDeviceClass(device),
      protocol_(protocol),
      model_(protocol == EspUsbAudioProtocol::Uac2
                 ? espusb::internal::AudioProtocol::Uac2
                 : espusb::internal::AudioProtocol::Uac1)
{
}

EspUsbAudioFunction::~EspUsbAudioFunction()
{
  end();
}

EspUsbAudioPlaybackStream &EspUsbAudioFunction::addPlaybackStream()
{
  playbackEnabled_ = true;
  return playback_;
}

EspUsbAudioCaptureStream &EspUsbAudioFunction::addCaptureStream()
{
  captureEnabled_ = true;
  return capture_;
}

bool EspUsbAudioPlaybackStream::addFormat(const EspUsbAudioFormat &format)
{
  return function_.addFormat(espusb::internal::AudioDirection::Playback,
                             format);
}

bool EspUsbAudioCaptureStream::addFormat(const EspUsbAudioFormat &format)
{
  return function_.addFormat(espusb::internal::AudioDirection::Capture,
                             format);
}

bool EspUsbAudioFunction::addFormat(
    espusb::internal::AudioDirection direction,
    const EspUsbAudioFormat &format)
{
  if (format.sampleRate == 0 || format.channels == 0)
  {
    return false;
  }
  const espusb::internal::AudioPcmFormat internalFormat{
      format.sampleRate, format.channels, format.bytesPerSample,
      format.bitsPerSample};
  espusb::internal::AudioStreamModel &stream =
      direction == espusb::internal::AudioDirection::Playback
          ? model_.playback()
          : model_.capture();
  // Both protocol writers currently emit one topology at both speeds.
  // Reject a format that cannot be represented on a negotiated FS link.
  return stream.addFormat(
      internalFormat, espusb::internal::UsbSpeed::Full,
      espusb::internal::defaultAudioBusLimits(
          espusb::internal::UsbSpeed::Full));
}

bool EspUsbAudioFunction::buildGraph(uint8_t interfaceNumber,
                                     uint8_t endpointNumber)
{
  if (interfaceNumber == 0xff || endpointNumber == 0 ||
      endpointNumber > 15)
  {
    return false;
  }
  espusb::internal::DescriptorLayout layout(
      espusb::internal::EndpointLimits{15, 15, 15});
  if (interfaceNumber != 0)
  {
    layout.allocateInterfaces(interfaceNumber);
  }
  for (uint8_t endpoint = 1; endpoint < endpointNumber; ++endpoint)
  {
    if (!layout.reserveEndpoint(endpoint) ||
        !layout.reserveEndpoint(static_cast<uint8_t>(0x80 | endpoint)))
    {
      return false;
    }
  }
  espusb::internal::AudioFunctionGraphConfig config;
  config.playbackFeedback = protocol_ == EspUsbAudioProtocol::Uac2;
  if (!espusb::internal::buildAudioFunctionGraph(model_, layout, config,
                                                  graph_))
  {
    return false;
  }
  return controls_.configure(model_, graph_);
}

bool EspUsbAudioFunction::begin()
{
  if ((!playbackEnabled_ && !captureEnabled_) ||
      (playbackEnabled_ && model_.playback().formatCount() != 1) ||
      (captureEnabled_ && model_.capture().formatCount() != 1))
  {
    return false;
  }
  if (g_activeAudio && g_activeAudio != this)
  {
    return false;
  }
  if (!buildGraph(graph_.controlInterface,
                  graph_.playback.dataEndpoint
                      ? static_cast<uint8_t>(
                            graph_.playback.dataEndpoint & 0x0f)
                      : 1))
  {
    return false;
  }
  events_.clear();
  playbackAlternate_ = 0;
  captureAlternate_ = 0;
  playback_.resetStats();
  capture_.resetStats();
  g_activeAudio = this;
  return true;
}

void EspUsbAudioFunction::end()
{
  if (g_activeAudio == this)
  {
    g_activeAudio = nullptr;
  }
  playbackAlternate_ = 0;
  captureAlternate_ = 0;
  events_.clear();
  playback_.resetStats();
  capture_.resetStats();
}

uint8_t EspUsbAudioFunction::interfaceCount() const
{
  return static_cast<uint8_t>(
      1U + (playbackEnabled_ ? 1U : 0U) + (captureEnabled_ ? 1U : 0U));
}

uint8_t EspUsbAudioFunction::endpointCount() const
{
  return static_cast<uint8_t>((playbackEnabled_ ? 1U : 0U) +
                              (playbackEnabled_ &&
                                       protocol_ == EspUsbAudioProtocol::Uac2
                                   ? 1U
                                   : 0U) +
                              (captureEnabled_ ? 1U : 0U));
}

uint16_t EspUsbAudioFunction::configurationDescriptor(
    uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber,
    uint16_t endpointSize)
{
  return configurationDescriptorForSpeed(
      dst, 256, interfaceNumber, endpointNumber, endpointSize > 64);
}

uint16_t EspUsbAudioFunction::configurationDescriptorForSpeed(
    uint8_t *dst, size_t capacity, uint8_t interfaceNumber,
    uint8_t endpointNumber, bool highSpeed)
{
  if (!dst || !buildGraph(interfaceNumber, endpointNumber))
  {
    return 0;
  }
  espusb::internal::DescriptorBuffer buffer(dst, capacity);
  espusb::internal::DescriptorBuildContext context(
      highSpeed ? espusb::internal::UsbSpeed::High
                : espusb::internal::UsbSpeed::Full,
      buffer);
  espusb::internal::AudioDescriptorError error;
  const bool written =
      protocol_ == EspUsbAudioProtocol::Uac2
          ? espusb::internal::writeUac2Function(
                context, model_, graph_,
                espusb::internal::AudioDescriptorConfig{}, &error)
          : espusb::internal::writeUac1Function(
                context, model_, graph_,
                espusb::internal::AudioDescriptorConfig{}, &error);
  if (!written)
  {
    return 0;
  }
  return static_cast<uint16_t>(buffer.size());
}

int EspUsbAudioPlaybackStream::available() const
{
#if ESP_USB_AUDIO_HAS_TINYUSB
  return static_cast<int>(tud_audio_available());
#else
  return 0;
#endif
}

size_t EspUsbAudioPlaybackStream::read(void *data, size_t length)
{
#if ESP_USB_AUDIO_HAS_TINYUSB
  if (!data || length == 0)
  {
    return 0;
  }
  return tud_audio_read(data, static_cast<uint16_t>(
                                  length > 0xffffU ? 0xffffU : length));
#else
  (void)data;
  (void)length;
  return 0;
#endif
}

bool EspUsbAudioPlaybackStream::clearBuffer()
{
#if ESP_USB_AUDIO_HAS_TINYUSB
  return tud_audio_clear_ep_out_ff();
#else
  return false;
#endif
}

EspUsbAudioStreamStats EspUsbAudioPlaybackStream::stats() const
{
  EspUsbAudioStreamStats result;
  result.transferredBytes =
      transferredBytes_.load(std::memory_order_relaxed);
  result.overrunCount = overrunCount_.load(std::memory_order_relaxed);
  result.overrunBytes = overrunBytes_.load(std::memory_order_relaxed);
  return result;
}

void EspUsbAudioPlaybackStream::resetStats()
{
  transferredBytes_.store(0, std::memory_order_relaxed);
  overrunCount_.store(0, std::memory_order_relaxed);
  overrunBytes_.store(0, std::memory_order_relaxed);
}

size_t EspUsbAudioCaptureStream::write(const void *data, size_t length)
{
#if ESP_USB_AUDIO_HAS_TINYUSB
  if (!data || length == 0)
  {
    return 0;
  }
  const uint16_t requested = static_cast<uint16_t>(
      length > 0xffffU ? 0xffffU : length);
  tu_fifo_t *fifo = tud_audio_get_ep_in_ff();
  uint32_t overwritten = 0;
  if (fifo)
  {
    const uint16_t count = tu_fifo_count(fifo);
    const uint16_t remaining =
        static_cast<uint16_t>(fifo->depth - count);
    overwritten =
        requested >= fifo->depth
            ? static_cast<uint32_t>(count) + requested - fifo->depth
            : (requested > remaining ? requested - remaining : 0);
  }
  const uint16_t written = tud_audio_write(data, requested);
  if (overwritten)
  {
    overrunCount_.fetch_add(1, std::memory_order_relaxed);
    overrunBytes_.fetch_add(overwritten, std::memory_order_relaxed);
  }
  return written;
#else
  (void)data;
  (void)length;
  return 0;
#endif
}

bool EspUsbAudioCaptureStream::clearBuffer()
{
#if ESP_USB_AUDIO_HAS_TINYUSB
  return tud_audio_clear_ep_in_ff();
#else
  return false;
#endif
}

EspUsbAudioStreamStats EspUsbAudioCaptureStream::stats() const
{
  EspUsbAudioStreamStats result;
  result.transferredBytes =
      transferredBytes_.load(std::memory_order_relaxed);
  result.overrunCount = overrunCount_.load(std::memory_order_relaxed);
  result.overrunBytes = overrunBytes_.load(std::memory_order_relaxed);
  result.underrunCount = underrunCount_.load(std::memory_order_relaxed);
  result.underrunBytes = underrunBytes_.load(std::memory_order_relaxed);
  return result;
}

void EspUsbAudioCaptureStream::resetStats()
{
  transferredBytes_.store(0, std::memory_order_relaxed);
  overrunCount_.store(0, std::memory_order_relaxed);
  overrunBytes_.store(0, std::memory_order_relaxed);
  underrunCount_.store(0, std::memory_order_relaxed);
  underrunBytes_.store(0, std::memory_order_relaxed);
}

uint32_t EspUsbAudioFunction::currentSampleRate() const
{
  return controls_.currentSampleRate();
}

uint8_t EspUsbAudioFunction::controlEntityId(
    EspUsbAudioDirection direction) const
{
  const espusb::internal::AudioDirection internalDirection =
      direction == EspUsbAudioDirection::Playback
          ? espusb::internal::AudioDirection::Playback
          : espusb::internal::AudioDirection::Capture;
  for (size_t i = 0; i < graph_.entityCount; ++i)
  {
    const espusb::internal::AudioEntity &entity = graph_.entities[i];
    if (entity.kind == espusb::internal::AudioEntityKind::FeatureUnit &&
        entity.direction == internalDirection)
    {
      return entity.id;
    }
  }
  return 0;
}

bool EspUsbAudioFunction::hasMute(EspUsbAudioDirection direction,
                                  uint8_t channel) const
{
  bool muted = false;
  return getMute(muted, direction, channel);
}

bool EspUsbAudioFunction::hasVolume(EspUsbAudioDirection direction,
                                    uint8_t channel) const
{
  int16_t volume = 0;
  return getVolume(volume, direction, channel);
}

bool EspUsbAudioFunction::getMute(bool &mute,
                                  EspUsbAudioDirection direction,
                                  uint8_t channel) const
{
  const uint8_t entityId = controlEntityId(direction);
  int32_t value = 0;
  if (entityId == 0 ||
      !controls_.current(entityId,
                         espusb::internal::AudioControlSelector::Mute,
                         channel, value))
  {
    return false;
  }
  mute = value != 0;
  return true;
}

bool EspUsbAudioFunction::setMute(bool mute,
                                  EspUsbAudioDirection direction,
                                  uint8_t channel)
{
  const uint8_t entityId = controlEntityId(direction);
  int32_t previous = 0;
  const int32_t value = mute ? 1 : 0;
  if (entityId == 0 ||
      !controls_.current(entityId,
                         espusb::internal::AudioControlSelector::Mute,
                         channel, previous) ||
      !controls_.setCurrent(entityId,
                            espusb::internal::AudioControlSelector::Mute,
                            channel, value))
  {
    return false;
  }
  if (previous != value)
  {
    pushControlEvent(espusb::internal::AudioControlSelector::Mute,
                     entityId, channel, value);
  }
  return true;
}

bool EspUsbAudioFunction::getVolume(int16_t &volume,
                                    EspUsbAudioDirection direction,
                                    uint8_t channel) const
{
  const uint8_t entityId = controlEntityId(direction);
  int32_t value = 0;
  if (entityId == 0 ||
      !controls_.current(entityId,
                         espusb::internal::AudioControlSelector::Volume,
                         channel, value))
  {
    return false;
  }
  volume = static_cast<int16_t>(value);
  return true;
}

bool EspUsbAudioFunction::setVolume(int16_t volume,
                                    EspUsbAudioDirection direction,
                                    uint8_t channel)
{
  const uint8_t entityId = controlEntityId(direction);
  int32_t previous = 0;
  if (entityId == 0 ||
      !controls_.current(entityId,
                         espusb::internal::AudioControlSelector::Volume,
                         channel, previous) ||
      !controls_.setCurrent(entityId,
                            espusb::internal::AudioControlSelector::Volume,
                            channel, volume))
  {
    return false;
  }
  if (previous != volume)
  {
    pushControlEvent(espusb::internal::AudioControlSelector::Volume,
                     entityId, channel, volume);
  }
  return true;
}

bool EspUsbAudioFunction::getVolumeRange(
    EspUsbAudioVolumeRange &range, EspUsbAudioDirection direction,
    uint8_t channel) const
{
  const uint8_t entityId = controlEntityId(direction);
  espusb::internal::AudioControlRange internalRange;
  if (entityId == 0 ||
      !controls_.range(entityId,
                       espusb::internal::AudioControlSelector::Volume,
                       channel, internalRange))
  {
    return false;
  }
  range.min = static_cast<int16_t>(internalRange.minimum);
  range.max = static_cast<int16_t>(internalRange.maximum);
  range.resolution = static_cast<int16_t>(internalRange.resolution);
  return true;
}

EspUsbAudioEventTarget
EspUsbAudioFunction::eventTarget(uint8_t entityId) const
{
  if (entityId == graph_.clockSourceId)
  {
    return EspUsbAudioEventTarget::Function;
  }
  for (size_t i = 0; i < graph_.entityCount; ++i)
  {
    if (graph_.entities[i].id != entityId)
    {
      continue;
    }
    return graph_.entities[i].direction ==
                   espusb::internal::AudioDirection::Playback
               ? EspUsbAudioEventTarget::Playback
               : EspUsbAudioEventTarget::Capture;
  }
  return EspUsbAudioEventTarget::Function;
}

void EspUsbAudioFunction::pushControlEvent(
    espusb::internal::AudioControlSelector selector, uint8_t entityId,
    uint8_t channel, int32_t value)
{
  espusb::internal::AudioRuntimeEvent event;
  const EspUsbAudioEventTarget target = eventTarget(entityId);
  event.target =
      target == EspUsbAudioEventTarget::Playback
          ? espusb::internal::AudioRuntimeEventTarget::Playback
          : (target == EspUsbAudioEventTarget::Capture
                 ? espusb::internal::AudioRuntimeEventTarget::Capture
                 : espusb::internal::AudioRuntimeEventTarget::Function);
  event.channel = channel;
  event.value = value;
  switch (selector)
  {
  case espusb::internal::AudioControlSelector::SampleRate:
    event.type = espusb::internal::AudioRuntimeEventType::SampleRate;
    break;
  case espusb::internal::AudioControlSelector::Mute:
    event.type = espusb::internal::AudioRuntimeEventType::Mute;
    break;
  case espusb::internal::AudioControlSelector::Volume:
    event.type = espusb::internal::AudioRuntimeEventType::Volume;
    break;
  case espusb::internal::AudioControlSelector::ClockValid:
    return;
  }
  events_.push(event);
}

bool EspUsbAudioFunction::pollEvent(EspUsbAudioEvent &event)
{
  espusb::internal::AudioRuntimeEvent queued;
  if (!events_.pop(queued))
  {
    return false;
  }
  event = EspUsbAudioEvent{};
  event.target =
      queued.target == espusb::internal::AudioRuntimeEventTarget::Playback
          ? EspUsbAudioEventTarget::Playback
          : (queued.target ==
                     espusb::internal::AudioRuntimeEventTarget::Capture
                 ? EspUsbAudioEventTarget::Capture
                 : EspUsbAudioEventTarget::Function);
  event.channel = queued.channel;
  event.alternateSetting = queued.alternateSetting;
  switch (queued.type)
  {
  case espusb::internal::AudioRuntimeEventType::SampleRate:
    event.type = EspUsbAudioEventType::SampleRateChanged;
    event.sampleRate = static_cast<uint32_t>(queued.value);
    break;
  case espusb::internal::AudioRuntimeEventType::Mute:
    event.type = EspUsbAudioEventType::MuteChanged;
    event.muted = queued.value != 0;
    break;
  case espusb::internal::AudioRuntimeEventType::Volume:
    event.type = EspUsbAudioEventType::VolumeChanged;
    event.volumeDb256 = static_cast<int16_t>(queued.value);
    break;
  case espusb::internal::AudioRuntimeEventType::StreamState:
    event.type = EspUsbAudioEventType::StreamStateChanged;
    event.enabled = queued.value != 0;
    break;
  }
  return true;
}

size_t EspUsbAudioFunction::pendingEvents() const
{
  return events_.pending();
}

uint32_t EspUsbAudioFunction::droppedEvents() const
{
  return events_.dropped();
}

void EspUsbAudioFunction::clearEvents()
{
  events_.clear();
}

bool EspUsbAudioFunction::handleGetEntityRequest(uint8_t rhport,
                                                 const void *rawRequest)
{
#if ESP_USB_AUDIO_HAS_TINYUSB
  const auto *request =
      static_cast<const tusb_control_request_t *>(rawRequest);
  if (protocol_ == EspUsbAudioProtocol::Uac1)
  {
    if (!request)
    {
      return false;
    }
    const uint8_t interfaceNumber =
        static_cast<uint8_t>(request->wIndex & 0xff);
    const uint8_t entityId =
        static_cast<uint8_t>(request->wIndex >> 8);
    const uint8_t channel =
        static_cast<uint8_t>(request->wValue & 0xff);
    const uint8_t selector =
        static_cast<uint8_t>(request->wValue >> 8);
    if (interfaceNumber != graph_.controlInterface ||
        controls_.entityKind(entityId) !=
            espusb::internal::AudioControlEntityKind::Feature ||
        (selector != 1 && selector != 2))
    {
      return false;
    }
    const auto control =
        selector == 1
            ? espusb::internal::AudioControlSelector::Mute
            : espusb::internal::AudioControlSelector::Volume;
    int32_t value = 0;
    if (request->bRequest == 0x81)
    {
      if (!controls_.current(entityId, control, channel, value))
      {
        return false;
      }
    }
    else
    {
      if (control != espusb::internal::AudioControlSelector::Volume ||
          request->bRequest < 0x82 || request->bRequest > 0x84)
      {
        return false;
      }
      espusb::internal::AudioControlRange range;
      if (!controls_.range(entityId, control, channel, range))
      {
        return false;
      }
      value = request->bRequest == 0x82
                  ? range.minimum
                  : (request->bRequest == 0x83
                         ? range.maximum
                         : range.resolution);
    }
    uint8_t response[2] = {
        static_cast<uint8_t>(value & 0xff),
        static_cast<uint8_t>((value >> 8) & 0xff),
    };
    // wLength bounds what the host accepts; TinyUSB clips the data stage to
    // min(wLength, responseLength), so only a zero-length read is invalid.
    const uint16_t responseLength = selector == 1 ? 1 : 2;
    if (request->wLength == 0)
    {
      return false;
    }
    return tud_audio_buffer_and_schedule_control_xfer(
        rhport, request, response, responseLength);
  }
  uint8_t responseData[CFG_TUD_AUDIO_CTRL_BUF_SZ] = {};
  espusb::internal::DescriptorBuffer response(responseData,
                                              sizeof(responseData));
  if (!espusb::internal::writeUac2EntityResponse(
          controls_, graph_.controlInterface, toEntityRequest(request),
          response))
  {
    return false;
  }
  return tud_audio_buffer_and_schedule_control_xfer(
      rhport, request, response.data(),
      static_cast<uint16_t>(response.size()));
#else
  (void)rhport;
  (void)rawRequest;
  return false;
#endif
}

bool EspUsbAudioFunction::handleSetEntityRequest(const void *rawRequest,
                                                 const uint8_t *data)
{
#if ESP_USB_AUDIO_HAS_TINYUSB
  const auto *request =
      static_cast<const tusb_control_request_t *>(rawRequest);
  if (protocol_ == EspUsbAudioProtocol::Uac1)
  {
    if (!request || !data || request->bRequest != 0x01)
    {
      return false;
    }
    const uint8_t interfaceNumber =
        static_cast<uint8_t>(request->wIndex & 0xff);
    const uint8_t entityId =
        static_cast<uint8_t>(request->wIndex >> 8);
    const uint8_t channel =
        static_cast<uint8_t>(request->wValue & 0xff);
    const uint8_t selector =
        static_cast<uint8_t>(request->wValue >> 8);
    if (interfaceNumber != graph_.controlInterface ||
        controls_.entityKind(entityId) !=
            espusb::internal::AudioControlEntityKind::Feature ||
        (selector != 1 && selector != 2))
    {
      return false;
    }
    const auto control =
        selector == 1
            ? espusb::internal::AudioControlSelector::Mute
            : espusb::internal::AudioControlSelector::Volume;
    const uint16_t expectedLength = selector == 1 ? 1 : 2;
    if (request->wLength != expectedLength)
    {
      return false;
    }
    const int32_t value =
        selector == 1
            ? data[0]
            : static_cast<int16_t>(
                  static_cast<uint16_t>(data[0]) |
                  static_cast<uint16_t>(
                      static_cast<uint16_t>(data[1]) << 8));
    int32_t previous = 0;
    if (!controls_.current(entityId, control, channel, previous) ||
        !controls_.setCurrent(entityId, control, channel, value))
    {
      return false;
    }
    if (previous != value)
    {
      pushControlEvent(control, entityId, channel, value);
    }
    return true;
  }
  espusb::internal::AudioControlChange change;
  const bool applied = espusb::internal::applyUac2EntityRequest(
      controls_, graph_.controlInterface, toEntityRequest(request), data,
      request->wLength, nullptr, &change);
  if (applied && change.changed)
  {
    pushControlEvent(change.selector, change.entityId, change.channel,
                     change.value);
  }
  return applied;
#else
  (void)rawRequest;
  (void)data;
  return false;
#endif
}

bool EspUsbAudioFunction::handleGetEndpointRequest(uint8_t rhport,
                                                   const void *rawRequest)
{
#if ESP_USB_AUDIO_HAS_TINYUSB
  if (protocol_ != EspUsbAudioProtocol::Uac1 || !rawRequest)
  {
    return false;
  }
  const auto *request =
      static_cast<const tusb_control_request_t *>(rawRequest);
  const uint8_t endpoint = static_cast<uint8_t>(request->wIndex & 0xff);
  const uint8_t selector = static_cast<uint8_t>(request->wValue >> 8);
  const espusb::internal::AudioPcmFormat *format = nullptr;
  if (graph_.playback.present && endpoint == graph_.playback.dataEndpoint)
  {
    format = model_.playback().format(0);
  }
  else if (graph_.capture.present && endpoint == graph_.capture.dataEndpoint)
  {
    format = model_.capture().format(0);
  }
  if (!format || selector != 1 || request->wLength != 3 ||
      request->bRequest < 0x81 || request->bRequest > 0x84)
  {
    return false;
  }
  const uint32_t rate =
      request->bRequest == 0x84 ? 0 : format->sampleRate;
  const uint8_t response[3] = {
      static_cast<uint8_t>(rate & 0xffU),
      static_cast<uint8_t>((rate >> 8) & 0xffU),
      static_cast<uint8_t>((rate >> 16) & 0xffU),
  };
  return tud_audio_buffer_and_schedule_control_xfer(
      rhport, request, const_cast<uint8_t *>(response), sizeof(response));
#else
  (void)rhport;
  (void)rawRequest;
  return false;
#endif
}

bool EspUsbAudioFunction::handleSetEndpointRequest(const void *rawRequest,
                                                   const uint8_t *data)
{
#if ESP_USB_AUDIO_HAS_TINYUSB
  if (protocol_ != EspUsbAudioProtocol::Uac1 || !rawRequest || !data)
  {
    return false;
  }
  const auto *request =
      static_cast<const tusb_control_request_t *>(rawRequest);
  const uint8_t endpoint = static_cast<uint8_t>(request->wIndex & 0xff);
  const uint8_t selector = static_cast<uint8_t>(request->wValue >> 8);
  const espusb::internal::AudioPcmFormat *format = nullptr;
  if (graph_.playback.present && endpoint == graph_.playback.dataEndpoint)
  {
    format = model_.playback().format(0);
  }
  else if (graph_.capture.present && endpoint == graph_.capture.dataEndpoint)
  {
    format = model_.capture().format(0);
  }
  if (!format || request->bRequest != 0x01 || selector != 1 ||
      request->wLength != 3)
  {
    return false;
  }
  const uint32_t rate = static_cast<uint32_t>(data[0]) |
                        (static_cast<uint32_t>(data[1]) << 8) |
                        (static_cast<uint32_t>(data[2]) << 16);
  if (format->sampleRate != rate)
  {
    return false;
  }
  // Each UAC1 endpoint currently advertises one discrete, fixed rate. SET_CUR
  // confirms that rate; it does not mutate the function-wide UAC2 clock state.
  return true;
#else
  (void)rawRequest;
  (void)data;
  return false;
#endif
}

bool EspUsbAudioFunction::handleSetInterface(const void *rawRequest)
{
#if ESP_USB_AUDIO_HAS_TINYUSB
  if (!rawRequest)
  {
    return false;
  }
  const auto *request =
      static_cast<const tusb_control_request_t *>(rawRequest);
  const uint8_t interfaceNumber =
      static_cast<uint8_t>(request->wIndex & 0xff);
  const uint8_t alternateSetting =
      static_cast<uint8_t>(request->wValue & 0xff);
  uint8_t *current = nullptr;
  espusb::internal::AudioRuntimeEventTarget target;
  if (graph_.playback.present &&
      interfaceNumber == graph_.playback.interfaceNumber)
  {
    current = &playbackAlternate_;
    target = espusb::internal::AudioRuntimeEventTarget::Playback;
  }
  else if (graph_.capture.present &&
           interfaceNumber == graph_.capture.interfaceNumber)
  {
    current = &captureAlternate_;
    target = espusb::internal::AudioRuntimeEventTarget::Capture;
  }
  else
  {
    return true;
  }
  if (*current == alternateSetting)
  {
    return true;
  }
  *current = alternateSetting;
  espusb::internal::AudioRuntimeEvent event;
  event.type = espusb::internal::AudioRuntimeEventType::StreamState;
  event.target = target;
  event.alternateSetting = alternateSetting;
  event.value = alternateSetting != 0 ? 1 : 0;
  events_.push(event);
  return true;
#else
  (void)rawRequest;
  return false;
#endif
}

bool EspUsbAudioFunction::handlePlaybackTransfer(
    uint16_t bytes, uint8_t alternateSetting)
{
#if ESP_USB_AUDIO_HAS_TINYUSB
  if (alternateSetting == 0 || !playbackEnabled_)
  {
    return true;
  }
  playback_.transferredBytes_.fetch_add(bytes,
                                        std::memory_order_relaxed);
  const uint16_t overflow =
      correctAudioFifoOverflow(tud_audio_get_ep_out_ff());
  if (overflow)
  {
    playback_.overrunCount_.fetch_add(1, std::memory_order_relaxed);
    playback_.overrunBytes_.fetch_add(overflow,
                                      std::memory_order_relaxed);
  }
  return true;
#else
  (void)bytes;
  (void)alternateSetting;
  return false;
#endif
}

bool EspUsbAudioFunction::handleCaptureTransfer(
    uint16_t bytes, uint8_t alternateSetting)
{
#if ESP_USB_AUDIO_HAS_TINYUSB
  if (alternateSetting == 0 || !captureEnabled_)
  {
    return true;
  }
  capture_.transferredBytes_.fetch_add(bytes,
                                       std::memory_order_relaxed);
  const espusb::internal::AudioPcmFormat *format =
      model_.capture().format(0);
  if (!format)
  {
    return true;
  }
  const uint32_t bytesPerSecond =
      format->sampleRate * format->channels * format->subslotBytes;
  const uint32_t serviceIntervals =
      tud_speed_get() == TUSB_SPEED_HIGH ? 8000U : 1000U;
  const uint32_t minimumBytes = bytesPerSecond / serviceIntervals;
  if (bytes < minimumBytes)
  {
    capture_.underrunCount_.fetch_add(1, std::memory_order_relaxed);
    capture_.underrunBytes_.fetch_add(minimumBytes - bytes,
                                      std::memory_order_relaxed);
  }
  return true;
#else
  (void)bytes;
  (void)alternateSetting;
  return false;
#endif
}

#if ESP_USB_AUDIO_HAS_TINYUSB
extern "C" bool tud_audio_get_req_entity_cb(
    uint8_t rhport, const tusb_control_request_t *request)
{
  return g_activeAudio &&
         g_activeAudio->handleGetEntityRequest(rhport, request);
}

extern "C" bool tud_audio_set_req_entity_cb(
    uint8_t, const tusb_control_request_t *request, uint8_t *data)
{
  return g_activeAudio &&
         g_activeAudio->handleSetEntityRequest(request, data);
}

extern "C" bool tud_audio_get_req_ep_cb(
    uint8_t rhport, const tusb_control_request_t *request)
{
  return g_activeAudio &&
         g_activeAudio->handleGetEndpointRequest(rhport, request);
}

extern "C" bool tud_audio_get_req_itf_cb(
    uint8_t, const tusb_control_request_t *)
{
  return false;
}

extern "C" bool tud_audio_set_req_ep_cb(
    uint8_t, const tusb_control_request_t *request, uint8_t *data)
{
  return g_activeAudio &&
         g_activeAudio->handleSetEndpointRequest(request, data);
}

extern "C" bool tud_audio_set_req_itf_cb(
    uint8_t, const tusb_control_request_t *, uint8_t *)
{
  return false;
}

extern "C" bool tud_audio_set_itf_cb(
    uint8_t, const tusb_control_request_t *request)
{
  return g_activeAudio &&
         g_activeAudio->handleSetInterface(request);
}

extern "C" bool tud_audio_set_itf_close_ep_cb(
    uint8_t, const tusb_control_request_t *)
{
  return g_activeAudio != nullptr;
}

extern "C" bool tud_audio_rx_done_isr(
    uint8_t, uint16_t bytes, uint8_t, uint8_t,
    uint8_t alternateSetting)
{
  return g_activeAudio &&
         g_activeAudio->handlePlaybackTransfer(bytes,
                                               alternateSetting);
}

extern "C" bool tud_audio_tx_done_isr(
    uint8_t, uint16_t bytes, uint8_t, uint8_t,
    uint8_t alternateSetting)
{
  return g_activeAudio &&
         g_activeAudio->handleCaptureTransfer(bytes,
                                              alternateSetting);
}

extern "C" void tud_audio_feedback_params_cb(
    uint8_t, uint8_t, audio_feedback_params_t *params)
{
  if (!params || !g_activeAudio)
  {
    return;
  }
  params->method = AUDIO_FEEDBACK_METHOD_FIFO_COUNT;
  params->sample_freq = g_activeAudio->currentSampleRate();
}
#endif
