/*
 * Controller event layout adapted from TinyUSB device/dcd.h.
 * Copyright (c) 2019 Ha Thach (tinyusb.org), MIT License.
 * See third_party/tinyusb/LICENSE and PROVENANCE.md.
 */
#pragma once
#include "EspUsbWire.h"
#ifdef __cplusplus
extern "C" {
#endif

// ESP device controller boundary using native wire/descriptor types;
// device stack registration, FIFO and OS abstractions are not part of this API.
typedef enum {
  ESPUSB_CONTROLLER_EVENT_INVALID = 0,    // 0
  ESPUSB_CONTROLLER_EVENT_BUS_RESET,      // 1
  ESPUSB_CONTROLLER_EVENT_UNPLUGGED,      // 2
  ESPUSB_CONTROLLER_EVENT_SOF,            // 3
  ESPUSB_CONTROLLER_EVENT_SUSPEND,        // 4 TODO LPM Sleep L1 support
  ESPUSB_CONTROLLER_EVENT_RESUME,         // 5
  ESPUSB_CONTROLLER_EVENT_SETUP_RECEIVED, // 6
  ESPUSB_CONTROLLER_EVENT_XFER_COMPLETE,  // 7
  ESPUSB_CONTROLLER_EVENT_COUNT
} EspUsbControllerEventKind;

typedef struct __attribute__((aligned(4))) {
  uint8_t rhport;
  uint8_t event_id;

  union {
    // BUS RESET
    struct {
      EspUsbSpeed speed;
    } bus_reset;

    // SOF
    struct {
      uint32_t frame_count;
    }sof;

    // SETUP_RECEIVED
    EspUsbControlRequest setup_received;

    // XFER_COMPLETE
    struct {
      uint8_t  ep_addr;
      uint8_t  result;
      uint32_t len;
    }xfer_complete;

    // Core-only deferred payload; the controller never emits this member.
    struct {
      void (*func) (void* param);
      void* param;
    }func_call;
  };
} EspUsbControllerEvent;


typedef struct {
  uint16_t bulkInDoubleBuffered;
  bool vbusSensing;
} EspUsbControllerConfig;

// Configure before init; memory is prepared separately by the owning core.
bool espusb_controller_configure(uint8_t port, const EspUsbControllerConfig *);
bool espusb_controller_init(uint8_t port, bool highSpeed);
bool espusb_controller_deinit(uint8_t port);
void espusb_controller_int_handler(uint8_t port);
bool espusb_controller_int_enable(uint8_t port);
void espusb_controller_int_disable(uint8_t port);
void espusb_controller_set_address(uint8_t port, uint8_t address);
void espusb_controller_remote_wakeup(uint8_t port);
void espusb_controller_test_mode(uint8_t port, uint8_t selector);
void espusb_controller_connect(uint8_t port);
void espusb_controller_disconnect(uint8_t port);
void espusb_controller_sof_enable(uint8_t port, bool enabled);
bool espusb_controller_edpt_open(uint8_t port, const EspUsbEndpointDescriptor *);
// Return only after non-control DMA and endpoint IRQ state are quiescent.
void espusb_controller_edpt_close_all(uint8_t port);
bool espusb_controller_edpt_xfer(uint8_t port, uint8_t address, uint8_t *, uint32_t bytes, bool inIsr);
bool espusb_controller_edpt_abort_supported(void);
bool espusb_controller_edpt_abort(uint8_t port, uint8_t address);
void espusb_controller_edpt_stall(uint8_t port, uint8_t address);
void espusb_controller_edpt_clear_stall(uint8_t port, uint8_t address);
bool espusb_controller_edpt_iso_alloc(uint8_t port, uint8_t address, uint16_t largestPacketBytes);
bool espusb_controller_edpt_iso_activate(uint8_t port, const EspUsbEndpointDescriptor *);

bool espusb_controller_dcache_clean(const void *, uint32_t bytes);
bool espusb_controller_dcache_invalidate(const void *, uint32_t bytes);
bool espusb_controller_dcache_clean_invalidate(const void *, uint32_t bytes);
// Same critical section on the worker/sketch and in the controller ISR.
void espusb_controller_lock(bool inIsr);
void espusb_controller_unlock(bool inIsr);
// Reset starts before link speed is known. Invalidate old core work while the
// controller lock is held; driver cleanup still follows the final reset event.
void espusb_controller_reset_begin(uint8_t port);
// Read under that same lock before opening an endpoint or publishing DMA.
bool espusb_controller_io_ready(uint8_t port);
void espusb_controller_event(const EspUsbControllerEvent *, bool inIsr);
// helper to send bus signal event
static inline void espusb_controller_event_bus_signal (uint8_t rhport, EspUsbControllerEventKind eid, bool in_isr) {
  EspUsbControllerEvent event = {0};
  event.rhport = rhport;
  event.event_id = eid;
  espusb_controller_event(&event, in_isr);
}

// helper to send bus reset event
static inline  void espusb_controller_event_bus_reset (uint8_t rhport, EspUsbSpeed speed, bool in_isr) {
  EspUsbControllerEvent event = {0};
  event.rhport = rhport;
  event.event_id = ESPUSB_CONTROLLER_EVENT_BUS_RESET;
  event.bus_reset.speed = speed;
  espusb_controller_event(&event, in_isr);
}

// helper to send setup received
static inline void espusb_controller_event_setup_received(uint8_t rhport, uint8_t const * setup, bool in_isr) {
  EspUsbControllerEvent event = {0};
  event.rhport = rhport;
  event.event_id = ESPUSB_CONTROLLER_EVENT_SETUP_RECEIVED;
  event.setup_received = espusb_decode_setup(setup);
  espusb_controller_event(&event, in_isr);
}

// helper to send transfer complete event
static inline void espusb_controller_event_xfer_complete (uint8_t rhport, uint8_t ep_addr, uint32_t xferred_bytes, uint8_t result, bool in_isr) {
  EspUsbControllerEvent event = {0};
  event.rhport = rhport;
  event.event_id = ESPUSB_CONTROLLER_EVENT_XFER_COMPLETE;
  event.xfer_complete.ep_addr = ep_addr;
  event.xfer_complete.len     = xferred_bytes;
  event.xfer_complete.result  = result;
  espusb_controller_event(&event, in_isr);
}

static inline void espusb_controller_event_sof(uint8_t rhport, uint32_t frame_count, bool in_isr) {
  EspUsbControllerEvent event = {0};
  event.rhport = rhport;
  event.event_id = ESPUSB_CONTROLLER_EVENT_SOF;
  event.sof.frame_count = frame_count;
  espusb_controller_event(&event, in_isr);
}

#ifdef __cplusplus
}
#endif
