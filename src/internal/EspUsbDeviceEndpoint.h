#pragma once

// Current C linkage bridge to the independent device core. This header exposes
// only the endpoint/control operations used by native classes, without the
// vendor class table, OS abstraction, FIFO or optional ISR dispatch API.
// Wire types and the usbd_* symbol names remain transitional.
#include "common/tusb_common.h"

extern "C" {
uint16_t usbd_control_received_bytes();
// Private capacity metadata used by core resource/fault probes.
uint16_t usbd_event_queue_capacity();
bool usbd_edpt_open(uint8_t port, const tusb_desc_endpoint_t *);
bool usbd_edpt_xfer(uint8_t port, uint8_t address, uint8_t *buffer, uint16_t bytes, bool inIsr);
bool usbd_edpt_claim(uint8_t port, uint8_t address);
bool usbd_edpt_release(uint8_t port, uint8_t address);
bool usbd_edpt_busy(uint8_t port, uint8_t address);
void usbd_edpt_stall(uint8_t port, uint8_t address);
void usbd_edpt_clear_stall(uint8_t port, uint8_t address);
bool usbd_edpt_stalled(uint8_t port, uint8_t address);
bool usbd_edpt_iso_alloc(uint8_t port, uint8_t address, uint16_t largestPacketBytes);
bool usbd_edpt_abort_supported();
bool usbd_edpt_abort(uint8_t port, uint8_t address);
// Nonblocking fence enqueue; false leaves ownership with the caller. Endpoint
// abort alone does not retire queued completions or return borrowed buffers.
bool usbd_try_defer_func(void (*job)(void *), void *context);
}
