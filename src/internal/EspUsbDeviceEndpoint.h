#pragma once

// Current C linkage bridge to the independent device core. This header exposes
// only the endpoint/control operations used by native classes, without the
// vendor class table, OS abstraction, FIFO or optional ISR dispatch API.
// Native wire types and entry points are owned by this device engine.
#include "EspUsbWire.h"

extern "C" {
uint16_t espusb_device_control_received_bytes();
// Private capacity metadata used by core resource/fault probes.
uint16_t espusb_device_queue_capacity();
bool espusb_device_edpt_open(uint8_t port, const EspUsbEndpointDescriptor *);
bool espusb_device_edpt_xfer(uint8_t port, uint8_t address, uint8_t *buffer, uint32_t bytes, bool inIsr);
bool espusb_device_edpt_claim(uint8_t port, uint8_t address);
bool espusb_device_edpt_release(uint8_t port, uint8_t address);
bool espusb_device_edpt_busy(uint8_t port, uint8_t address);
void espusb_device_edpt_stall(uint8_t port, uint8_t address);
void espusb_device_edpt_clear_stall(uint8_t port, uint8_t address);
bool espusb_device_edpt_stalled(uint8_t port, uint8_t address);
bool espusb_device_edpt_abort_supported();
bool espusb_device_edpt_abort(uint8_t port, uint8_t address);
// Nonblocking fence enqueue; false leaves ownership with the caller. Endpoint
// abort alone does not retire queued completions or return borrowed buffers.
bool espusb_device_try_defer(void (*job)(void *), void *context);
}
