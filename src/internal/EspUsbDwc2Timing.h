#pragma once
#include "EspUsbWire.h"

// USB 2.0 endpoint service intervals: FS uses frames, HS microframes.
// Bulk/control do not participate in ISO retry timing and need no shift.
static inline bool espusb_dwc2_decode_interval(const EspUsbEndpointDescriptor *endpoint,
    bool highSpeed, uint16_t *ticks) {
  const uint8_t type = espusb_endpoint_type(endpoint), encoded = endpoint->bInterval;
  if (type == ESPUSB_XFER_ISOCHRONOUS || (type == ESPUSB_XFER_INTERRUPT && highSpeed)) {
    if (!encoded || encoded > 16) return false;
    *ticks = (uint16_t)(1u << (encoded - 1));
  } else if (type == ESPUSB_XFER_INTERRUPT) {
    if (!encoded) return false;
    *ticks = encoded;
  } else {
    *ticks = 0;
  }
  return true;
}
