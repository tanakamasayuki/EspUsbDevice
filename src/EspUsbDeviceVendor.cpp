#include "EspUsbDevice.h"
#include "internal/EspUsbDeviceAppDriver.h"
#include "internal/EspUsbTinyUsbRuntime.h"
#include <algorithm>
#include <limits.h>
#include <new>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "device/usbd_pvt.h"
#include "device/usbd.h"

namespace {
portMUX_TYPE vendorLock = portMUX_INITIALIZER_UNLOCKED;
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
  return static_cast<uint8_t *>(heap_caps_aligned_alloc(
      64, (n + 63) & ~size_t(63), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
}
extern const usbd_class_driver_t vendorDriver;
}

// Temporary adapter to TinyUSB EP0/DCD. Queue storage and class policy are
// independent of TinyUSB Vendor and are allocated only for active instances.
struct EspUsbVendorState {
  static EspUsbVendorState *first;
  EspUsbVendorState *next = nullptr;
  EspUsbDeviceVendor &owner;
  Ring rx, tx;
  uint8_t *rxPacket = nullptr, *txPacket = nullptr;
  uint8_t rhport = 0, epIn = 0, epOut = 0;
  uint16_t packetSize = 0, packetCapacity = 0;
  bool txBusy = false, rxBusy = false, flushing = false, stopping = false;
  const void *directBuffer = nullptr;
  size_t directLength = 0;
  void *directRxBuffer = nullptr;
  size_t directRxCapacity = 0;
  bool needZlp = false, sendingZlp = false;
  EspUsbTransferResult takeDirect(EspUsbTransferStatus status, size_t actual, bool known) {
    EspUsbTransferResult result{directBuffer, directLength, actual, status, known};
    directBuffer = nullptr; directLength = 0; needZlp = sendingZlp = false;
    return result;
  }

  EspUsbTransferResult takeReceive(EspUsbTransferStatus status, size_t actual, bool known) {
    EspUsbTransferResult result{directRxBuffer, directRxCapacity, actual, status, known};
    directRxBuffer = nullptr; directRxCapacity = 0;
    return result;
  }

  explicit EspUsbVendorState(EspUsbDeviceVendor &o) : owner(o) {}
  ~EspUsbVendorState() {
    if (rx.owned) free(rx.data);
    if (tx.owned) free(tx.data);
    heap_caps_free(rxPacket); heap_caps_free(txPacket);
  }
  bool allocate(uint16_t maxPacket) {
    packetCapacity = maxPacket;
    rx.capacity = owner.config_.rxCapacity; tx.capacity = owner.config_.txCapacity;
    rx.owned = !owner.config_.rxBuffer;
    tx.owned = !owner.config_.txBuffer;
    rx.data = owner.config_.rxBuffer ? owner.config_.rxBuffer : (rx.capacity ? static_cast<uint8_t *>(malloc(rx.capacity)) : nullptr);
    tx.data = owner.config_.txBuffer ? owner.config_.txBuffer : (tx.capacity ? static_cast<uint8_t *>(malloc(tx.capacity)) : nullptr);
    rxPacket = owner.config_.rxMode == EspUsbVendorRxMode::Borrowed ? nullptr : allocateDma(maxPacket);
    txPacket = tx.capacity ? allocateDma(owner.config_.transferBytes) : nullptr;
    return (!rx.capacity || rx.data) && (owner.config_.rxMode == EspUsbVendorRxMode::Borrowed || rxPacket) && (!tx.capacity || (tx.data && txPacket));
  }
  // Endpoint operations hold the instance lock. User callbacks never do.
  void receive() {
    if (owner.config_.rxMode == EspUsbVendorRxMode::Borrowed || stopping || !epOut || rxBusy ||
        (owner.config_.rxMode == EspUsbVendorRxMode::Stream && rx.capacity - rx.used < packetSize)) return;
    if (!usbd_edpt_claim(rhport, epOut)) return;
    rxBusy = true;
    if (!usbd_edpt_xfer(rhport, epOut, rxPacket, packetSize, false)) {
      rxBusy = false; usbd_edpt_release(rhport, epOut);
    }
  }
  void transmit() {
    if (stopping || !epIn || txBusy || directBuffer || !flushing || !tx.used) return;
    if (!usbd_edpt_claim(rhport, epIn)) return;
    // The interim DWC2 adapter has a 10-bit packet count and a 16-bit
    // software length. Split copied data instead of overflowing either.
    const size_t n = std::min(tx.used, std::min(size_t(owner.config_.transferBytes), size_t(packetSize) * 1023));
    tx.peek(txPacket, n);
    txBusy = true;
    if (usbd_edpt_xfer(rhport, epIn, txPacket, uint16_t(n), false)) tx.drop(n);
    else { txBusy = false; usbd_edpt_release(rhport, epIn); }
  }
  void reset() {
    portENTER_CRITICAL(&vendorLock);
    const auto status = stopping ? EspUsbTransferStatus::Stopped : EspUsbTransferStatus::BusReset;
    const auto result = takeDirect(status, 0, false);
    const auto received = takeReceive(status, 0, false);
    epIn = epOut = 0; txBusy = rxBusy = flushing = false;
    rx.clear(); tx.clear();
    portEXIT_CRITICAL(&vendorLock);
    if (result.buffer && owner.directTxCompleteCallback_) owner.directTxCompleteCallback_(result);
    if (received.buffer && owner.directRxCompleteCallback_) owner.directRxCompleteCallback_(received);
  }
  static void init() {}
  static void resetAll(uint8_t) { for (auto *s = first; s; s = s->next) s->reset(); }
  static bool deinit() { resetAll(0); return true; }
  static uint16_t open(uint8_t port, const tusb_desc_interface_t *itf, uint16_t length) {
    const uint16_t required = sizeof(*itf) + 2 * sizeof(tusb_desc_endpoint_t);
    if (itf->bInterfaceClass != TUSB_CLASS_VENDOR_SPECIFIC || itf->bNumEndpoints != 2 || length < required) return 0;
    auto *s = first;
    while (s && s->owner.interfaceNumber_ != itf->bInterfaceNumber) s = s->next;
    if (!s) return 0;
    const auto *p = reinterpret_cast<const uint8_t *>(itf) + sizeof(*itf);
    uint8_t in = 0, out = 0;
    uint16_t mps = 0;
    for (unsigned i = 0; i < 2; ++i, p += sizeof(tusb_desc_endpoint_t)) {
      const auto *ep = reinterpret_cast<const tusb_desc_endpoint_t *>(p);
      const uint16_t size = tu_edpt_packet_size(ep);
      if (ep->bLength != sizeof(*ep) || ep->bDescriptorType != TUSB_DESC_ENDPOINT ||
          ep->bmAttributes.xfer != TUSB_XFER_BULK || !size || size > s->packetCapacity ||
          (s->tx.capacity && size > s->owner.config_.transferBytes) ||
          (s->owner.config_.rxMode == EspUsbVendorRxMode::Stream && size > s->rx.capacity) || (mps && mps != size)) return 0;
      if (!usbd_edpt_open(port, ep)) return 0;
      mps = size;
      if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN) in = ep->bEndpointAddress;
      else out = ep->bEndpointAddress;
    }
    if (!in || !out) return 0;
    portENTER_CRITICAL(&vendorLock);
    s->rhport = port; s->epIn = in; s->epOut = out; s->packetSize = mps;
    s->receive();
    portEXIT_CRITICAL(&vendorLock);
    return required;
  }
  static bool control(uint8_t port, uint8_t stage, const tusb_control_request_t *request) {
    for (auto *s = first; s; s = s->next)
      if (s->owner.onVendorControlRequest(port, stage, request)) return true;
    return false;
  }
  static bool complete(uint8_t, uint8_t ep, xfer_result_t result, uint32_t n) {
    auto *s = first;
    while (s && s->epIn != ep && s->epOut != ep) s = s->next;
    if (!s) return false;
    const bool input = ep == s->epIn;
    EspUsbTransferResult completion, received;
    bool copied = false;
    portENTER_CRITICAL(&vendorLock);
    if (input) {
      s->txBusy = false;
      if (s->directBuffer) {
        if (result != XFER_RESULT_SUCCESS) {
          completion = s->takeDirect(EspUsbTransferStatus::TransferError, 0, false);
        } else if (s->needZlp && !s->sendingZlp) {
          s->sendingZlp = true;
          if (usbd_edpt_claim(s->rhport, s->epIn)) {
            s->txBusy = usbd_edpt_xfer(s->rhport, s->epIn, nullptr, 0, false);
            if (!s->txBusy) usbd_edpt_release(s->rhport, s->epIn);
          }
          if (!s->txBusy) completion = s->takeDirect(EspUsbTransferStatus::TransferError, n, true);
        } else {
          completion = s->takeDirect(EspUsbTransferStatus::Success,
                                     s->sendingZlp ? s->directLength : n, true);
        }
      } else copied = true;
      s->transmit();
      if (!s->txBusy && !s->tx.used) s->flushing = false;
    } else {
      s->rxBusy = false;
      if (s->owner.config_.rxMode == EspUsbVendorRxMode::Borrowed && s->directRxBuffer) {
        const bool success = result == XFER_RESULT_SUCCESS && n <= s->directRxCapacity;
        received = s->takeReceive(success ? EspUsbTransferStatus::Success : EspUsbTransferStatus::TransferError,
                                  success ? n : 0, success);
      } else if (s->owner.config_.rxMode == EspUsbVendorRxMode::Stream) {
        if (result == XFER_RESULT_SUCCESS && n <= s->packetSize) s->rx.push(s->rxPacket, n);
        s->receive();
      }
    }
    portEXIT_CRITICAL(&vendorLock);
    // No internal lock is held when ownership is returned to the application.
    if (completion.buffer && s->owner.directTxCompleteCallback_) s->owner.directTxCompleteCallback_(completion);
    if (received.buffer && s->owner.directRxCompleteCallback_) s->owner.directRxCompleteCallback_(received);
    if (result == XFER_RESULT_SUCCESS) {
      if (input && copied) s->owner.handleTxComplete(n);
      else if (!input && s->owner.config_.rxMode != EspUsbVendorRxMode::Borrowed && n <= s->packetSize) s->owner.handleRx(s->owner.config_.rxMode == EspUsbVendorRxMode::Callback ? s->rxPacket : nullptr, n);
    }
    if (!input && s->owner.config_.rxMode == EspUsbVendorRxMode::Callback) {
      // Callback borrows the packet only for the call. Re-arm afterwards.
      portENTER_CRITICAL(&vendorLock); s->receive(); portEXIT_CRITICAL(&vendorLock);
    }
    return true;
  }
};
EspUsbVendorState *EspUsbVendorState::first = nullptr;
namespace {
const usbd_class_driver_t vendorDriver = {
    .name = "EspVendor", .init = EspUsbVendorState::init, .deinit = EspUsbVendorState::deinit,
    .reset = EspUsbVendorState::resetAll, .open = EspUsbVendorState::open,
    .control_xfer_cb = EspUsbVendorState::control, .xfer_cb = EspUsbVendorState::complete,
    .xfer_isr = nullptr, .sof = nullptr,
};
}

EspUsbDeviceVendor::EspUsbDeviceVendor(EspUsbDevice &d, uint16_t mps)
    : EspUsbDeviceClass(d), endpointSize_(mps ? mps : 64) {}
EspUsbDeviceVendor::~EspUsbDeviceVendor() { end(); }
esp_err_t EspUsbDeviceVendor::configure(const EspUsbVendorConfig &c) {
  if (state_) return ESP_ERR_INVALID_STATE;
  if ((c.txCapacity && !c.transferBytes) ||
      (c.rxMode == EspUsbVendorRxMode::Stream ? !c.rxCapacity :
       (c.rxMode != EspUsbVendorRxMode::Callback && c.rxMode != EspUsbVendorRxMode::Borrowed) || c.rxCapacity != 0) ||
      c.rxCapacity > SIZE_MAX / 2 || c.txCapacity > SIZE_MAX / 2) return ESP_ERR_INVALID_ARG;
  const uintptr_t rx = reinterpret_cast<uintptr_t>(c.rxBuffer), tx = reinterpret_cast<uintptr_t>(c.txBuffer);
  if ((c.rxBuffer && (!c.rxCapacity || rx > UINTPTR_MAX - c.rxCapacity)) ||
      (c.txBuffer && (!c.txCapacity || tx > UINTPTR_MAX - c.txCapacity)) ||
      (c.rxBuffer && c.txBuffer && rx < tx + c.txCapacity && tx < rx + c.rxCapacity)) return ESP_ERR_INVALID_ARG;
  config_ = c;
  return ESP_OK;
}
bool EspUsbDeviceVendor::begin() {
  if (state_ || espusb::internal::tinyUsbRuntimeStarted()) { startError_ = ESP_ERR_INVALID_STATE; return false; }
  uint16_t maxPacket = 64;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (device_.config().controller != EspUsbController::FullSpeed) maxPacket = 512;
#endif
  if ((config_.rxMode == EspUsbVendorRxMode::Stream && config_.rxCapacity < maxPacket) ||
      (config_.txCapacity && config_.transferBytes < maxPacket)) { startError_ = ESP_ERR_INVALID_SIZE; return false; }
  if (config_.rxMode == EspUsbVendorRxMode::Callback && !rxDataCallback_) { startError_ = ESP_ERR_INVALID_ARG; return false; }
  if (config_.rxMode == EspUsbVendorRxMode::Borrowed && !directRxCompleteCallback_) { startError_ = ESP_ERR_INVALID_ARG; return false; }
  auto *s = new (std::nothrow) EspUsbVendorState(*this);
  if (!s || !s->allocate(maxPacket)) { delete s; startError_ = ESP_ERR_NO_MEM; return false; }
  if (!EspUsbVendorState::first && !espUsbDeviceRegisterAppDrivers(&vendorDriver, 1)) { delete s; startError_ = ESP_ERR_NO_MEM; return false; }
  portENTER_CRITICAL(&vendorLock);
  s->next = EspUsbVendorState::first; EspUsbVendorState::first = s; state_ = s;
  portEXIT_CRITICAL(&vendorLock);
  startError_ = ESP_OK;
  return true;
}
void EspUsbDeviceVendor::prepareToStop() {
  portENTER_CRITICAL(&vendorLock);
  if (state_) state_->stopping = true;
  portEXIT_CRITICAL(&vendorLock);
}
void EspUsbDeviceVendor::end() {
  if (device_.tinyusbStarted_) { device_.end(); return; }
  portENTER_CRITICAL(&vendorLock);
  auto *state = state_;
  state_ = nullptr;
  if (state) {
    auto **link = &EspUsbVendorState::first;
    while (*link && *link != state) link = &(*link)->next;
    if (*link) *link = state->next;
  }
  const bool empty = !EspUsbVendorState::first;
  portEXIT_CRITICAL(&vendorLock);
  delete state;
  if (empty) espUsbDeviceUnregisterAppDrivers(&vendorDriver, 1);
}
uint16_t EspUsbDeviceVendor::configurationDescriptor(uint8_t *dst, uint8_t itf, uint8_t ep, uint16_t ceiling) {
  interfaceNumber_ = itf;
  const uint16_t packet = ceiling ? std::min(endpointSize_, ceiling) : endpointSize_;
  const uint8_t descriptor[] = {
      9, TUSB_DESC_INTERFACE, itf, 0, 2, TUSB_CLASS_VENDOR_SPECIFIC, 0, 0, 0,
      7, TUSB_DESC_ENDPOINT, ep, TUSB_XFER_BULK, uint8_t(packet), uint8_t(packet >> 8), 0,
      7, TUSB_DESC_ENDPOINT, uint8_t(0x80 | ep), TUSB_XFER_BULK, uint8_t(packet), uint8_t(packet >> 8), 0,
  };
  memcpy(dst, descriptor, sizeof(descriptor)); return sizeof(descriptor);
}
bool EspUsbDeviceVendor::mounted() const {
  portENTER_CRITICAL(&vendorLock);
  const bool value = state_ && !state_->stopping && state_->epIn && state_->epOut;
  portEXIT_CRITICAL(&vendorLock);
  return value;
}
int EspUsbDeviceVendor::available() {
  portENTER_CRITICAL(&vendorLock);
  const size_t value = state_ ? state_->rx.used : 0;
  portEXIT_CRITICAL(&vendorLock);
  return int(std::min(value, size_t(INT_MAX)));
}
int EspUsbDeviceVendor::read() { uint8_t b; return read(&b, 1) ? b : -1; }
size_t EspUsbDeviceVendor::read(uint8_t *buffer, size_t n) {
  if (!buffer) return 0;
  portENTER_CRITICAL(&vendorLock);
  if (!state_ || state_->stopping || config_.rxMode != EspUsbVendorRxMode::Stream) n = 0;
  else { n = state_->rx.pop(buffer, n); state_->receive(); }
  portEXIT_CRITICAL(&vendorLock);
  return n;
}
size_t EspUsbDeviceVendor::write(uint8_t b) { return write(&b, 1); }
size_t EspUsbDeviceVendor::write(const uint8_t *buffer, size_t n) {
  if (!buffer) return 0;
  portENTER_CRITICAL(&vendorLock);
  if (!state_ || state_->stopping || !state_->epIn || state_->directBuffer) n = 0;
  else { n = state_->tx.push(buffer, n); if (config_.autoFlush) state_->flushing = true; state_->transmit(); }
  portEXIT_CRITICAL(&vendorLock);
  return n;
}
size_t EspUsbDeviceVendor::writeAvailable() const {
  portENTER_CRITICAL(&vendorLock);
  const size_t n = state_ && !state_->stopping && state_->epIn && !state_->directBuffer ? state_->tx.capacity - state_->tx.used : 0;
  portEXIT_CRITICAL(&vendorLock);
  return n;
}
size_t EspUsbDeviceVendor::writeCapacity() const {
  portENTER_CRITICAL(&vendorLock);
  const size_t n = state_ ? state_->tx.capacity : 0;
  portEXIT_CRITICAL(&vendorLock);
  return n;
}
size_t EspUsbDeviceVendor::readCapacity() const {
  portENTER_CRITICAL(&vendorLock);
  const size_t n = state_ ? state_->rx.capacity : 0;
  portEXIT_CRITICAL(&vendorLock);
  return n;
}
bool EspUsbDeviceVendor::waitWritable(size_t n, uint32_t timeoutMs) {
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
void EspUsbDeviceVendor::flush() {
  portENTER_CRITICAL(&vendorLock);
  if (state_ && !state_->stopping && state_->tx.used) { state_->flushing = true; state_->transmit(); }
  portEXIT_CRITICAL(&vendorLock);
}
bool EspUsbDeviceVendor::directWriteSupported() { return true; }
size_t EspUsbDeviceVendor::maxDirectTransfer() const {
  portENTER_CRITICAL(&vendorLock);
  const size_t value = state_ && !state_->stopping && state_->epIn
      ? std::min(size_t(UINT16_MAX), size_t(state_->packetSize) * 1023) : 0;
  portEXIT_CRITICAL(&vendorLock);
  return value;
}
esp_err_t EspUsbDeviceVendor::onDirectTxComplete(DirectCompleteCallback callback) {
  if (state_) return ESP_ERR_INVALID_STATE;
  directTxCompleteCallback_ = callback;
  return ESP_OK;
}
EspUsbTransferSubmission EspUsbDeviceVendor::writeDirect(const void *buffer, size_t n, EspUsbTransferTermination termination) {
  auto error = EspUsbTransferError::None;
  if (!buffer || !n || n > UINT16_MAX || !directTxCompleteCallback_ ||
      (termination != EspUsbTransferTermination::Continue && termination != EspUsbTransferTermination::EndWithShortPacket))
    error = EspUsbTransferError::BadArgument;
  else if (reinterpret_cast<uintptr_t>(buffer) & 63) error = EspUsbTransferError::NotAligned;
  else if (reinterpret_cast<uintptr_t>(buffer) > UINTPTR_MAX - (n - 1))
    error = EspUsbTransferError::BadArgument;
  else if (!esp_ptr_dma_capable(buffer) || !esp_ptr_dma_capable(reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(buffer) + n - 1)))
    error = EspUsbTransferError::NotDmaCapable;
  portENTER_CRITICAL(&vendorLock);
  if (error == EspUsbTransferError::None) {
    if (!state_ || state_->stopping || !state_->epIn) error = EspUsbTransferError::NotMounted;
    else if (n > size_t(state_->packetSize) * 1023) error = EspUsbTransferError::BadArgument;
    else if (state_->directBuffer || state_->txBusy || state_->tx.used || !usbd_edpt_claim(state_->rhport, state_->epIn))
      error = EspUsbTransferError::Busy;
    else {
      state_->directBuffer = buffer; state_->directLength = n;
      state_->needZlp = termination == EspUsbTransferTermination::EndWithShortPacket && n % state_->packetSize == 0;
      state_->txBusy = true;
      if (!usbd_edpt_xfer(state_->rhport, state_->epIn, static_cast<uint8_t *>(const_cast<void *>(buffer)), uint16_t(n), false)) {
        state_->takeDirect(EspUsbTransferStatus::TransferError, 0, false);
        state_->txBusy = false;
        usbd_edpt_release(state_->rhport, state_->epIn);
        error = EspUsbTransferError::TransferFailed;
      }
    }
  }
  portEXIT_CRITICAL(&vendorLock);
  return {error};
}
size_t EspUsbDeviceVendor::maxDirectReceive() const {
  portENTER_CRITICAL(&vendorLock);
  size_t value = 0;
  if (state_ && !state_->stopping && state_->epOut && config_.rxMode == EspUsbVendorRxMode::Borrowed) {
    value = std::min(size_t(UINT16_MAX), size_t(state_->packetSize) * 1023);
    value -= value % std::max(size_t(state_->packetSize), size_t(64));
  }
  portEXIT_CRITICAL(&vendorLock);
  return value;
}
esp_err_t EspUsbDeviceVendor::onDirectRxComplete(DirectCompleteCallback callback) {
  if (state_) return ESP_ERR_INVALID_STATE;
  directRxCompleteCallback_ = callback;
  return ESP_OK;
}
EspUsbTransferSubmission EspUsbDeviceVendor::readDirect(void *buffer, size_t capacity) {
  auto error = EspUsbTransferError::None;
  if (!buffer || !capacity || capacity > UINT16_MAX || capacity % 64 || !directRxCompleteCallback_ ||
      reinterpret_cast<uintptr_t>(buffer) > UINTPTR_MAX - (capacity - 1)) error = EspUsbTransferError::BadArgument;
  else if (reinterpret_cast<uintptr_t>(buffer) & 63) error = EspUsbTransferError::NotAligned;
  else if (!esp_ptr_dma_capable(buffer) || !esp_ptr_dma_capable(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(buffer) + capacity - 1)))
    error = EspUsbTransferError::NotDmaCapable;
  portENTER_CRITICAL(&vendorLock);
  if (error == EspUsbTransferError::None) {
    if (!state_ || state_->stopping || !state_->epOut) error = EspUsbTransferError::NotMounted;
    else if (config_.rxMode != EspUsbVendorRxMode::Borrowed) error = EspUsbTransferError::NotSupported;
    else if (capacity % state_->packetSize || capacity > size_t(state_->packetSize) * 1023)
      error = EspUsbTransferError::BadArgument;
    else if (state_->directRxBuffer || state_->rxBusy || !usbd_edpt_claim(state_->rhport, state_->epOut))
      error = EspUsbTransferError::Busy;
    else {
      state_->directRxBuffer = buffer; state_->directRxCapacity = capacity; state_->rxBusy = true;
      if (!usbd_edpt_xfer(state_->rhport, state_->epOut, static_cast<uint8_t *>(buffer), uint16_t(capacity), false)) {
        state_->takeReceive(EspUsbTransferStatus::TransferError, 0, false);
        state_->rxBusy = false; usbd_edpt_release(state_->rhport, state_->epOut);
        error = EspUsbTransferError::TransferFailed;
      }
    }
  }
  portEXIT_CRITICAL(&vendorLock);
  return {error};
}
esp_err_t EspUsbDeviceVendor::onRx(RxCallback cb) {
  if (state_) return ESP_ERR_INVALID_STATE;
  rxCallback_ = cb;
  return ESP_OK;
}
esp_err_t EspUsbDeviceVendor::onRxData(RxDataCallback cb) {
  if (state_) return ESP_ERR_INVALID_STATE;
  rxDataCallback_ = cb;
  return ESP_OK;
}
esp_err_t EspUsbDeviceVendor::onTxComplete(TxCompleteCallback cb) {
  if (state_) return ESP_ERR_INVALID_STATE;
  txCompleteCallback_ = cb;
  return ESP_OK;
}
esp_err_t EspUsbDeviceVendor::onControlRequest(ControlRequestCallback cb) {
  if (state_) return ESP_ERR_INVALID_STATE;
  controlRequestCallback_ = cb;
  return ESP_OK;
}
void EspUsbDeviceVendor::handleRx(const uint8_t *data, size_t n) {
  if (data && rxDataCallback_) rxDataCallback_(data, n);
  if (rxCallback_) rxCallback_(available());
}
void EspUsbDeviceVendor::handleTxComplete(size_t n) { if (txCompleteCallback_) txCompleteCallback_(n); }
bool EspUsbDeviceVendor::onVendorControlRequest(uint8_t port, uint8_t stage, const void *request) {
  const auto *r = static_cast<const tusb_control_request_t *>(request);
  if (!r || (r->bmRequestType_bit.recipient == TUSB_REQ_RCPT_INTERFACE && uint8_t(r->wIndex) != interfaceNumber_)) return false;
  return handleControlRequest(port, stage, request);
}
bool EspUsbDeviceVendor::handleControlRequest(uint8_t port, uint8_t stage, const void *request) {
  const auto *r = static_cast<const tusb_control_request_t *>(request);
  if (!r || !controlRequestCallback_) return false;
  EspUsbDeviceVendorControlRequest event;
  event.rhport = port; event.stage = stage; event.bmRequestType = r->bmRequestType;
  event.bRequest = r->bRequest; event.wValue = r->wValue; event.wIndex = r->wIndex;
  event.wLength = r->wLength; event.rawRequest = r;
  return controlRequestCallback_(event);
}
bool EspUsbDeviceVendor::sendControlResponse(const EspUsbDeviceVendorControlRequest &request, const void *data, size_t n) {
  const auto *r = static_cast<const tusb_control_request_t *>(request.rawRequest);
  if (!r || n > UINT16_MAX) return false;
  if (!n) return tud_control_status(request.rhport, r);
  return data && tud_control_xfer(request.rhport, r, const_cast<void *>(data), uint16_t(n));
}
