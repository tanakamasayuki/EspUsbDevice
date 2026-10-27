#include "EspUsbDevice.h"
#include "internal/EspUsbDeviceAppDriver.h"
#include "internal/EspUsbDeviceCore.h"
#include "internal/EspUsbMemory.h"
#include "internal/EspUsbTinyUsbRuntime.h"
#include <algorithm>
#include <limits.h>
#include <new>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "internal/EspUsbDeviceEndpoint.h"
#include "internal/EspUsbDeviceBus.h"

namespace {
portMUX_TYPE bulkLock = portMUX_INITIALIZER_UNLOCKED;
struct Ring {
  uint8_t *data = nullptr;
  bool owned = false;
  size_t capacity = 0, head = 0, used = 0;
  size_t push(const uint8_t *src, size_t n) {
    n = std::min(n, capacity - used);
    if (!n) return 0;
    const size_t tail = (head + used) % capacity;
    const size_t first = std::min(n, capacity - tail);
    memcpy(data + tail, src, first); memcpy(data, src + first, n - first);
    used += n;
    return n;
  }
  void peek(uint8_t *dst, size_t n) const {
    const size_t first = std::min(n, capacity - head);
    memcpy(dst, data + head, first); memcpy(dst + first, data, n - first);
  }
  void drop(size_t n) { head = (head + n) % capacity; used -= n; }
  size_t pop(uint8_t *dst, size_t n) {
    n = std::min(n, used);
    if (n) { peek(dst, n); drop(n); }
    return n;
  }
  void clear() { head = used = 0; }
};
uint8_t *allocateDma(size_t n) {
  return static_cast<uint8_t *>(espusb::internal::allocateUsbMemory(n, 64, true));
}
extern const espusb::internal::UsbClassDriver bulkDriver;
}

// Per-Function transport over the independent core and temporary DWC2 ABI.
struct EspUsbBulkState {
  static EspUsbBulkState *first;
  EspUsbBulkState *next = nullptr;
  EspUsbDeviceBulkStream &owner;
  Ring rx, tx;
  uint8_t *rxPacket = nullptr, *txPacket = nullptr;
  uint8_t rhport = 0, epIn = 0, epOut = 0;
  uint16_t packetSize = 0, packetCapacity = 0;
  size_t packetUnit = 1;
  size_t rxPendingOffset = 0, rxPendingLength = 0;
  bool txBusy = false, rxBusy = false, flushing = false, stopping = false;
  const void *directBuffer = nullptr;
  size_t directLength = 0;
  void *directRxBuffer = nullptr;
  size_t directRxCapacity = 0;
  uint64_t txId = 0, rxId = 0;
  espusb::internal::TinyUsbRuntimeJob cancelJob{nullptr, nullptr};
  bool cancelTx = false, cancelRx = false, abortedTx = false, abortedRx = false;
  bool fenceTx = false, fenceRx = false, resetTxPid = false, resetRxPid = false;
  EspUsbTransferStatus cancelTxStatus = EspUsbTransferStatus::Cancelled;
  EspUsbTransferStatus cancelRxStatus = EspUsbTransferStatus::Cancelled;
  bool needZlp = false, sendingZlp = false, copyZlp = false, terminateCopy = false;
  EspUsbTransferResult takeDirect(EspUsbTransferStatus status, size_t actual, bool known) {
    EspUsbTransferResult result{directBuffer, directLength, actual, status, known};
    result.id = txId; txId = 0;
    directBuffer = nullptr; directLength = 0; needZlp = sendingZlp = false;
    return result;
  }

  EspUsbTransferResult takeReceive(EspUsbTransferStatus status, size_t actual, bool known) {
    EspUsbTransferResult result{directRxBuffer, directRxCapacity, actual, status, known};
    result.id = rxId; rxId = 0;
    directRxBuffer = nullptr; directRxCapacity = 0;
    return result;
  }

  explicit EspUsbBulkState(EspUsbDeviceBulkStream &o) : owner(o) {}
  ~EspUsbBulkState() {
    espusb::internal::removeTinyUsbRuntimeJob(cancelJob);
    if (rx.owned) espusb::internal::freeUsbMemory(rx.data);
    if (tx.owned) espusb::internal::freeUsbMemory(tx.data);
    espusb::internal::freeUsbMemory(rxPacket); espusb::internal::freeUsbMemory(txPacket);
  }
  bool allocate(uint16_t maxPacket) {
    packetCapacity = maxPacket;
    terminateCopy = owner.config_.streamTermination == EspUsbStreamTermination::EndWithShortPacket ||
        (owner.config_.streamTermination == EspUsbStreamTermination::FunctionDefault && owner.defaultStreamTermination());
    rx.capacity = owner.config_.rxCapacity; tx.capacity = owner.config_.txCapacity;
    rx.owned = !owner.config_.rxBuffer;
    tx.owned = !owner.config_.txBuffer;
    rx.data = owner.config_.rxBuffer ? owner.config_.rxBuffer : (rx.capacity ? static_cast<uint8_t *>(espusb::internal::allocateUsbMemory(rx.capacity)) : nullptr);
    tx.data = owner.config_.txBuffer ? owner.config_.txBuffer : (tx.capacity ? static_cast<uint8_t *>(espusb::internal::allocateUsbMemory(tx.capacity)) : nullptr);
    const bool copiedRx = owner.config_.rxMode == EspUsbBulkRxMode::Stream || owner.config_.rxMode == EspUsbBulkRxMode::Callback;
    rxPacket = copiedRx ? allocateDma(maxPacket) : nullptr;
    txPacket = tx.capacity ? allocateDma(owner.config_.transferBytes) : nullptr;
    return (!rx.capacity || rx.data) && (!copiedRx || rxPacket) && (!tx.capacity || (tx.data && txPacket));
  }
  // Endpoint operations hold the instance lock. User callbacks never do.
  void fillReceiveRing() {
    if (!rxPendingLength) return;
    const size_t n = rx.push(rxPacket + rxPendingOffset, rxPendingLength);
    rxPendingOffset += n; rxPendingLength -= n;
    if (!rxPendingLength) rxPendingOffset = 0;
  }
  void receive() {
    if (owner.config_.rxMode == EspUsbBulkRxMode::Borrowed || owner.config_.rxMode == EspUsbBulkRxMode::Disabled || stopping || cancelRx || !epOut || rxBusy ||
        (owner.config_.rxMode == EspUsbBulkRxMode::Stream && (rxPendingLength || rx.used == rx.capacity))) return;
    if (!espusb_device_edpt_claim(rhport, epOut)) return;
    rxBusy = true;
    if (!espusb_device_edpt_xfer(rhport, epOut, rxPacket, packetSize, false)) {
      rxBusy = false; espusb_device_edpt_release(rhport, epOut);
    }
  }
  void transmit() {
    if (stopping || cancelTx || !epIn || txBusy || directBuffer || !flushing || !tx.used) return;
    if (!espusb_device_edpt_claim(rhport, epIn)) return;
    // The adapter splits hardware arms internally; its call length is 16-bit.
    size_t n = std::min(tx.used, size_t(owner.config_.transferBytes));
    if (packetUnit > 1) n -= n % packetUnit;
    tx.peek(txPacket, n);
    txBusy = true;
    if (espusb_device_edpt_xfer(rhport, epIn, txPacket, uint16_t(n), false)) tx.drop(n);
    else { txBusy = false; espusb_device_edpt_release(rhport, epIn); }
  }
  void reset() {
    portENTER_CRITICAL(&bulkLock);
    const auto status = stopping ? EspUsbTransferStatus::Stopped : EspUsbTransferStatus::BusReset;
    EspUsbTransferResult result, received;
    if (cancelTx && !stopping) { abortedTx = true; cancelTxStatus = status; }
    else { result = takeDirect(status, 0, false); cancelTx = abortedTx = fenceTx = false; }
    if (cancelRx && !stopping) { abortedRx = true; cancelRxStatus = status; }
    else { received = takeReceive(status, 0, false); cancelRx = abortedRx = fenceRx = false; }
    epIn = epOut = 0; txBusy = rxBusy = flushing = false;
    resetTxPid = resetRxPid = false;
    rx.clear(); tx.clear(); copyZlp = false;
    rxPendingOffset = rxPendingLength = 0;
    portEXIT_CRITICAL(&bulkLock);
    owner.onBulkReset();
    if (result.buffer && owner.directTxCompleteCallback_) owner.directTxCompleteCallback_(result);
    if (received.buffer && owner.directRxCompleteCallback_) owner.directRxCompleteCallback_(received);
  }
  esp_err_t requestCancel(bool input, uint64_t id) {
    // Caller holds bulkLock: end cannot discard the runtime/state between
    // checking the loan and posting its allocation-free worker job.
    if (stopping || (input ? !directBuffer : !directRxBuffer)) return ESP_ERR_INVALID_STATE;
    if (id && id != (input ? txId : rxId)) return ESP_ERR_INVALID_STATE;
    if (!espusb_device_edpt_abort_supported()) return ESP_ERR_NOT_SUPPORTED;
    bool &pending = input ? cancelTx : cancelRx;
    if (pending) return ESP_OK;
    pending = true;
    (input ? cancelTxStatus : cancelRxStatus) = EspUsbTransferStatus::Cancelled;
    cancelJob.run = cancelPump; cancelJob.context = this;
    if (!espusb::internal::postTinyUsbRuntimeJob(cancelJob)) {
      pending = false;
      return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
  }
  bool changeEndpoint(bool input, bool resetPid, EspUsbTransferStatus reason) {
    // USB worker and bulkLock held. Stop DMA before acknowledging the request;
    // retain every loan and staging buffer until the old event fence retires.
    if (stopping) return false;
    bool &pending = input ? cancelTx : cancelRx;
    auto &status = input ? cancelTxStatus : cancelRxStatus;
    if (!pending || (status == EspUsbTransferStatus::Cancelled && reason == EspUsbTransferStatus::TransferError)) status = reason;
    pending = true;
    (input ? resetTxPid : resetRxPid) = resetPid;
    const uint8_t endpoint = input ? epIn : epOut;
    bool &aborted = input ? abortedTx : abortedRx;
    aborted = endpoint && espusb_device_edpt_abort(rhport, endpoint);
    if (input) { tx.clear(); flushing = copyZlp = false; }
    else { rx.clear(); rxPendingOffset = rxPendingLength = 0; }
    cancelJob.run = cancelPump; cancelJob.context = this;
    const bool posted = espusb::internal::postTinyUsbRuntimeJob(cancelJob);
    return aborted && posted;
  }
  static void cancelPump(void *context) {
    auto *s = static_cast<EspUsbBulkState *>(context);
    portENTER_CRITICAL(&bulkLock);
    if (!s->stopping) {
      if (s->cancelTx && !s->abortedTx && s->epIn) s->abortedTx = espusb_device_edpt_abort(s->rhport, s->epIn);
      if (s->cancelRx && !s->abortedRx && s->epOut) s->abortedRx = espusb_device_edpt_abort(s->rhport, s->epOut);
      // A FIFO fence runs after already-queued completion events. Keep the
      // loan blocked until then. A full event queue is retried, never dropped.
      if (s->cancelTx && s->abortedTx && !s->fenceTx) s->fenceTx = espusb_device_try_defer(finishCancelTx, s);
      if (s->cancelRx && s->abortedRx && !s->fenceRx) s->fenceRx = espusb_device_try_defer(finishCancelRx, s);
      if ((s->cancelTx && !s->fenceTx) || (s->cancelRx && !s->fenceRx))
        espusb::internal::postTinyUsbRuntimeJob(s->cancelJob);
    }
    portEXIT_CRITICAL(&bulkLock);
  }
  static void finishCancelTx(void *context) { finishCancel(static_cast<EspUsbBulkState *>(context), true); }
  static void finishCancelRx(void *context) { finishCancel(static_cast<EspUsbBulkState *>(context), false); }
  static void finishCancel(EspUsbBulkState *s, bool input) {
    EspUsbTransferResult result;
    portENTER_CRITICAL(&bulkLock);
    if (s->stopping) { portEXIT_CRITICAL(&bulkLock); return; }
    if (input && s->cancelTx) {
      if (s->resetTxPid && s->epIn) espusb_device_edpt_clear_stall(s->rhport, s->epIn);
      s->resetTxPid = false;
      result = s->takeDirect(s->cancelTxStatus, 0, false);
      s->cancelTx = s->abortedTx = s->fenceTx = s->txBusy = false;
      s->transmit();
    } else if (!input && s->cancelRx) {
      if (s->resetRxPid && s->epOut) espusb_device_edpt_clear_stall(s->rhport, s->epOut);
      s->resetRxPid = false;
      result = s->takeReceive(s->cancelRxStatus, 0, false);
      s->cancelRx = s->abortedRx = s->fenceRx = s->rxBusy = false;
      s->receive();
    }
    portEXIT_CRITICAL(&bulkLock);
    if (result.buffer) {
      if (input && s->owner.directTxCompleteCallback_) s->owner.directTxCompleteCallback_(result);
      else if (!input && s->owner.directRxCompleteCallback_) s->owner.directRxCompleteCallback_(result);
    }
  }
  static void init() {}
  static void resetAll(uint8_t) { for (auto *s = first; s; s = s->next) s->reset(); }
  static bool deinit() { resetAll(0); return true; }
  static uint16_t open(uint8_t port, const EspUsbInterfaceDescriptor *itf, uint16_t length) {
    auto *s = first;
    while (s && s->owner.interfaceNumber_ != itf->bInterfaceNumber) s = s->next;
    if (!s) return 0;
    uint8_t in = 0, out = 0;
    uint16_t mps = 0;
    const uint16_t required = s->owner.openBulkInterface(port, reinterpret_cast<const uint8_t *>(itf), length, in, out, mps);
    if (!required) return 0;
    portENTER_CRITICAL(&bulkLock);
    s->rhport = port; s->epIn = in; s->epOut = out; s->packetSize = mps;
    s->receive();
    portEXIT_CRITICAL(&bulkLock);
    return required;
  }
  static bool control(uint8_t port, uint8_t stage, const EspUsbControlRequest *request) {
    if (request->bmRequestType == 1 && request->bRequest == 11 && !request->wValue && !request->wLength) {
      for (auto *s = first; s; s = s->next) {
        if (!s->epIn || espusb::internal::usbEndpointInterface(port, s->epIn) != request->wIndex) continue;
        if (stage != ESPUSB_CONTROL_STAGE_SETUP) return true;
        portENTER_CRITICAL(&bulkLock);
        const bool input = s->changeEndpoint(true, true, EspUsbTransferStatus::Cancelled);
        const bool output = s->changeEndpoint(false, true, EspUsbTransferStatus::Cancelled);
        portEXIT_CRITICAL(&bulkLock);
        if (!input || !output) return espusb::internal::rejectUsbControl();
        return espusb_device_control_status(port, request);
      }
    }
    if (request->bmRequestType == 2 && (request->bRequest == 1 || request->bRequest == 3) && !request->wValue && !request->wLength) {
      for (auto *s = first; s; s = s->next) {
        if (request->wIndex != s->epIn && request->wIndex != s->epOut) continue;
        if (stage != ESPUSB_CONTROL_STAGE_SETUP) return true;
        const bool clear = request->bRequest == 1;
        portENTER_CRITICAL(&bulkLock);
        const bool changed = s->changeEndpoint(request->wIndex == s->epIn, clear,
            clear ? EspUsbTransferStatus::Cancelled : EspUsbTransferStatus::TransferError);
        portEXIT_CRITICAL(&bulkLock);
        return changed || espusb::internal::rejectUsbControl();
      }
    }
    for (auto *s = first; s; s = s->next)
      if (s->owner.onBulkControl(port, stage, request)) return true;
    return false;
  }
  static bool complete(uint8_t, uint8_t ep, EspUsbControllerResult result, uint32_t n) {
    auto *s = first;
    while (s && s->epIn != ep && s->epOut != ep) s = s->next;
    if (!s) {
      for (auto *other = first; other; other = other->next)
        if (other->owner.onBulkExtraCompletion(ep, uint8_t(result), n)) return true;
      return false;
    }
    const bool input = ep == s->epIn;
    EspUsbTransferResult completion, received;
    bool copied = false;
    portENTER_CRITICAL(&bulkLock);
    if ((input && s->cancelTx) || (!input && s->cancelRx)) {
      portEXIT_CRITICAL(&bulkLock);
      return true;
    }
    if (input) {
      s->txBusy = false;
      if (s->directBuffer) {
        if (result != ESPUSB_TRANSFER_SUCCESS) {
          completion = s->takeDirect(EspUsbTransferStatus::TransferError, 0, false);
        } else if (s->needZlp && !s->sendingZlp) {
          s->sendingZlp = true;
          if (espusb_device_edpt_claim(s->rhport, s->epIn)) {
            s->txBusy = espusb_device_edpt_xfer(s->rhport, s->epIn, nullptr, 0, false);
            if (!s->txBusy) espusb_device_edpt_release(s->rhport, s->epIn);
          }
          if (!s->txBusy) completion = s->takeDirect(EspUsbTransferStatus::TransferError, n, true);
        } else {
          completion = s->takeDirect(EspUsbTransferStatus::Success,
                                     s->sendingZlp ? s->directLength : n, true);
        }
      } else if (s->copyZlp) {
        s->copyZlp = false;
      } else {
        copied = true;
        if (result == ESPUSB_TRANSFER_SUCCESS && n && n % s->packetSize == 0 && !s->tx.used &&
            s->terminateCopy &&
            espusb_device_edpt_claim(s->rhport, s->epIn)) {
          s->copyZlp = s->txBusy = espusb_device_edpt_xfer(s->rhport, s->epIn, nullptr, 0, false);
          if (!s->txBusy) espusb_device_edpt_release(s->rhport, s->epIn);
        }
      }
      s->transmit();
      if (!s->txBusy && !s->tx.used) s->flushing = false;
    } else {
      s->rxBusy = false;
      if (result == ESPUSB_TRANSFER_SUCCESS && n <= s->packetSize && s->packetUnit > 1 &&
          (s->owner.config_.rxMode == EspUsbBulkRxMode::Stream || s->owner.config_.rxMode == EspUsbBulkRxMode::Callback)) {
        const size_t filtered = s->owner.filterBulkRx(s->rxPacket, n);
        n = filtered <= n ? uint32_t(filtered) : 0;
      }
      if (s->owner.config_.rxMode == EspUsbBulkRxMode::Borrowed && s->directRxBuffer) {
        const bool success = result == ESPUSB_TRANSFER_SUCCESS && n <= s->directRxCapacity;
        received = s->takeReceive(success ? EspUsbTransferStatus::Success : EspUsbTransferStatus::TransferError,
                                  success ? n : 0, success);
      } else if (s->owner.config_.rxMode == EspUsbBulkRxMode::Stream) {
        if (result == ESPUSB_TRANSFER_SUCCESS && n <= s->packetSize) {
          s->rxPendingOffset = 0; s->rxPendingLength = n; s->fillReceiveRing();
        }
        s->receive();
      }
    }
    portEXIT_CRITICAL(&bulkLock);
    // No internal lock is held when ownership is returned to the application.
    if (completion.buffer && s->owner.directTxCompleteCallback_) s->owner.directTxCompleteCallback_(completion);
    if (received.buffer && s->owner.directRxCompleteCallback_) s->owner.directRxCompleteCallback_(received);
    if (result == ESPUSB_TRANSFER_SUCCESS) {
      if (input && copied) s->owner.handleTxComplete(n);
      else if (!input && (s->owner.config_.rxMode == EspUsbBulkRxMode::Stream || s->owner.config_.rxMode == EspUsbBulkRxMode::Callback) && n <= s->packetSize) s->owner.handleRx(s->owner.config_.rxMode == EspUsbBulkRxMode::Callback ? s->rxPacket : nullptr, n);
    }
    if (!input && s->owner.config_.rxMode == EspUsbBulkRxMode::Callback) {
      // Callback borrows the packet only for the call. Re-arm afterwards.
      portENTER_CRITICAL(&bulkLock); s->receive(); portEXIT_CRITICAL(&bulkLock);
    }
    return true;
  }
};
EspUsbBulkState *EspUsbBulkState::first = nullptr;
namespace {
const espusb::internal::UsbClassDriver bulkDriver = {
    .init = EspUsbBulkState::init, .deinit = EspUsbBulkState::deinit,
    .reset = EspUsbBulkState::resetAll, .open = EspUsbBulkState::open,
    .control_xfer_cb = EspUsbBulkState::control, .xfer_cb = EspUsbBulkState::complete,
    .sof = nullptr,
};
}

EspUsbDeviceBulkStream::EspUsbDeviceBulkStream(EspUsbDevice &d, uint16_t mps)
    : EspUsbDeviceClass(d), endpointSize_(mps ? mps : 64) {}
EspUsbDeviceBulkStream::~EspUsbDeviceBulkStream() { end(); }
esp_err_t EspUsbDeviceBulkStream::configure(const EspUsbBulkStreamConfig &c) {
  if (c.streamTermination != EspUsbStreamTermination::FunctionDefault && c.streamTermination != EspUsbStreamTermination::Continue && c.streamTermination != EspUsbStreamTermination::EndWithShortPacket) return ESP_ERR_INVALID_ARG;
  if (state_) return ESP_ERR_INVALID_STATE;
  if ((c.txCapacity && !c.transferBytes) ||
      (c.rxMode == EspUsbBulkRxMode::Stream ? !c.rxCapacity :
       (c.rxMode != EspUsbBulkRxMode::Callback && c.rxMode != EspUsbBulkRxMode::Borrowed && c.rxMode != EspUsbBulkRxMode::Disabled) || c.rxCapacity != 0) ||
      c.rxCapacity > SIZE_MAX / 2 || c.txCapacity > SIZE_MAX / 2) return ESP_ERR_INVALID_ARG;
  const uintptr_t rx = reinterpret_cast<uintptr_t>(c.rxBuffer), tx = reinterpret_cast<uintptr_t>(c.txBuffer);
  if ((c.rxBuffer && (!c.rxCapacity || rx > UINTPTR_MAX - c.rxCapacity)) ||
      (c.txBuffer && (!c.txCapacity || tx > UINTPTR_MAX - c.txCapacity)) ||
      (c.rxBuffer && c.txBuffer && rx < tx + c.txCapacity && tx < rx + c.rxCapacity)) return ESP_ERR_INVALID_ARG;
  config_ = c;
  return ESP_OK;
}
bool EspUsbDeviceBulkStream::begin() {
  if (!registered_ || state_ || espusb::internal::tinyUsbRuntimeStarted()) { startError_ = ESP_ERR_INVALID_STATE; return false; }
  const size_t unit = bulkPacketUnit();
  if (!unit || (unit > 1 && (config_.rxCapacity % unit || config_.txCapacity % unit || config_.transferBytes % unit))) {
    startError_ = ESP_ERR_INVALID_ARG; return false;
  }
  uint16_t maxPacket = 64;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (device_.config().controller != EspUsbController::FullSpeed) maxPacket = 512;
#endif
  if (config_.rxMode == EspUsbBulkRxMode::Callback && !rxDataCallback_) { startError_ = ESP_ERR_INVALID_ARG; return false; }
  if (config_.rxMode == EspUsbBulkRxMode::Borrowed && !directRxCompleteCallback_) { startError_ = ESP_ERR_INVALID_ARG; return false; }
  auto *s = espusb::internal::newUsbObject<EspUsbBulkState>(*this);
  if (s) s->packetUnit = unit;
  if (!s || !s->allocate(maxPacket)) { espusb::internal::deleteUsbObject(s); startError_ = ESP_ERR_NO_MEM; return false; }
  if (!EspUsbBulkState::first && !espUsbDeviceRegisterAppDrivers(&bulkDriver, 1)) { espusb::internal::deleteUsbObject(s); startError_ = ESP_ERR_NO_MEM; return false; }
  portENTER_CRITICAL(&bulkLock);
  s->next = EspUsbBulkState::first; EspUsbBulkState::first = s; state_ = s;
  portEXIT_CRITICAL(&bulkLock);
  startError_ = ESP_OK;
  return true;
}
void EspUsbDeviceBulkStream::prepareToStop() {
  portENTER_CRITICAL(&bulkLock);
  if (state_) state_->stopping = true;
  portEXIT_CRITICAL(&bulkLock);
}
void EspUsbDeviceBulkStream::end() {
  if (device_.tinyusbStarted_ && state_) { device_.end(); return; }
  portENTER_CRITICAL(&bulkLock);
  auto *state = state_;
  state_ = nullptr;
  if (state) {
    auto **link = &EspUsbBulkState::first;
    while (*link && *link != state) link = &(*link)->next;
    if (*link) *link = state->next;
  }
  const bool empty = !EspUsbBulkState::first;
  portEXIT_CRITICAL(&bulkLock);
  espusb::internal::deleteUsbObject(state);
  if (empty) espUsbDeviceUnregisterAppDrivers(&bulkDriver, 1);
}

bool EspUsbDeviceBulkStream::mounted() const {
  portENTER_CRITICAL(&bulkLock);
  const bool value = state_ && !state_->stopping && state_->epIn && state_->epOut;
  portEXIT_CRITICAL(&bulkLock);
  return value;
}
int EspUsbDeviceBulkStream::available() {
  portENTER_CRITICAL(&bulkLock);
  const size_t value = state_ ? state_->rx.used + state_->rxPendingLength : 0;
  portEXIT_CRITICAL(&bulkLock);
  return int(std::min(value, size_t(INT_MAX)));
}
int EspUsbDeviceBulkStream::read() { uint8_t b; return read(&b, 1) ? b : -1; }
size_t EspUsbDeviceBulkStream::read(uint8_t *buffer, size_t n) {
  if (!buffer) return 0;
  portENTER_CRITICAL(&bulkLock);
  if (!state_ || state_->stopping || config_.rxMode != EspUsbBulkRxMode::Stream) n = 0;
  else {
    n = std::min(n, state_->rx.used + state_->rxPendingLength);
    if (state_->packetUnit > 1) n -= n % state_->packetUnit;
    size_t copied = 0;
    while (copied < n) {
      copied += state_->rx.pop(buffer + copied, n - copied);
      state_->fillReceiveRing();
    }
    state_->receive();
  }
  portEXIT_CRITICAL(&bulkLock);
  return n;
}
size_t EspUsbDeviceBulkStream::write(uint8_t b) { return write(&b, 1); }
size_t EspUsbDeviceBulkStream::write(const uint8_t *buffer, size_t n) {
  if (!buffer) return 0;
  portENTER_CRITICAL(&bulkLock);
  if (!state_ || state_->stopping || state_->cancelTx || !state_->epIn || state_->directBuffer || espusb_device_edpt_stalled(state_->rhport, state_->epIn)) n = 0;
  else {
    n = std::min(n, state_->tx.capacity - state_->tx.used);
    if (state_->packetUnit > 1) {
      n -= n % state_->packetUnit;
      if (n && !validateBulkTx(buffer, n)) n = 0;
    }
    n = state_->tx.push(buffer, n); if (config_.autoFlush) state_->flushing = true; state_->transmit();
  }
  portEXIT_CRITICAL(&bulkLock);
  return n;
}
size_t EspUsbDeviceBulkStream::writeAvailable() const {
  portENTER_CRITICAL(&bulkLock);
  const size_t n = state_ && !state_->stopping && !state_->cancelTx && state_->epIn && !state_->directBuffer && !espusb_device_edpt_stalled(state_->rhport, state_->epIn) ? state_->tx.capacity - state_->tx.used : 0;
  portEXIT_CRITICAL(&bulkLock);
  return n;
}
size_t EspUsbDeviceBulkStream::writeCapacity() const {
  portENTER_CRITICAL(&bulkLock);
  const size_t n = state_ ? state_->tx.capacity : 0;
  portEXIT_CRITICAL(&bulkLock);
  return n;
}
size_t EspUsbDeviceBulkStream::copyTransferCapacity() const {
  portENTER_CRITICAL(&bulkLock);
  const size_t n = state_ && state_->tx.capacity ? config_.transferBytes : 0;
  portEXIT_CRITICAL(&bulkLock);
  return n;
}
size_t EspUsbDeviceBulkStream::readCapacity() const {
  portENTER_CRITICAL(&bulkLock);
  const size_t n = state_ ? state_->rx.capacity : 0;
  portEXIT_CRITICAL(&bulkLock);
  return n;
}
bool EspUsbDeviceBulkStream::waitWritable(size_t n, uint32_t timeoutMs) {
  if (n > writeCapacity()) return false;
  const TickType_t start = xTaskGetTickCount(), timeout = pdMS_TO_TICKS(timeoutMs);
  do {
    if (!mounted()) return false;
    if (writeAvailable() >= n) return true;
    flush();
    if (!timeoutMs || xTaskGetTickCount() - start >= timeout) return false;
    vTaskDelay(1);
  } while (true);
}
void EspUsbDeviceBulkStream::flush() {
  portENTER_CRITICAL(&bulkLock);
  if (state_ && !state_->stopping && state_->tx.used) { state_->flushing = true; state_->transmit(); }
  portEXIT_CRITICAL(&bulkLock);
}
bool EspUsbDeviceBulkStream::directWriteSupported() { return true; }
size_t EspUsbDeviceBulkStream::maxDirectTransfer() const {
  portENTER_CRITICAL(&bulkLock);
  const size_t value = state_ && !state_->stopping && state_->epIn
      ? size_t(UINT16_MAX) : 0;
  portEXIT_CRITICAL(&bulkLock);
  return value;
}
esp_err_t EspUsbDeviceBulkStream::onDirectTxComplete(DirectCompleteCallback callback) {
  if (state_) return ESP_ERR_INVALID_STATE;
  directTxCompleteCallback_ = callback;
  return ESP_OK;
}
EspUsbTransferSubmission EspUsbDeviceBulkStream::writeDirect(const void *buffer, size_t n, EspUsbTransferTermination termination) {
  auto error = EspUsbTransferError::None;
  uint64_t id = 0;
  if (!buffer || !n || n > UINT16_MAX || !directTxCompleteCallback_ ||
      (termination != EspUsbTransferTermination::Continue && termination != EspUsbTransferTermination::EndWithShortPacket))
    error = EspUsbTransferError::BadArgument;
  else if (reinterpret_cast<uintptr_t>(buffer) & 63) error = EspUsbTransferError::NotAligned;
  else if (reinterpret_cast<uintptr_t>(buffer) > UINTPTR_MAX - (n - 1))
    error = EspUsbTransferError::BadArgument;
  else if (!esp_ptr_dma_capable(buffer) || !esp_ptr_dma_capable(reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(buffer) + n - 1)))
    error = EspUsbTransferError::NotDmaCapable;
  portENTER_CRITICAL(&bulkLock);
  if (error == EspUsbTransferError::None) {
    if (!state_ || state_->stopping || !state_->epIn) error = EspUsbTransferError::NotMounted;
    else if (state_->cancelTx || state_->directBuffer || state_->txBusy || state_->tx.used || !espusb_device_edpt_claim(state_->rhport, state_->epIn))
      error = EspUsbTransferError::Busy;
    else {
      state_->directBuffer = buffer; state_->directLength = n;
      if (++nextTransferId_ == 0) ++nextTransferId_;
      state_->txId = id = nextTransferId_;
      state_->needZlp = termination == EspUsbTransferTermination::EndWithShortPacket && n % state_->packetSize == 0;
      state_->txBusy = true;
      if (!espusb_device_edpt_xfer(state_->rhport, state_->epIn, static_cast<uint8_t *>(const_cast<void *>(buffer)), uint16_t(n), false)) {
        state_->takeDirect(EspUsbTransferStatus::TransferError, 0, false);
        state_->txBusy = false;
        espusb_device_edpt_release(state_->rhport, state_->epIn);
        error = EspUsbTransferError::TransferFailed;
      }
    }
  }
  portEXIT_CRITICAL(&bulkLock);
  return {error, error == EspUsbTransferError::None ? id : 0};
}
size_t EspUsbDeviceBulkStream::maxDirectReceive() const {
  portENTER_CRITICAL(&bulkLock);
  size_t value = 0;
  if (state_ && !state_->stopping && state_->epOut && config_.rxMode == EspUsbBulkRxMode::Borrowed) {
    value = size_t(UINT16_MAX);
    value -= value % std::max(size_t(state_->packetSize), size_t(64));
  }
  portEXIT_CRITICAL(&bulkLock);
  return value;
}
esp_err_t EspUsbDeviceBulkStream::onDirectRxComplete(DirectCompleteCallback callback) {
  if (state_) return ESP_ERR_INVALID_STATE;
  directRxCompleteCallback_ = callback;
  return ESP_OK;
}
EspUsbTransferSubmission EspUsbDeviceBulkStream::readDirect(void *buffer, size_t capacity) {
  auto error = EspUsbTransferError::None;
  uint64_t id = 0;
  if (!buffer || !capacity || capacity > UINT16_MAX || capacity % 64 ||
      reinterpret_cast<uintptr_t>(buffer) > UINTPTR_MAX - (capacity - 1)) error = EspUsbTransferError::BadArgument;
  else if (reinterpret_cast<uintptr_t>(buffer) & 63) error = EspUsbTransferError::NotAligned;
  else if (!esp_ptr_dma_capable(buffer) || !esp_ptr_dma_capable(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(buffer) + capacity - 1)))
    error = EspUsbTransferError::NotDmaCapable;
  portENTER_CRITICAL(&bulkLock);
  if (error == EspUsbTransferError::None) {
    if (!state_ || state_->stopping || !state_->epOut) error = EspUsbTransferError::NotMounted;
    else if (config_.rxMode != EspUsbBulkRxMode::Borrowed) error = EspUsbTransferError::NotSupported;
    else if (!directRxCompleteCallback_) error = EspUsbTransferError::BadArgument;
    else if (capacity % state_->packetSize)
      error = EspUsbTransferError::BadArgument;
    else if (state_->cancelRx || state_->directRxBuffer || state_->rxBusy || !espusb_device_edpt_claim(state_->rhport, state_->epOut))
      error = EspUsbTransferError::Busy;
    else {
      state_->directRxBuffer = buffer; state_->directRxCapacity = capacity; state_->rxBusy = true;
      if (++nextTransferId_ == 0) ++nextTransferId_;
      state_->rxId = id = nextTransferId_;
      if (!espusb_device_edpt_xfer(state_->rhport, state_->epOut, static_cast<uint8_t *>(buffer), uint16_t(capacity), false)) {
        state_->takeReceive(EspUsbTransferStatus::TransferError, 0, false);
        state_->rxBusy = false; espusb_device_edpt_release(state_->rhport, state_->epOut);
        error = EspUsbTransferError::TransferFailed;
      }
    }
  }
  portEXIT_CRITICAL(&bulkLock);
  return {error, error == EspUsbTransferError::None ? id : 0};
}
esp_err_t EspUsbDeviceBulkStream::cancelDirectTx(uint64_t id) {
  portENTER_CRITICAL(&bulkLock);
  const auto error = state_ ? state_->requestCancel(true, id) : ESP_ERR_INVALID_STATE;
  portEXIT_CRITICAL(&bulkLock);
  return error;
}
esp_err_t EspUsbDeviceBulkStream::cancelDirectRx(uint64_t id) {
  portENTER_CRITICAL(&bulkLock);
  const auto error = state_ ? state_->requestCancel(false, id) : ESP_ERR_INVALID_STATE;
  portEXIT_CRITICAL(&bulkLock);
  return error;
}
esp_err_t EspUsbDeviceBulkStream::onRx(RxCallback cb) {
  if (state_) return ESP_ERR_INVALID_STATE;
  rxCallback_ = cb;
  return ESP_OK;
}
esp_err_t EspUsbDeviceBulkStream::onRxData(RxDataCallback cb) {
  if (state_) return ESP_ERR_INVALID_STATE;
  rxDataCallback_ = cb;
  return ESP_OK;
}
esp_err_t EspUsbDeviceBulkStream::onTxComplete(TxCompleteCallback cb) {
  if (state_) return ESP_ERR_INVALID_STATE;
  txCompleteCallback_ = cb;
  return ESP_OK;
}

void EspUsbDeviceBulkStream::handleRx(const uint8_t *data, size_t n) {
  if (data && rxDataCallback_) rxDataCallback_(data, n);
  if (rxCallback_) rxCallback_(available());
}
void EspUsbDeviceBulkStream::handleTxComplete(size_t n) { if (txCompleteCallback_) txCompleteCallback_(n); }

bool EspUsbDeviceBulkStream::openBulkPair(uint8_t port, const uint8_t *p, uint8_t &in, uint8_t &out, uint16_t &mps) {
  if (!state_) return false;
  const uint8_t *first = p;
  for (unsigned i = 0; i < 2; ++i, p += sizeof(EspUsbEndpointDescriptor)) {
    const auto *ep = reinterpret_cast<const EspUsbEndpointDescriptor *>(p);
    const uint16_t size = espusb_endpoint_packet_size(ep);
    if (ep->bLength != sizeof(*ep) || ep->bDescriptorType != ESPUSB_DESC_ENDPOINT ||
        espusb_endpoint_type(ep) != ESPUSB_XFER_BULK || !size || size > state_->packetCapacity ||
        (mps && mps != size)) return false;
    mps = size;
    if (espusb_endpoint_direction(ep->bEndpointAddress) == ESPUSB_DIR_IN) in = ep->bEndpointAddress;
    else out = ep->bEndpointAddress;
  }
  if (!in || !out) return false;
  return espusb_device_edpt_open(port, reinterpret_cast<const EspUsbEndpointDescriptor *>(first)) &&
      espusb_device_edpt_open(port, reinterpret_cast<const EspUsbEndpointDescriptor *>(first + sizeof(EspUsbEndpointDescriptor)));
}
