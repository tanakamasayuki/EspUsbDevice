#include "internal/EspUsbDeviceEndpoint.h"
#include "EspUsbDevice.h"
#include "internal/EspUsbDeviceCore.h"
#include "internal/EspUsbDeviceAppDriver.h"
#include "internal/EspUsbMemory.h"
#include "internal/EspUsbRuntime.h"
#include "internal/EspUsbDeviceBus.h"
#include "esp_memory_utils.h"
#include <algorithm>
#include <string.h>
#include <utility>
namespace {
portMUX_TYPE mscLock = portMUX_INITIALIZER_UNLOCKED;
const espusb::internal::UsbClassDriver &mscDriver();
uint32_t le32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint32_t be32(const uint8_t *p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint16_t be16(const uint8_t *p) { return uint16_t(p[0]) << 8 | p[1]; }
void putLe32(uint8_t *p, uint32_t n) { for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(n >> (8 * i)); }
void putBe32(uint8_t *p, uint32_t n) { for (unsigned i = 0; i < 4; ++i) p[3 - i] = uint8_t(n >> (8 * i)); }
}
struct EspUsbMscState {
  static EspUsbMscState *first;
  EspUsbMscState *next = nullptr;
  EspUsbDeviceMsc &owner;
  uint8_t *buffer = nullptr;
  bool owned = false, resetFence = false, clearIn = false, clearOut = false;
  bool interfaceReset = false, needsBotReset = false;
  std::atomic<bool> stopping{false};
  enum class Stage { Command, DataIn, DataOut, Status, StatusSent, NeedReset, Recovery } stage = Stage::Command;
  uint8_t port = 0, interface = 255, in = 0, out = 0, maxLun = 0;
  uint8_t key = 0, asc = 0, ascq = 0, status = 0, command[16] = {};
  uint16_t packetSize = 0;
  uint32_t tag = 0, hostBytes = 0, deviceBytes = 0, done = 0, limit = 0, lba = 0, armed = 0;
  bool hostIn = false, deviceIn = false, blockIo = false;
  uint32_t resetEpoch = 0, fenceEpoch = 0;
  // media_ when the command began, and the last insertion already reported.
  uint32_t commandMedia = 0, insertionsSeen = 0;
  espusb::internal::UsbRuntimeJob recoveryJob{recover, this};
  explicit EspUsbMscState(EspUsbDeviceMsc &o) : owner(o) {}
  ~EspUsbMscState() {
    espusb::internal::removeUsbRuntimeJob(recoveryJob);
    if (owned) espusb::internal::freeUsbMemory(buffer);
  }
  void sense(uint8_t k, uint8_t a, uint8_t q = 0) { key = k; asc = a; ascq = q; status = 1; }
  bool ready() { if (owner.testUnitReady()) return true; sense(2, 0x3a); return false; }
  // Forget media changes made before the Host (re)configured the device.
  void forgetInsertions() { insertionsSeen = owner.insertions_.load(); }
  bool mediumGone() { if (owner.mediaUnchanged(commandMedia)) return false; sense(2, 0x3a); finishData(); return true; }
  bool arm(uint8_t ep, uint32_t bytes) {
    armed = bytes;
    if (stopping.load()) return false;
    if (espusb_device_edpt_xfer(port, ep, buffer, uint16_t(bytes), false)) return true;
    needReset(); return false;
  }
  void commandReady() {
    stage = Stage::Command;
    if (!espusb_device_edpt_stalled(port, out)) arm(out, 31);
  }
  void needReset() {
    stage = Stage::NeedReset;
    espusb_device_edpt_stall(port, in); espusb_device_edpt_stall(port, out);
  }
  void sendStatus() {
    stage = Stage::Status;
    if (espusb_device_edpt_stalled(port, in)) return;
    putLe32(buffer, 0x53425355); putLe32(buffer + 4, tag);
    putLe32(buffer + 8, hostBytes - done); buffer[12] = status;
    stage = Stage::StatusSent; arm(in, 13);
  }
  void finishData() {
    if (hostBytes > done) espusb_device_edpt_stall(port, hostIn ? in : out);
    sendStatus();
  }
  // One command owns the single DMA buffer until data and status finish.
  // Storage callbacks run on the USB worker without a library spinlock.
  void transfer() {
    if (done == limit) { finishData(); return; }
    const uint32_t n = std::min(uint32_t(owner.config_.transferBytes), limit - done);
    if (deviceIn && blockIo) {
      if (mediumGone()) return;
      const uint32_t block = owner.blockSize_;
      const int32_t read = owner.read10(lba + done / block, done % block, buffer, n);
      if (read != int32_t(n)) { sense(3, 0x11); finishData(); return; }
    }
    stage = deviceIn ? Stage::DataIn : Stage::DataOut;
    // OUT hardware must have room for the complete final bus packet, even
    // when the SCSI payload ends partway through it (BOT case 11).
    const uint32_t busBytes = deviceIn ? n : ((n + packetSize - 1) / packetSize) * packetSize;
    arm(deviceIn ? in : out, busBytes);
  }
  void decode() {
    deviceBytes = 0; deviceIn = true; blockIo = false;
    // CBW was copied before this shared DMA region becomes response storage.
    memset(buffer, 0, 36);
    const auto *c = command;
    commandMedia = owner.media_.load();
    // An unreported insertion fails the next command once (SPC UNIT ATTENTION),
    // except INQUIRY and REQUEST SENSE, which must not lose it.
    const uint32_t insertions = owner.insertions_.load();
    if (insertions != insertionsSeen && c[0] != 0x12 && c[0] != 0x03) {
      insertionsSeen = insertions; sense(6, 0x28); return;
    }
    switch (c[0]) {
    case 0x00: ready(); break; // TEST UNIT READY
    case 0x12: // INQUIRY: standard page only
      if ((c[1] & 3) || c[2]) { sense(5, 0x24); break; }
      buffer[1] = 0x80; buffer[2] = 2; buffer[3] = 2; buffer[4] = 31;
      owner.inquiry(buffer + 8, buffer + 16, buffer + 32);
      deviceBytes = std::min(uint32_t(be16(c + 3)), uint32_t(36)); break;
    case 0x03: // REQUEST SENSE, fixed format
      if (c[1] & 1) { sense(5, 0x24); break; }
      buffer[0] = 0x70; buffer[2] = key; buffer[7] = 10; buffer[12] = asc; buffer[13] = ascq;
      deviceBytes = std::min(uint32_t(c[4]), uint32_t(18)); key = asc = ascq = 0; break;
    case 0x25: // READ CAPACITY(10)
      if (!ready()) break;
      putBe32(buffer, owner.blockCount_ - 1); putBe32(buffer + 4, owner.blockSize_);
      deviceBytes = 8; break;
    case 0x23: // READ FORMAT CAPACITIES
      if (!ready()) break;
      buffer[3] = 8; putBe32(buffer + 4, owner.blockCount_); putBe32(buffer + 8, owner.blockSize_); buffer[8] = 2;
      deviceBytes = std::min(uint32_t(be16(c + 7)), uint32_t(12)); break;
    case 0x1a: case 0x5a: // MODE SENSE headers, no optional mode pages
      if (!ready()) break;
      if ((c[2] & 0x3f) != 0x3f && (c[2] & 0x3f)) { sense(5, 0x24); break; }
      if (c[0] == 0x1a) { buffer[0] = 3; buffer[2] = owner.writable() ? 0 : 0x80; deviceBytes = std::min(uint32_t(c[4]), uint32_t(4)); }
      else { buffer[1] = 6; buffer[3] = owner.writable() ? 0 : 0x80; deviceBytes = std::min(uint32_t(be16(c + 7)), uint32_t(8)); }
      break;
    case 0x1b:
      if (!owner.startStop(c[4] >> 4, c[4] & 1, c[4] & 2)) sense(5, 0x24);
      break;
    case 0x1e: case 0x35: break; // PREVENT/ALLOW, SYNCHRONIZE CACHE: synchronous storage
    case 0x2f:
      if (c[1] & 6) sense(5, 0x24); else ready(); // VERIFY without comparison data
      break;
    case 0x28: case 0x2a: {
      if (!ready()) break;
      deviceIn = c[0] == 0x28; blockIo = true; lba = be32(c + 2);
      const uint32_t blocks = be16(c + 7);
      if (lba > owner.blockCount_ || blocks > owner.blockCount_ - lba) { sense(5, 0x21); break; }
      if (!deviceIn && !owner.writable()) { sense(7, 0x27); break; }
      deviceBytes = blocks * uint32_t(owner.blockSize_); break;
    }
    default: sense(5, 0x20); break;
    }
  }
  void receivedCommand(uint32_t bytes) {
    if (bytes != 31 || le32(buffer) != 0x43425355 || (buffer[12] & 0x7f) || (buffer[13] & 0xf0) || !buffer[14] || buffer[14] > 16) { needReset(); return; }
    tag = le32(buffer + 4); hostBytes = le32(buffer + 8); hostIn = buffer[12] & 0x80;
    const uint8_t lun = buffer[13], cdbBytes = buffer[14];
    memcpy(command, buffer + 15, 16); status = 0; done = 0;
    // All currently supported CDBs are 6/10 bytes; never read beyond the length.
    const uint8_t needed = command[0] < 0x20 ? 6 : 10;
    if (lun || cdbBytes < needed) { deviceBytes = 0; sense(5, lun ? 0x25 : 0x24); }
    else decode();
    limit = std::min(hostBytes, deviceBytes);
    // BOT's thirteen cases: transfer only the common, direction-compatible
    // portion; mismatched direction or a short host expectation is phase error.
    if ((!hostBytes && deviceBytes) || (hostBytes && deviceBytes && (hostIn != deviceIn || hostBytes < deviceBytes))) status = 2;
    if (!hostBytes || !deviceBytes || hostIn != deviceIn) limit = 0;
    transfer();
  }
  static void finishRecovery(void *context) {
    auto *s = static_cast<EspUsbMscState *>(context);
    s->resetFence = false;
    if (s->fenceEpoch != s->resetEpoch) {
      espusb::internal::postUsbRuntimeJob(s->recoveryJob); return;
    }
    if (s->stopping.load() || s->stage != Stage::Recovery) return;
    if (s->interfaceReset) {
      const bool haltIn = espusb_device_edpt_stalled(s->port, s->in), haltOut = espusb_device_edpt_stalled(s->port, s->out);
      espusb_device_edpt_clear_stall(s->port, s->in); espusb_device_edpt_clear_stall(s->port, s->out);
      // SET_INTERFACE resets endpoint toggles, but must not bypass the
      // outstanding CLEAR_FEATURE steps of an ongoing BOT Reset Recovery.
      if (haltIn && !s->clearIn) espusb_device_edpt_stall(s->port, s->in);
      if (haltOut && !s->clearOut) espusb_device_edpt_stall(s->port, s->out);
      s->interfaceReset = false;
    }
    if (s->needsBotReset) s->needReset();
    else if (s->clearIn && s->clearOut) s->commandReady();
  }
  static void recover(void *context) {
    auto *s = static_cast<EspUsbMscState *>(context);
    if (!s->stopping.load() && s->stage == Stage::Recovery && !s->resetFence) {
      s->fenceEpoch = s->resetEpoch;
      s->resetFence = espusb_device_try_defer(finishRecovery, s);
      if (!s->resetFence) espusb::internal::postUsbRuntimeJob(s->recoveryJob);
    }
  }
  static void init() {}
  static void resetAll(uint8_t) {
    for (auto *s = first; s; s = s->next) {
      ++s->resetEpoch;
      s->in = s->out = 0; s->interface = 255; s->stage = Stage::Command;
      s->key = s->asc = s->ascq = 0; s->forgetInsertions();
      s->interfaceReset = s->needsBotReset = s->clearIn = s->clearOut = false;
    }
  }
  static bool deinit() { resetAll(0); return true; }
  static uint16_t open(uint8_t port, const EspUsbInterfaceDescriptor *itf, uint16_t bytes) {
    if (bytes < 23 || itf->bLength != 9 || itf->bInterfaceClass != 8 || itf->bInterfaceSubClass != 6 || itf->bInterfaceProtocol != 0x50 || itf->bNumEndpoints != 2) return 0;
    auto *s = first;
    while (s && s->owner.interfaceNumber_ != itf->bInterfaceNumber) s = s->next;
    if (!s) return 0;
    uint8_t in = 0, out = 0; uint16_t packetSize = 0;
    for (unsigned i = 0; i < 2; ++i) {
      const auto *ep = reinterpret_cast<const EspUsbEndpointDescriptor *>(reinterpret_cast<const uint8_t *>(itf) + 9 + 7 * i);
      if (ep->bLength != 7 || ep->bDescriptorType != 5 || espusb_endpoint_type(ep) != ESPUSB_XFER_BULK || !espusb_endpoint_packet_size(ep) || espusb_endpoint_packet_size(ep) > s->owner.config_.transferBytes || !espusb_device_edpt_open(port, ep)) return 0;
      if (packetSize && packetSize != espusb_endpoint_packet_size(ep)) return 0;
      packetSize = espusb_endpoint_packet_size(ep);
      if (ep->bEndpointAddress & 0x80) in = ep->bEndpointAddress; else out = ep->bEndpointAddress;
    }
    if (!in || !out) return 0;
    s->port = port; s->interface = itf->bInterfaceNumber; s->in = in; s->out = out; s->packetSize = packetSize;
    s->forgetInsertions(); s->commandReady(); return 23;
  }
  static bool control(uint8_t port, uint8_t phase, const EspUsbControlRequest *r) {
    auto *s = first;
    if (r->bmRequestType == 2) {
      while (s && s->in != r->wIndex && s->out != r->wIndex) s = s->next;
      if (!s || r->bRequest != 1 || r->wValue || r->wLength) return false;
      if (phase != ESPUSB_CONTROL_STAGE_SETUP) return true;
      if (s->stage == Stage::NeedReset || s->needsBotReset) espusb_device_edpt_stall(port, uint8_t(r->wIndex));
      else if (s->stage == Stage::Recovery) {
        (r->wIndex == s->in ? s->clearIn : s->clearOut) = true;
        if (s->clearIn && s->clearOut && !s->resetFence && !s->recoveryJob.queued) s->commandReady();
      } else if (r->wIndex == s->in && s->stage == Stage::Status) s->sendStatus();
      else if (r->wIndex == s->out && s->stage == Stage::Command && !espusb_device_edpt_busy(port, s->out)) s->commandReady();
      return true;
    }
    while (s && s->interface != r->wIndex) s = s->next;
    if (!s || r->wValue) return false;
    if (r->bmRequestType == 1 && r->bRequest == 11 && !r->wLength) {
      if (phase != ESPUSB_CONTROL_STAGE_SETUP) return true;
      const bool input = espusb_device_edpt_abort(port, s->in);
      const bool output = espusb_device_edpt_abort(port, s->out);
      if (!input || !output) return espusb::internal::rejectUsbControl();
      // A new interface selection cancels normal transport, but it cannot
      // replace BOT Reset Recovery after an invalid CBW or a phase error.
      s->needsBotReset |= s->stage == Stage::NeedReset;
      if (s->stage != Stage::Recovery) s->clearIn = s->clearOut = true;
      ++s->resetEpoch; s->interfaceReset = true; s->stage = Stage::Recovery;
      if (!espusb::internal::postUsbRuntimeJob(s->recoveryJob)) return espusb::internal::rejectUsbControl();
      return espusb_device_control_status(port, r);
    }
    if (r->bmRequestType == 0xa1 && r->bRequest == 254 && r->wLength == 1)
      return phase != ESPUSB_CONTROL_STAGE_SETUP || espusb_device_control_transfer(port, r, &s->maxLun, 1);
    if (r->bmRequestType == 0x21 && r->bRequest == 255 && !r->wLength) {
      if (phase != ESPUSB_CONTROL_STAGE_SETUP) return true;
      if (!espusb_device_edpt_abort(port, s->in) || !espusb_device_edpt_abort(port, s->out)) return false;
      ++s->resetEpoch;
      // Transport only: the sense of the last failure and an untold medium
      // change survive, as SCSI state; a USB reset or end() clears them.
      s->stage = Stage::Recovery; s->clearIn = s->clearOut = s->needsBotReset = false;
      espusb::internal::postUsbRuntimeJob(s->recoveryJob);
      return espusb_device_control_status(port, r);
    }
    return false;
  }
  static bool complete(uint8_t, uint8_t ep, EspUsbControllerResult result, uint32_t bytes) {
    auto *s = first; while (s && ep != s->in && ep != s->out) s = s->next;
    if (!s) return false;
    if (s->stopping.load() || s->stage == Stage::Recovery || s->stage == Stage::NeedReset) return true;
    if (result != ESPUSB_TRANSFER_SUCCESS) { s->needReset(); return true; }
    if (s->stage == Stage::Command && ep == s->out) s->receivedCommand(bytes);
    else if (s->stage == Stage::StatusSent && ep == s->in) {
      if (s->status == 2) s->needReset(); else s->commandReady();
    } else if ((s->stage == Stage::DataIn && ep == s->in) || (s->stage == Stage::DataOut && ep == s->out)) {
      const uint32_t wanted = std::min(uint32_t(s->owner.config_.transferBytes), s->limit - s->done);
      if (bytes < wanted || bytes > s->armed || (s->deviceIn && bytes != wanted)) {
        s->status = 2; s->finishData(); return true;
      }
      // Bytes beyond the device payload are transport padding/excess, not
      // storage data. CSW residue counts processed bytes only.
      bytes = wanted;
      if (!s->deviceIn && s->blockIo) {
        const uint32_t block = s->owner.blockSize_;
        if (s->status != 2 && s->mediumGone()) return true;
        if (s->status != 2 && s->owner.write10(s->lba + s->done / block, s->done % block, s->buffer, bytes) != int32_t(bytes)) {
          s->sense(3, 0x0c); s->finishData(); return true;
        }
      }
      s->done += bytes;
      if (s->blockIo || s->done == s->limit) s->transfer();
      else { s->status = 2; s->finishData(); }
    }
    return true;
  }
};
EspUsbMscState *EspUsbMscState::first = nullptr;
namespace {
const espusb::internal::UsbClassDriver &mscDriver() {
  static const espusb::internal::UsbClassDriver driver = {
    .init = EspUsbMscState::init, .deinit = EspUsbMscState::deinit,
    .reset = EspUsbMscState::resetAll, .open = EspUsbMscState::open,
    .control_xfer_cb = EspUsbMscState::control, .xfer_cb = EspUsbMscState::complete, .sof = nullptr
  };
  return driver;
}
}
EspUsbDeviceMsc::EspUsbDeviceMsc(EspUsbDevice &d) : EspUsbDeviceClass(d) {}
EspUsbDeviceMsc::~EspUsbDeviceMsc() { end(); }
esp_err_t EspUsbDeviceMsc::configure(const EspUsbMscConfig &c) {
  if (state_) return ESP_ERR_INVALID_STATE;
  if (!c.transferBytes || c.transferBytes > UINT16_MAX || c.transferBytes % 64) return ESP_ERR_INVALID_SIZE;
  if (c.transferBuffer && (reinterpret_cast<uintptr_t>(c.transferBuffer) % 64 || !esp_ptr_internal(c.transferBuffer) || !esp_ptr_dma_capable(c.transferBuffer) || reinterpret_cast<uintptr_t>(c.transferBuffer) > UINTPTR_MAX - c.transferBytes)) return ESP_ERR_INVALID_ARG;
  config_ = c; return ESP_OK;
}
bool EspUsbDeviceMsc::begin(uint32_t count, uint16_t size) {
  if (state_) { startError_ = ESP_ERR_INVALID_STATE; return false; }
  if (!count || !size) { startError_ = ESP_ERR_INVALID_ARG; return false; }
  blockCount_ = count; blockSize_ = size;
  startError_ = readCallback_ ? ESP_OK : ESP_ERR_INVALID_ARG;
  return startError_ == ESP_OK;
}
bool EspUsbDeviceMsc::begin() {
  if (state_ || !registered_ || espusb::internal::usbRuntimeStarted()) { startError_ = ESP_ERR_INVALID_STATE; return false; }
  if (!blockCount_ || !blockSize_ || !readCallback_) { startError_ = ESP_ERR_INVALID_ARG; return false; }
  startError_ = configure(config_); if (startError_ != ESP_OK) return false;
  size_t mps = 64;
#if defined(CONFIG_IDF_TARGET_ESP32P4) || defined(CONFIG_IDF_TARGET_ESP32S31)
  if (device_.config().controller != EspUsbController::FullSpeed) mps = 512;
#endif
  if (config_.transferBytes % mps) { startError_ = ESP_ERR_INVALID_SIZE; return false; }
  auto *s = espusb::internal::newUsbObject<EspUsbMscState>(*this);
  if (s) {
    s->buffer = config_.transferBuffer;
    if (!s->buffer) { s->owned = true; s->buffer = static_cast<uint8_t *>(espusb::internal::allocateUsbMemory(config_.transferBytes, 64, true)); }
  }
  if (!s || !s->buffer || (!EspUsbMscState::first && !espUsbDeviceRegisterAppDrivers(&mscDriver(), 1))) {
    espusb::internal::deleteUsbObject(s); startError_ = ESP_ERR_NO_MEM; return false;
  }
  portENTER_CRITICAL(&mscLock);
  s->next = EspUsbMscState::first; EspUsbMscState::first = s; state_ = s;
  portEXIT_CRITICAL(&mscLock);
  startError_ = ESP_OK; return true;
}
void EspUsbDeviceMsc::prepareToStop() { if (state_) state_->stopping.store(true); }
void EspUsbDeviceMsc::end() {
  // Device also calls end after its worker and DMA have stopped.
  if (state_ && device_.usbStarted_) { device_.end(); return; }
  portENTER_CRITICAL(&mscLock);
  auto *s = state_; state_ = nullptr;
  auto **link = &EspUsbMscState::first;
  while (*link && *link != s) link = &(*link)->next;
  if (*link) *link = s->next;
  const bool empty = !EspUsbMscState::first;
  portEXIT_CRITICAL(&mscLock);
  espusb::internal::deleteUsbObject(s);
  if (empty) espUsbDeviceUnregisterAppDrivers(&mscDriver(), 1);
}
size_t EspUsbDeviceMsc::transferCapacity() const {
  portENTER_CRITICAL(&mscLock); size_t n = state_ ? config_.transferBytes : 0; portEXIT_CRITICAL(&mscLock); return n;
}
bool EspUsbDeviceMsc::mounted() const {
  portENTER_CRITICAL(&mscLock); bool value = state_ && !state_->stopping.load() && espusb_device_ready(); portEXIT_CRITICAL(&mscLock); return value;
}

uint16_t EspUsbDeviceMsc::configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize)
{
  if (endpointNumber == 0 || endpointNumber > 15)
  {
    return 0;
  }
  if (!dst) return 23;
  interfaceNumber_ = interfaceNumber;
  const uint8_t epOut = endpointNumber;
  const uint8_t epIn = static_cast<uint8_t>(0x80 | endpointNumber);
  const uint8_t descriptor[] = {
      9, 4, interfaceNumber, 0, 2, 8, 6, 0x50, stringIndex_,
      7, 5, epOut, 2, uint8_t(endpointSize), uint8_t(endpointSize >> 8), 0,
      7, 5, epIn, 2, uint8_t(endpointSize), uint8_t(endpointSize >> 8), 0,
  };
  memcpy(dst, descriptor, sizeof(descriptor));
  return sizeof(descriptor);
}

esp_err_t EspUsbDeviceMsc::vendorID(const char *value)
{
  if (state_) return ESP_ERR_INVALID_STATE;
  copyPadded(reinterpret_cast<uint8_t *>(vendor_), sizeof(vendor_) - 1, value);
  vendor_[sizeof(vendor_) - 1] = '\0';
  return ESP_OK;
}

esp_err_t EspUsbDeviceMsc::productID(const char *value)
{
  if (state_) return ESP_ERR_INVALID_STATE;
  copyPadded(reinterpret_cast<uint8_t *>(product_), sizeof(product_) - 1, value);
  product_[sizeof(product_) - 1] = '\0';
  return ESP_OK;
}

esp_err_t EspUsbDeviceMsc::productRevision(const char *value)
{
  if (state_) return ESP_ERR_INVALID_STATE;
  copyPadded(reinterpret_cast<uint8_t *>(revision_), sizeof(revision_) - 1, value);
  revision_[sizeof(revision_) - 1] = '\0';
  return ESP_OK;
}

void EspUsbDeviceMsc::mediaPresent(bool value)
{
  uint32_t media = media_.load();
  do {
    if (bool(media & 1) == value) return;
  } while (!media_.compare_exchange_weak(media, value ? media | 1 : (media + 2) & ~uint32_t(1)));
  if (value) ++insertions_;
}

void EspUsbDeviceMsc::isWritable(bool value)
{
  writable_ = value;
}

esp_err_t EspUsbDeviceMsc::onRead(EspUsbDeviceMscReadCallback callback)
{
  if (state_) return ESP_ERR_INVALID_STATE;
  readCallback_ = std::move(callback);
  return ESP_OK;
}

esp_err_t EspUsbDeviceMsc::onWrite(EspUsbDeviceMscWriteCallback callback)
{
  if (state_) return ESP_ERR_INVALID_STATE;
  writeCallback_ = std::move(callback);
  return ESP_OK;
}

esp_err_t EspUsbDeviceMsc::onStartStop(EspUsbDeviceMscStartStopCallback callback)
{
  if (state_) return ESP_ERR_INVALID_STATE;
  startStopCallback_ = std::move(callback);
  return ESP_OK;
}

uint8_t EspUsbDeviceMsc::maxLun() const
{
  return 0;
}

void EspUsbDeviceMsc::inquiry(uint8_t vendor[8], uint8_t product[16], uint8_t revision[4]) const
{
  copyPadded(vendor, 8, vendor_);
  copyPadded(product, 16, product_);
  copyPadded(revision, 4, revision_);
}

bool EspUsbDeviceMsc::testUnitReady() const
{
  return media_.load() & 1;
}

void EspUsbDeviceMsc::capacity(uint32_t *blockCount, uint16_t *blockSize) const
{
  if (!testUnitReady())
  {
    *blockCount = 0;
    *blockSize = 0;
    return;
  }
  *blockCount = blockCount_;
  *blockSize = blockSize_;
}

bool EspUsbDeviceMsc::startStop(uint8_t powerCondition, bool start, bool loadEject)
{
  return startStopCallback_ ? startStopCallback_(powerCondition, start, loadEject) : true;
}

int32_t EspUsbDeviceMsc::read10(uint32_t lba, uint32_t offset, void *buffer, uint32_t size)
{
  if (!testUnitReady() || !readCallback_)
  {
    return -1;
  }
  return readCallback_(lba, offset, buffer, size);
}

int32_t EspUsbDeviceMsc::write10(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size)
{
  if (!testUnitReady() || !writable_ || !writeCallback_)
  {
    return -1;
  }
  return writeCallback_(lba, offset, buffer, size);
}

bool EspUsbDeviceMsc::writable() const
{
  return writable_ && bool(writeCallback_);
}

void EspUsbDeviceMsc::copyPadded(uint8_t *dst, size_t size, const char *value)
{
  memset(dst, ' ', size);
  if (!value)
  {
    return;
  }
  size_t length = strlen(value);
  if (length > size)
  {
    length = size;
  }
  memcpy(dst, value, length);
}

