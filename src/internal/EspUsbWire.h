#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Native USB 2.0 Chapter 9 layouts. No stack configuration, MCU, OS, FIFO or
// compiler abstraction is imported here. Multibyte descriptor fields stay in
// wire byte order; ControlRequest fields are decoded before dispatch.
typedef enum {
  ESPUSB_SPEED_FULL = 0, ESPUSB_SPEED_LOW = 1, ESPUSB_SPEED_HIGH = 2,
  ESPUSB_SPEED_INVALID = 255
} EspUsbSpeed;
typedef enum {
  ESPUSB_TRANSFER_SUCCESS = 0, ESPUSB_TRANSFER_FAILED = 1,
  ESPUSB_TRANSFER_STALLED = 2, ESPUSB_TRANSFER_TIMEOUT = 3,
  ESPUSB_TRANSFER_ABORTED = 4, ESPUSB_TRANSFER_INVALID = 5
} EspUsbControllerResult;
enum {
  ESPUSB_CONTROL_STAGE_SETUP = 1, ESPUSB_CONTROL_STAGE_DATA = 2, ESPUSB_CONTROL_STAGE_ACK = 3,
  ESPUSB_XFER_CONTROL = 0, ESPUSB_XFER_ISOCHRONOUS = 1, ESPUSB_XFER_BULK = 2, ESPUSB_XFER_INTERRUPT = 3,
  ESPUSB_DIR_OUT = 0, ESPUSB_DIR_IN = 1, ESPUSB_DIR_IN_MASK = 128,
  ESPUSB_DESC_INTERFACE = 4, ESPUSB_DESC_ENDPOINT = 5,
  ESPUSB_CLASS_CDC = 2, ESPUSB_CLASS_CDC_DATA = 10, ESPUSB_CLASS_VENDOR_SPECIFIC = 255,
  ESPUSB_REQ_TYPE_CLASS = 1, ESPUSB_REQ_RCPT_INTERFACE = 1,
  // The current FS/HS engine uses 64-byte EP0 packets, independent of data MPS.
  ESPUSB_CONTROL_PACKET_BYTES = 64
};

typedef struct __attribute__((packed)) {
  uint8_t bmRequestType, bRequest;
  uint16_t wValue, wIndex, wLength;
} EspUsbControlRequest;
typedef struct __attribute__((packed)) {
  uint8_t bLength, bDescriptorType, bInterfaceNumber, bAlternateSetting;
  uint8_t bNumEndpoints, bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol, iInterface;
} EspUsbInterfaceDescriptor;
typedef struct __attribute__((packed)) {
  uint8_t bLength, bDescriptorType, bEndpointAddress, bmAttributes;
  uint16_t wMaxPacketSize;
  uint8_t bInterval;
} EspUsbEndpointDescriptor;

#ifdef __cplusplus
static_assert(sizeof(EspUsbControlRequest) == 8 && offsetof(EspUsbControlRequest, wValue) == 2, "SETUP layout");
static_assert(sizeof(EspUsbInterfaceDescriptor) == 9, "interface layout");
static_assert(sizeof(EspUsbEndpointDescriptor) == 7 && offsetof(EspUsbEndpointDescriptor, wMaxPacketSize) == 4, "endpoint layout");
#else
_Static_assert(sizeof(EspUsbControlRequest) == 8 && offsetof(EspUsbControlRequest, wValue) == 2, "SETUP layout");
_Static_assert(sizeof(EspUsbInterfaceDescriptor) == 9, "interface layout");
_Static_assert(sizeof(EspUsbEndpointDescriptor) == 7 && offsetof(EspUsbEndpointDescriptor, wMaxPacketSize) == 4, "endpoint layout");
#endif

static inline uint16_t espusb_read_u16(const void *address) {
  const uint8_t *p = (const uint8_t *)address;
  return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
static inline uint8_t espusb_endpoint_number(uint8_t address) { return address & 15; }
static inline uint8_t espusb_endpoint_direction(uint8_t address) { return (address >> 7) & 1; }
static inline uint8_t espusb_endpoint_address(uint8_t number, uint8_t direction) {
  return (uint8_t)(number | (direction == ESPUSB_DIR_IN ? ESPUSB_DIR_IN_MASK : 0));
}
static inline uint8_t espusb_endpoint_type(const EspUsbEndpointDescriptor *descriptor) {
  return descriptor->bmAttributes & 3;
}
static inline uint16_t espusb_endpoint_packet_size(const EspUsbEndpointDescriptor *descriptor) {
  return espusb_read_u16((const uint8_t *)descriptor + 4) & 0x7ff;
}
static inline uint8_t espusb_request_type(const EspUsbControlRequest *request) { return (request->bmRequestType >> 5) & 3; }
static inline uint8_t espusb_request_recipient(const EspUsbControlRequest *request) { return request->bmRequestType & 31; }
static inline EspUsbControlRequest espusb_decode_setup(const uint8_t *bytes) {
  EspUsbControlRequest request;
  request.bmRequestType = bytes[0]; request.bRequest = bytes[1];
  request.wValue = espusb_read_u16(bytes + 2);
  request.wIndex = espusb_read_u16(bytes + 4);
  request.wLength = espusb_read_u16(bytes + 6);
  return request;
}
