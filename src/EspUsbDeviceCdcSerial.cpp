#include "EspUsbDevice.h"
#include "internal/EspUsbMemory.h"
#include "internal/EspUsbDeviceCore.h"
#include "internal/EspUsbDeviceBus.h"
#include "internal/EspUsbDeviceEndpoint.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <utility>

namespace {
portMUX_TYPE cdcLock = portMUX_INITIALIZER_UNLOCKED;
uint32_t read32(const uint8_t *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
bool validCoding(const uint8_t *p) {
  return read32(p) && p[4] <= 2 && p[5] <= 4 && ((p[6] >= 5 && p[6] <= 8) || p[6] == 16);
}
// USB CDC ACM request selectors.
enum CdcRequest : uint8_t {
  SetLineCoding = 0x20, GetLineCoding = 0x21,
  SetControlLineState = 0x22, SendBreak = 0x23,
};
constexpr uint8_t AcmSubclass = 2;
}

EspUsbDeviceCdcSerial::EspUsbDeviceCdcSerial(EspUsbDevice &device, const char *name)
    : EspUsbDeviceBulkStream(device, 512), name_(name) {}
EspUsbDeviceCdcSerial::~EspUsbDeviceCdcSerial() { end(); }
bool EspUsbDeviceCdcSerial::begin() {
  if (state_ || controlBuffer_) { startError_ = ESP_ERR_INVALID_STATE; return false; }
  controlBuffer_ = static_cast<uint8_t *>(espusb::internal::allocateUsbMemory(64, 64, true));
  if (!controlBuffer_) { startError_ = ESP_ERR_NO_MEM; return false; }
  if (!EspUsbDeviceBulkStream::begin()) {
    espusb::internal::freeUsbMemory(controlBuffer_); controlBuffer_ = nullptr; return false;
  }
  return true;
}
void EspUsbDeviceCdcSerial::end() {
  EspUsbDeviceBulkStream::end();
  espusb::internal::freeUsbMemory(controlBuffer_); controlBuffer_ = nullptr;
}
void EspUsbDeviceCdcSerial::assignFunctionIds(uint8_t instance, uint8_t stringIndex) { instance_ = instance; stringIndex_ = stringIndex; }
esp_err_t EspUsbDeviceCdcSerial::setName(const char *name) {
  if (state_) return ESP_ERR_INVALID_STATE;
  name_ = name; return ESP_OK;
}
uint16_t EspUsbDeviceCdcSerial::configurationDescriptor(uint8_t *dst, uint8_t itf, uint8_t ep, uint16_t ceiling) {
  if (!ep || ep > 14) return 0;
  interfaceNumber_ = itf;
  const uint16_t packet = ceiling < endpointSize_ ? ceiling : endpointSize_;
  // IAD, ACM control interface and functional descriptors, notification IN,
  // then the data interface and bulk pair. Lengths/IDs come from this Function.
  const uint8_t dataInterface = uint8_t(itf + 1), dataEndpoint = uint8_t(ep + 1);
  const uint8_t bytes[] = {
    8, 11, itf, 2, 2, AcmSubclass, 0, stringIndex_,
    9, 4, itf, 0, 1, 2, AcmSubclass, 0, stringIndex_,
    5, 0x24, 0, 0x20, 0x01,
    5, 0x24, 1, 0, dataInterface,
    4, 0x24, 2, 6,
    5, 0x24, 6, itf, dataInterface,
    7, 5, uint8_t(0x80 | ep), 3, 8, 0, 1,
    9, 4, dataInterface, 0, 2, 10, 0, 0, 0,
    7, 5, dataEndpoint, 2, uint8_t(packet), uint8_t(packet >> 8), 0,
    7, 5, uint8_t(0x80 | dataEndpoint), 2, uint8_t(packet), uint8_t(packet >> 8), 0,
  };
  if (dst) memcpy(dst, bytes, sizeof(bytes));
  return sizeof(bytes);
}
uint16_t EspUsbDeviceCdcSerial::openBulkInterface(uint8_t port, const uint8_t *d, uint16_t length,
    uint8_t &in, uint8_t &out, uint16_t &mps) {
  const auto *control = reinterpret_cast<const tusb_desc_interface_t *>(d);
  if (length < sizeof(*control) || control->bInterfaceClass != TUSB_CLASS_CDC ||
      control->bInterfaceSubClass != AcmSubclass || control->bNumEndpoints != 1) return 0;
  size_t offset = sizeof(*control);
  const tusb_desc_endpoint_t *notify = nullptr;
  while (offset + 2 <= length) {
    const uint8_t bytes = d[offset];
    if (bytes < 2 || offset + bytes > length) return 0;
    if (d[offset + 1] == TUSB_DESC_INTERFACE) break;
    if (d[offset + 1] == TUSB_DESC_ENDPOINT) {
      if (notify || bytes != sizeof(*notify)) return 0;
      notify = reinterpret_cast<const tusb_desc_endpoint_t *>(d + offset);
    }
    offset += bytes;
  }
  if (!notify || offset + sizeof(tusb_desc_interface_t) + 2 * sizeof(tusb_desc_endpoint_t) > length ||
      notify->bmAttributes.xfer != TUSB_XFER_INTERRUPT || !(notify->bEndpointAddress & 0x80) || tu_edpt_packet_size(notify) != 8) return 0;
  const auto *data = reinterpret_cast<const tusb_desc_interface_t *>(d + offset);
  if (data->bLength != sizeof(*data) || data->bInterfaceClass != TUSB_CLASS_CDC_DATA ||
      data->bInterfaceNumber != interfaceNumber_ + 1 || data->bNumEndpoints != 2) return 0;
  offset += sizeof(*data);
  if (!openBulkPair(port, d + offset, in, out, mps) || !espusb_device_edpt_open(port, notify)) return 0;
  notifyEndpoint_ = notify->bEndpointAddress;
  return offset + 2 * sizeof(tusb_desc_endpoint_t);
}
EspUsbDeviceCdcLineCoding EspUsbDeviceCdcSerial::lineCoding() const {
  portENTER_CRITICAL(&cdcLock); const auto value = lineCoding_; portEXIT_CRITICAL(&cdcLock); return value;
}
EspUsbDeviceCdcLineState EspUsbDeviceCdcSerial::lineState() const {
  portENTER_CRITICAL(&cdcLock); const auto value = lineState_; portEXIT_CRITICAL(&cdcLock); return value;
}
bool EspUsbDeviceCdcSerial::connected() const { return mounted() && lineState().dtr; }
esp_err_t EspUsbDeviceCdcSerial::onLineCoding(LineCodingCallback callback) {
  if (state_) return ESP_ERR_INVALID_STATE;
  lineCodingCallback_ = std::move(callback); return ESP_OK;
}
esp_err_t EspUsbDeviceCdcSerial::onLineState(LineStateCallback callback) {
  if (state_) return ESP_ERR_INVALID_STATE;
  lineStateCallback_ = std::move(callback); return ESP_OK;
}
esp_err_t EspUsbDeviceCdcSerial::onBreak(BreakCallback callback) {
  if (state_) return ESP_ERR_INVALID_STATE;
  breakCallback_ = std::move(callback); return ESP_OK;
}
void EspUsbDeviceCdcSerial::onBulkReset() {
  notifyEndpoint_ = 0;
  portENTER_CRITICAL(&cdcLock);
  const bool changed = lineState_.dtr || lineState_.rts;
  lineState_ = {};
  portEXIT_CRITICAL(&cdcLock);
  if (changed && lineStateCallback_) { const EspUsbDeviceCdcLineState state; lineStateCallback_(state); }
}
bool EspUsbDeviceCdcSerial::onBulkExtraCompletion(uint8_t ep, uint8_t, uint32_t) { return notifyEndpoint_ && ep == notifyEndpoint_; }
bool EspUsbDeviceCdcSerial::onBulkControl(uint8_t port, uint8_t stage, const void *raw) {
  const auto *r = static_cast<const tusb_control_request_t *>(raw);
  if (r && r->bmRequestType == 1 && r->bRequest == 11 && r->wIndex == interfaceNumber_ && !r->wValue && !r->wLength) {
    if (stage != CONTROL_STAGE_SETUP) return true;
    // This implementation never posts serial-state notifications. The control
    // interface therefore has no application loan or completion to fence.
    if (!notifyEndpoint_ || !espusb_device_edpt_abort(port, notifyEndpoint_)) return espusb::internal::rejectUsbControl();
    espusb_device_edpt_clear_stall(port, notifyEndpoint_);
    return espusb_device_control_status(port, r);
  }
  if (!r || !controlBuffer_ || r->bmRequestType_bit.type != TUSB_REQ_TYPE_CLASS ||
      r->bmRequestType_bit.recipient != TUSB_REQ_RCPT_INTERFACE || r->wIndex != interfaceNumber_) return false;
  switch (r->bRequest) {
  case GetLineCoding:
    if (r->bmRequestType != 0xa1 || r->wValue || r->wLength != 7) return false;
    if (stage == CONTROL_STAGE_SETUP) {
      const auto coding = lineCoding();
      for (unsigned i = 0; i < 4; ++i) controlBuffer_[i] = uint8_t(coding.baud >> (i * 8));
      controlBuffer_[4] = coding.stopBits; controlBuffer_[5] = coding.parity; controlBuffer_[6] = coding.dataBits;
      return espusb_device_control_transfer(port, r, controlBuffer_, 7);
    }
    return true;
  case SetLineCoding:
    if (r->bmRequestType != 0x21 || r->wValue || r->wLength != 7) return false;
    if (stage == CONTROL_STAGE_SETUP) return espusb_device_control_transfer(port, r, controlBuffer_, 7);
    if (stage == CONTROL_STAGE_DATA) return espusb_device_control_received_bytes() == 7 && validCoding(controlBuffer_);
    if (stage == CONTROL_STAGE_ACK) {
      const EspUsbDeviceCdcLineCoding coding{read32(controlBuffer_), controlBuffer_[4], controlBuffer_[5], controlBuffer_[6]};
      portENTER_CRITICAL(&cdcLock); lineCoding_ = coding; portEXIT_CRITICAL(&cdcLock);
      if (lineCodingCallback_) lineCodingCallback_(coding);
    }
    return true;
  case SetControlLineState:
    if (r->bmRequestType != 0x21 || r->wLength || r->wValue > 3) return false;
    if (stage == CONTROL_STAGE_SETUP) return espusb_device_control_status(port, r);
    if (stage == CONTROL_STAGE_ACK) {
      const EspUsbDeviceCdcLineState state{bool(r->wValue & 1), bool(r->wValue & 2)};
      portENTER_CRITICAL(&cdcLock); lineState_ = state; portEXIT_CRITICAL(&cdcLock);
      if (lineStateCallback_) lineStateCallback_(state);
    }
    return true;
  case SendBreak:
    if (r->bmRequestType != 0x21 || r->wLength) return false;
    if (stage == CONTROL_STAGE_SETUP) return espusb_device_control_status(port, r);
    if (stage == CONTROL_STAGE_ACK && breakCallback_) breakCallback_(r->wValue);
    return true;
  default: return false;
  }
}
