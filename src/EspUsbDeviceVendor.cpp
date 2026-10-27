#include "EspUsbDevice.h"
#include "internal/EspUsbDeviceCore.h"
#include "internal/EspUsbDeviceBus.h"
#include <algorithm>
#include <string.h>

EspUsbDeviceVendor::EspUsbDeviceVendor(EspUsbDevice &device, uint16_t size)
    : EspUsbDeviceBulkStream(device, size) {}
EspUsbDeviceVendor::~EspUsbDeviceVendor() { end(); }
uint16_t EspUsbDeviceVendor::openBulkInterface(uint8_t port, const uint8_t *descriptor, uint16_t length,
    uint8_t &in, uint8_t &out, uint16_t &mps) {
  const auto *itf = reinterpret_cast<const tusb_desc_interface_t *>(descriptor);
  const uint16_t required = sizeof(*itf) + 2 * sizeof(tusb_desc_endpoint_t);
  if (length < required || itf->bInterfaceClass != TUSB_CLASS_VENDOR_SPECIFIC || itf->bNumEndpoints != 2) return 0;
  return openBulkPair(port, descriptor + sizeof(*itf), in, out, mps) ? required : 0;
}

uint16_t EspUsbDeviceVendor::configurationDescriptor(uint8_t *dst, uint8_t itf, uint8_t ep, uint16_t ceiling) {
  interfaceNumber_ = itf;
  // HS bulk endpoints use 512 bytes; endpointSize_ selects the FS size.
  const uint16_t packet = ceiling > 64 ? 512 : ceiling ? std::min(endpointSize_, ceiling) : endpointSize_;
  const uint8_t descriptor[] = {
      9, TUSB_DESC_INTERFACE, itf, 0, 2, TUSB_CLASS_VENDOR_SPECIFIC, 0, 0, 0,
      7, TUSB_DESC_ENDPOINT, ep, TUSB_XFER_BULK, uint8_t(packet), uint8_t(packet >> 8), 0,
      7, TUSB_DESC_ENDPOINT, uint8_t(0x80 | ep), TUSB_XFER_BULK, uint8_t(packet), uint8_t(packet >> 8), 0,
  };
  if (dst) memcpy(dst, descriptor, sizeof(descriptor)); return sizeof(descriptor);
}

esp_err_t EspUsbDeviceVendor::onControlRequest(ControlRequestCallback cb) {
  if (state_) return ESP_ERR_INVALID_STATE;
  controlRequestCallback_ = cb;
  return ESP_OK;
}

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
  event.wLength = r->wLength; event.actualBytes = espusb::internal::usbControlTransferredBytes(); event.rawRequest = r;
  return controlRequestCallback_(event);
}

bool EspUsbDeviceVendor::sendControlResponse(const EspUsbDeviceVendorControlRequest &request, const void *data, size_t n) {
  const auto *r = static_cast<const tusb_control_request_t *>(request.rawRequest);
  if (!r || n > UINT16_MAX) return false;
  if (!r->wLength) return espusb_device_control_status(request.rhport, r);
  return (!n || data) && espusb_device_control_transfer(request.rhport, r, const_cast<void *>(data), uint16_t(n));
}
