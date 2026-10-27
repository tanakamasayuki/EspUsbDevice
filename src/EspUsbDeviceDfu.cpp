#include "internal/EspUsbDeviceEndpoint.h"
#include "EspUsbDevice.h"
#include "internal/EspUsbDeviceAppDriver.h"
#include "internal/EspUsbDeviceRestart.h"
#include "internal/EspUsbMemory.h"
#include "internal/EspUsbTinyUsbRuntime.h"
#include "device/usbd.h"
#include <string.h>
#include <utility>
namespace {
using Status = EspUsbDfuStatus;
enum DfuState : uint8_t { AppIdle, AppDetach, Idle, DownloadSync, DownloadBusy,
  DownloadIdle, ManifestSync, Manifest, WaitReset, UploadIdle, Error };
const espusb::internal::UsbClassDriver &dfuDriver();
EspUsbDeviceDfu *otaOwner = nullptr;
}
struct EspUsbDfuState {
  static EspUsbDfuState *first;
  EspUsbDfuState *next = nullptr;
  EspUsbDeviceDfu &owner;
  uint8_t *buffer = nullptr;
  bool owned = false, receiving = false, dataReady = false, workPending = false;
  std::atomic<bool> stopping{false};
  uint8_t interface = 255, state = Idle, statusNext = Idle, reply[6] = {};
  Status status = Status::Ok;
  uint16_t block = 0, bytes = 0, nextBlock = 0;
  TickType_t detachAt = 0;
  explicit EspUsbDfuState(EspUsbDeviceDfu &o) : owner(o) { reset(); }
  ~EspUsbDfuState() { if (owned) espusb::internal::freeUsbMemory(buffer); }
  void reset() {
    owner.handleAbort(); owner.written_.store(0);
    state = owner.mode_ == EspUsbDeviceDfuMode::Runtime ? AppIdle : Idle;
    status = Status::Ok; nextBlock = 0;
    receiving = dataReady = workPending = false;
  }
  bool reject(Status reason = Status::StalledPacket) {
    if (state >= Idle && state != WaitReset && state != Error) {
      status = owner.failDownload(reason); state = Error;
      receiving = dataReady = workPending = false;
    }
    return false;
  }
  void finished(Status result, bool manifesting) {
    workPending = false;
    if (result != Status::Ok) { status = result; state = Error; return; }
    if (manifesting) state = owner.config_.manifestationTolerant ? ManifestSync : WaitReset;
    else { ++nextBlock; state = DownloadSync; }
  }
  void expireDetach() {
    if (state == AppDetach && int32_t(xTaskGetTickCount() - detachAt) >= 0) state = AppIdle;
  }
  static void init() {}
  static void resetAll(uint8_t) {
    for (auto *s = first; s; s = s->next) { s->reset(); s->interface = 255; }
  }
  static bool deinit() { resetAll(0); return true; }
  static uint16_t open(uint8_t, const tusb_desc_interface_t *itf, uint16_t size) {
    if (size < 18 || itf->bLength != 9 || itf->bInterfaceClass != 0xfe ||
        itf->bInterfaceSubClass != 1 || itf->bNumEndpoints || itf->bAlternateSetting) return 0;
    auto *s = first;
    while (s && (s->owner.interface_ != itf->bInterfaceNumber || !s->owner.device_.config().startTinyUsb)) s = s->next;
    if (!s || itf->bInterfaceProtocol != (s->owner.mode_ == EspUsbDeviceDfuMode::Runtime ? 1 : 2)) return 0;
    const auto *f = reinterpret_cast<const uint8_t *>(itf) + 9;
    if (f[0] != 9 || f[1] != 0x21 || (f[5] | uint16_t(f[6]) << 8) != s->owner.transferSize()) return 0;
    s->interface = itf->bInterfaceNumber; return 18;
  }
  static bool control(uint8_t port, uint8_t stage, const tusb_control_request_t *r) {
    auto *s = first;
    while (s && s->interface != r->wIndex) s = s->next;
    if (!s || s->stopping.load()) return false;
    auto &o = s->owner;
    s->expireDetach();
    if (stage == CONTROL_STAGE_SETUP) { s->receiving = false; s->dataReady = false; }
    if (r->bmRequestType == 0x81 && r->bRequest == 10 && !r->wValue && r->wLength == 1) {
      if (stage != CONTROL_STAGE_SETUP) return true;
      s->reply[0] = 0; return tud_control_xfer(port, r, s->reply, 1);
    }
    if (r->bmRequestType == 1 && r->bRequest == 11 && !r->wValue && !r->wLength) {
      if (stage == CONTROL_STAGE_SETUP) return tud_control_status(port, r);
      if (stage == CONTROL_STAGE_ACK) s->reset();
      return true;
    }
    if (r->bmRequestType != 0x21 && r->bmRequestType != 0xa1) return false;
    if (s->state == WaitReset) return false;
    if (r->bRequest == 3 && r->bmRequestType == 0xa1 && !r->wValue && r->wLength == 6) {
      if (s->state == DownloadBusy || s->state == Manifest) return s->reject();
      if (stage == CONTROL_STAGE_SETUP) {
        uint32_t poll = 0; s->statusNext = s->state;
        if (s->state == DownloadSync) {
          s->statusNext = s->workPending ? DownloadBusy : DownloadIdle;
          if (s->workPending) poll = o.config_.downloadPollMs;
        } else if (s->state == ManifestSync) {
          s->statusNext = s->workPending ? Manifest : Idle;
          if (s->workPending) poll = o.config_.manifestPollMs;
        }
        s->reply[0] = uint8_t(s->status);
        for (unsigned i = 0; i < 3; ++i) s->reply[i + 1] = uint8_t(poll >> (8 * i));
        s->reply[4] = s->statusNext; s->reply[5] = 0;
        return tud_control_xfer(port, r, s->reply, 6);
      }
      if (stage == CONTROL_STAGE_ACK) {
        s->state = s->statusNext;
        if (s->state == DownloadBusy) s->finished(o.handleDownload(s->block, s->buffer, s->bytes), false);
        else if (s->state == Manifest) s->finished(o.handleManifest(), true);
        else if (s->state == Idle) s->nextBlock = 0;
      }
      return true;
    }
    if (r->bRequest == 5 && r->bmRequestType == 0xa1 && !r->wValue && r->wLength == 1) {
      if (s->state == DownloadBusy || s->state == Manifest) return s->reject();
      if (stage != CONTROL_STAGE_SETUP) return true;
      s->reply[0] = s->state; return tud_control_xfer(port, r, s->reply, 1);
    }
    if (o.mode_ == EspUsbDeviceDfuMode::Runtime) {
      if (r->bRequest != 0 || r->bmRequestType != 0x21 || r->wLength || s->state != AppIdle) return false;
      if (stage == CONTROL_STAGE_SETUP) return tud_control_status(port, r);
      if (stage == CONTROL_STAGE_ACK) {
        s->state = AppDetach;
        const uint16_t ms = r->wValue < o.config_.detachTimeoutMs ? r->wValue : o.config_.detachTimeoutMs;
        s->detachAt = xTaskGetTickCount() + pdMS_TO_TICKS(ms);
        o.handleDetach();
      }
      return true;
    }
    if (r->bRequest == 4 && r->bmRequestType == 0x21 && !r->wValue && !r->wLength && s->state == Error) {
      if (stage == CONTROL_STAGE_SETUP) return tud_control_status(port, r);
      if (stage == CONTROL_STAGE_ACK) s->reset(); return true;
    }
    if (r->bRequest == 6 && r->bmRequestType == 0x21 && !r->wValue && !r->wLength &&
        (s->state == Idle || s->state == DownloadSync || s->state == DownloadIdle || s->state == ManifestSync)) {
      if (stage == CONTROL_STAGE_SETUP) return tud_control_status(port, r);
      if (stage == CONTROL_STAGE_ACK) s->reset(); return true;
    }
    if (r->bRequest != 1 || r->bmRequestType != 0x21 ||
        (s->state != Idle && s->state != DownloadIdle) || r->wLength > o.transferSize()) return s->reject();
    if (r->wValue != s->nextBlock) return s->reject(Status::NotDone);
    if (!r->wLength) {
      if (s->state != DownloadIdle) return s->reject(Status::NotDone);
      if (stage == CONTROL_STAGE_SETUP) return tud_control_status(port, r);
      if (stage == CONTROL_STAGE_ACK) { s->state = ManifestSync; s->workPending = true; }
      return true;
    }
    if (stage == CONTROL_STAGE_SETUP) {
      s->receiving = true; s->block = r->wValue; s->bytes = r->wLength;
      return tud_control_xfer(port, r, s->buffer, s->bytes);
    }
    if (stage == CONTROL_STAGE_DATA) {
      if (!s->receiving || usbd_control_received_bytes() != s->bytes) return s->reject();
      s->dataReady = true;
    } else if (stage == CONTROL_STAGE_ACK) {
      if (!s->receiving || !s->dataReady) return s->reject();
      if (s->state == Idle) o.written_.store(0);
      s->receiving = false; s->state = DownloadSync; s->workPending = true;
    }
    return true;
  }
};
EspUsbDfuState *EspUsbDfuState::first = nullptr;
namespace {
const espusb::internal::UsbClassDriver &dfuDriver() {
  static const espusb::internal::UsbClassDriver driver = {
    .init = EspUsbDfuState::init, .deinit = EspUsbDfuState::deinit,
    .reset = EspUsbDfuState::resetAll, .open = EspUsbDfuState::open,
    .control_xfer_cb = EspUsbDfuState::control, .xfer_cb = nullptr, .sof = nullptr
  };
  return driver;
}
}
EspUsbDeviceDfu::EspUsbDeviceDfu(EspUsbDevice &d, EspUsbDeviceDfuMode mode, const char *name)
    : EspUsbDeviceClass(d), mode_(mode), name_(name) {}
EspUsbDeviceDfu::~EspUsbDeviceDfu() { end(); }
esp_err_t EspUsbDeviceDfu::configure(const EspUsbDfuConfig &c) {
  if (state_) return ESP_ERR_INVALID_STATE;
  if (c.transferBytes < 64 || c.transferBytes > UINT16_MAX || c.downloadPollMs > 0xffffff || c.manifestPollMs > 0xffffff) return ESP_ERR_INVALID_SIZE;
  if (c.transferBuffer && reinterpret_cast<uintptr_t>(c.transferBuffer) > UINTPTR_MAX - c.transferBytes) return ESP_ERR_INVALID_ARG;
  config_ = c; return ESP_OK;
}
esp_err_t EspUsbDeviceDfu::setBackend(const EspUsbDfuBackend &b) {
  if (state_) return ESP_ERR_INVALID_STATE;
  if (bool(b.download) != bool(b.manifest) || (!b.download && b.abort)) return ESP_ERR_INVALID_ARG;
  if (mode_ == EspUsbDeviceDfuMode::Runtime && b.download) return ESP_ERR_INVALID_ARG;
  backend_ = b; return ESP_OK;
}
bool EspUsbDeviceDfu::begin() {
  if (state_ || !registered_ || espusb::internal::tinyUsbRuntimeStarted()) { startError_ = ESP_ERR_INVALID_STATE; return false; }
  startError_ = configure(config_); if (startError_ != ESP_OK) return false;
  auto *s = espusb::internal::newUsbObject<EspUsbDfuState>(*this);
  if (s && mode_ == EspUsbDeviceDfuMode::Download) {
    s->owned = !config_.transferBuffer;
    s->buffer = config_.transferBuffer ? config_.transferBuffer : static_cast<uint8_t *>(espusb::internal::allocateUsbMemory(config_.transferBytes));
  }
  if (!s || (mode_ == EspUsbDeviceDfuMode::Download && !s->buffer) ||
      (!EspUsbDfuState::first && !espUsbDeviceRegisterAppDrivers(&dfuDriver(), 1))) {
    espusb::internal::deleteUsbObject(s); startError_ = ESP_ERR_NO_MEM; return false;
  }
  s->next = EspUsbDfuState::first; EspUsbDfuState::first = s; state_ = s;
  startError_ = ESP_OK; return true;
}
void EspUsbDeviceDfu::prepareToStop() { if (state_) state_->stopping.store(true); }
void EspUsbDeviceDfu::end() {
  if (state_ && device_.tinyusbStarted_) { device_.end(); return; }
  if (!state_) return;
  handleAbort();
  auto **link = &EspUsbDfuState::first;
  while (*link && *link != state_) link = &(*link)->next;
  if (*link) *link = state_->next;
  espusb::internal::deleteUsbObject(state_); state_ = nullptr; written_.store(0);
  if (!EspUsbDfuState::first) espUsbDeviceUnregisterAppDrivers(&dfuDriver(), 1);
}
void EspUsbDeviceDfu::assignFunctionIds(uint8_t, uint8_t stringIndex) { stringIndex_ = stringIndex; }
uint16_t EspUsbDeviceDfu::configurationDescriptor(uint8_t *dst, uint8_t itf, uint8_t, uint16_t) {
  if (state_) { device_.setLastError(ESP_ERR_INVALID_STATE); return 0; }
  interface_ = itf;
  const uint16_t n = transferSize(), timeout = config_.detachTimeoutMs;
  const uint8_t attrs = 1 | (mode_ == EspUsbDeviceDfuMode::Runtime ? 8 : config_.manifestationTolerant ? 4 : 0);
  const uint8_t data[] = {9,4,itf,0,0,0xfe,1,uint8_t(mode_ == EspUsbDeviceDfuMode::Runtime ? 1 : 2),stringIndex_,
    9,0x21,attrs,uint8_t(timeout),uint8_t(timeout >> 8),uint8_t(n),uint8_t(n >> 8),0x10,1};
  if (dst) memcpy(dst, data, sizeof(data)); return sizeof(data);
}
size_t EspUsbDeviceDfu::transferCapacity() const { return state_ && state_->buffer ? config_.transferBytes : 0; }
esp_err_t EspUsbDeviceDfu::onDetach(DetachCallback c) { if (state_) return ESP_ERR_INVALID_STATE; detachCallback_ = std::move(c); return ESP_OK; }
esp_err_t EspUsbDeviceDfu::onProgress(ProgressCallback c) { if (state_) return ESP_ERR_INVALID_STATE; progressCallback_ = std::move(c); return ESP_OK; }
esp_err_t EspUsbDeviceDfu::onComplete(CompleteCallback c) { if (state_) return ESP_ERR_INVALID_STATE; completeCallback_ = std::move(c); return ESP_OK; }
esp_err_t EspUsbDeviceDfu::onError(ErrorCallback c) { if (state_) return ESP_ERR_INVALID_STATE; errorCallback_ = std::move(c); return ESP_OK; }
esp_err_t EspUsbDeviceDfu::restartWhenComplete(bool enable) { if (state_) return ESP_ERR_INVALID_STATE; restartWhenComplete_ = enable; return ESP_OK; }
void EspUsbDeviceDfu::handleDetach() {
  if (detachCallback_) detachCallback_();
  else espusb::internal::scheduleDeviceRestart(&device_, 100, true);
}
void EspUsbDeviceDfu::handleAbort() {
  if (downloadOpen_) {
    if (backend_.download) { if (backend_.abort) backend_.abort(); }
    else firmware_.abort();
    downloadOpen_ = false;
  }
  if (otaOwner == this) otaOwner = nullptr;
}
Status EspUsbDeviceDfu::failDownload(Status status) {
  handleAbort();
  if (uint8_t(status) > 15) status = Status::Unknown;
  if (errorCallback_) errorCallback_(status);
  return status;
}
Status EspUsbDeviceDfu::handleDownload(uint16_t block, const uint8_t *data, uint16_t bytes) {
  if (bytes > SIZE_MAX - written_.load()) return failDownload(Status::Address);
  if (backend_.download) {
    downloadOpen_ = true;
    const Status status = backend_.download(block, data, bytes);
    if (status != Status::Ok) return failDownload(status);
  } else {
    if (!downloadOpen_) {
      if (block) return failDownload(Status::NotDone);
      if (otaOwner && otaOwner != this) return failDownload(Status::Target);
      otaOwner = this;
      if (!firmware_.begin()) return failDownload(Status::Target);
      downloadOpen_ = true;
    }
    if (!firmware_.write(data, bytes)) return failDownload(Status::Write);
  }
  const size_t n = written_.fetch_add(bytes) + bytes;
  if (progressCallback_) progressCallback_(n);
  return Status::Ok;
}
Status EspUsbDeviceDfu::handleManifest() {
  if (!downloadOpen_) return failDownload(Status::NotDone);
  if (backend_.download) {
    const Status status = backend_.manifest();
    if (status != Status::Ok) return failDownload(status);
    downloadOpen_ = false; return Status::Ok;
  }
  if (!firmware_.end()) return failDownload(Status::Verify);
  downloadOpen_ = false;
  if (otaOwner == this) otaOwner = nullptr;
  if (completeCallback_ && !completeCallback_()) {
    EspUsbDeviceFirmwareUpdate::cancelPendingBoot();
    return failDownload(Status::Firmware);
  }
  if (restartWhenComplete_) espusb::internal::scheduleDeviceRestart(&device_, 500, false);
  return Status::Ok;
}
void EspUsbDeviceDfu::onBusDetached() { if (state_) state_->reset(); else handleAbort(); }
