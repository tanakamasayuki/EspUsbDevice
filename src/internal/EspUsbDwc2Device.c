/*
 * SPDX-FileCopyrightText: Copyright (c) 2019 William D. Jones
 * SPDX-FileCopyrightText: Copyright (c) 2019 Ha Thach (tinyusb.org)
 * SPDX-FileCopyrightText: Copyright (c) 2020 Jan Duempelmann
 * SPDX-FileCopyrightText: Copyright (c) 2020 Reinhard Panhuber
 * SPDX-License-Identifier: MIT
 *
 * Native ESP device DMA engine, adapted from TinyUSB DWC2.
 * Independently maintained with core-owned storage and native class/IRQ APIs.
 * Origin: third_party/tinyusb/ORIGIN.json; license retained there.
 */

#include "EspUsbDwc2Support.h"
#include "EspUsbLegacyFlags.h"

#include "EspUsbController.h"
#include "EspUsbDwc2Hardware.h"
#include "EspUsbDwc2Timing.h"
#include "EspUsbDwc2Runtime.h"

//--------------------------------------------------------------------+
// MACRO TYPEDEF CONSTANT ENUM
//--------------------------------------------------------------------+
typedef struct {
  uint8_t* buffer;
  uint32_t total_len;
  uint32_t dma_done;
  uint16_t max_size, dma_chunk; // One bounded hardware arm; one final event.
  uint16_t interval;
  uint16_t iso_retry; // ISO retry counter
} EspUsbDwc2Transfer;
_Static_assert(sizeof(void*) != 4 || sizeof(EspUsbDwc2Transfer) == 20, "32-bit transfer record");

typedef struct {
  // EP0 transfers are limited to 1 packet - larger sizes has to be split
  uint16_t ep0_pending[2];  // Indexed by native OUT/IN direction
  uint16_t dfifo_top;      // top free location in DFIFO in words

  // Number of IN endpoints active
  uint8_t allocated_epin_count;

  // SOF enabling flag - required for SOF to not get disabled in ISR when SOF was enabled by
  bool sof_en;
} EspUsbDwc2ControllerState;

// EspUsb owns this storage for exactly one active controller. Transfer records
// are compact: OUT records followed by IN records, selected by descriptor masks.
// SETUP DMA occupies its own full cache line after all metadata.
typedef struct {
  EspUsbDwc2ControllerState controller;
  uint8_t *setup_buffer;
  size_t bytes;
  uint16_t mask[2];
  uint8_t count[2], ep_count, port;
  EspUsbDwc2Transfer transfers[];
} EspUsbDwc2RuntimeState;
static EspUsbDwc2RuntimeState *runtime;
#define DWC2_SETUP_STORAGE_BYTES 64u

static inline unsigned selected_count(uint16_t mask) { return (unsigned)__builtin_popcount((unsigned)mask); }
// Dedicated TX FIFOs are a compact hardware resource, not USB endpoint IDs.
// Keep EP0 in FIFO 0 and assign selected IN endpoints by rank, including gaps
// introduced by OUT-only endpoints or Function endpoint-number reservations.
static inline uint8_t selected_tx_fifo(uint8_t ep) {
  return (uint8_t) selected_count((uint16_t)(runtime->mask[ESPUSB_DIR_IN] & ((1u << ep) - 1u)));
}
static inline EspUsbDwc2Transfer *selected_transfer(uint8_t ep, uint8_t dir) {
  if (!runtime || dir > 1 || ep >= runtime->ep_count || !(runtime->mask[dir] & (1u << ep))) return NULL;
  const unsigned before = selected_count((uint16_t)(runtime->mask[dir] & ((1u << ep) - 1u)));
  return runtime->transfers + (dir == ESPUSB_DIR_IN ? runtime->count[ESPUSB_DIR_OUT] : 0) + before;
}

size_t espusb_dwc2_runtime_size(uint8_t port, uint16_t out_mask, uint16_t in_mask) {
  if (port >= ESPUSB_DWC2_PORT_COUNT) return 0;
  const uint8_t count = espusb_dwc2_port(port)->ep_count;
  if (!count || count > 16 || !(out_mask & 1u) || !(in_mask & 1u)) return 0;
  const uint32_t allowed = (1u << count) - 1u;
  if ((out_mask | in_mask) & ~allowed) return 0;
  const size_t metadata = sizeof(EspUsbDwc2RuntimeState) + (selected_count(out_mask) + selected_count(in_mask)) * sizeof(EspUsbDwc2Transfer);
  return ((metadata + 63u) & ~(size_t)63u) + DWC2_SETUP_STORAGE_BYTES;
}
bool espusb_dwc2_runtime_prepare(uint8_t port, void *memory, size_t bytes, uint16_t out_mask, uint16_t in_mask) {
  const size_t required = espusb_dwc2_runtime_size(port, out_mask, in_mask);
  if (runtime || !required || !memory || ((uintptr_t)memory & 63u) || bytes < required) return false;
  memset(memory, 0, required);
  EspUsbDwc2RuntimeState *state = (EspUsbDwc2RuntimeState *)memory;
  state->mask[ESPUSB_DIR_OUT] = out_mask; state->mask[ESPUSB_DIR_IN] = in_mask;
  state->count[ESPUSB_DIR_OUT] = selected_count(out_mask); state->count[ESPUSB_DIR_IN] = selected_count(in_mask);
  state->ep_count = espusb_dwc2_port(port)->ep_count; state->port = port;
  state->bytes = required; state->setup_buffer = (uint8_t *)memory + required - DWC2_SETUP_STORAGE_BYTES;
  // Initialization dirtied this cache line. Hand it to DMA before controller
  // initialization; otherwise a delayed writeback could overwrite a SETUP.
  // Keep direction operations separate and publish storage only on success.
  if (!espusb_controller_dcache_clean(state->setup_buffer, DWC2_SETUP_STORAGE_BYTES) ||
      !espusb_controller_dcache_invalidate(state->setup_buffer, DWC2_SETUP_STORAGE_BYTES)) return false;
  runtime = state;
  return true;
}
void espusb_dwc2_runtime_release(void) { runtime = NULL; }
size_t espusb_dwc2_runtime_bytes(void) { return runtime ? runtime->bytes : 0; }
uint8_t espusb_dwc2_runtime_transfers(void) { return runtime ? runtime->count[0] + runtime->count[1] : 0; }

static EspUsbControllerConfig controller_config = {0, false};

static inline uint8_t dwc2_ep_count(const EspUsbDwc2Registers* dwc2) {
  const EspUsbDwc2Ghwcfg2 ghwcfg2 = {.value = dwc2->ghwcfg2};
  return ghwcfg2.num_dev_ep + 1;
}

//--------------------------------------------------------------------+
//
//--------------------------------------------------------------------+
static inline bool edpt_is_enabled(EspUsbDwc2EndpointRegisters* dep) {
  return (dep->ctl & EPCTL_EPENA) != 0;
}

  //--------------------------------------------------------------------
  // DMA
  //--------------------------------------------------------------------

static inline bool dma_device_enabled(const EspUsbDwc2Registers* dwc2) {
  (void) dwc2;
  // Internal DMA only
  const EspUsbDwc2Ghwcfg2 ghwcfg2 = {.value = dwc2->ghwcfg2};
  return ghwcfg2.arch == GHWCFG2_ARCH_INTERNAL_DMA;
}

static void dma_setup_prepare(uint8_t rhport) {
  EspUsbDwc2Registers* dwc2 = espusb_dwc2_registers(rhport);

  if (dwc2->gsnpsid >= DWC2_CORE_REV_3_00a) {
    if(edpt_is_enabled(&dwc2->epout[0])) {
      return;
    }
  }

  // Receive back-to-back setup packets
  dwc2->epout[0].doeptsiz = (3 << DOEPTSIZ_STUPCNT_Pos);
  dwc2->epout[0].doepdma = (uintptr_t) runtime->setup_buffer;
  dwc2->epout[0].doepctl |= DOEPCTL_EPENA | DOEPCTL_USBAEP;
}

//--------------------------------------------------------------------+
// Data FIFO
//--------------------------------------------------------------------+

/* Device Data FIFO scheme
  The controller has a single SPRAM of otg_dfifo_depth 32-bit words shared between all FIFOs and optional DMA metadata.
  otg_dfifo_depth = ghwcfg3.dfifo_depth + EP_LOC_CNT. It is split up into:

  - EPInfo: for storing DMA address registers (DxEPDMAn), only required when DMA is used.
    gdfifocfg.EPINFOBASE and gdfifocfg.GDFIFOCfg must be configured before gahbcfg.dmaen is set.
    The number of words needed per endpoint direction depends on the DMA mode used at runtime:
      - Buffer DMA mode: 1 word per endpoint direction
      - Scatter/Gather DMA mode: 4 words per endpoint direction
  - TX FIFO: one fifo for each IN endpoint. Size is dynamic depending on packet size, starting from top with EP0 IN.
  - Shared RX FIFO: a shared fifo for all OUT endpoints. Typically, can hold up to 2 packets of the largest EP size.

  We allocate TX FIFOs from top to bottom (using a top pointer), this to allow the RX FIFO to grow dynamically, which is
  possible since the free space is located between the RX and TX FIFOs.

   --------------- otg_dfifo_depth
  |  EPInfo      |   DxEPDMAn (DMA only, sized per runtime DMA mode)
  |-------------|-- gdfifocfg.EPINFOBASE (start of EPInfo; FIFO space sized by GDFIFOCFG)
  | IN FIFO 0   |       control EP
  |-------------|
  | IN FIFO 1   |
  |-------------|
  |   . . . .   |
  |-------------|
  | IN FIFO n   |
  |-------------|
  |    FREE     |
  |-------------|-- GRXFSIZ (expandable)
  |  OUT FIFO   |
  | ( Shared )  |
  --------------- 0

  According to "FIFO RAM allocation" section in RM, FIFO RAM are allocated as follows (each word 32-bits):
  - Each EP IN needs at least max packet size
  - All EP OUT shared a unique OUT FIFO which uses (for Slave or Buffer DMA, Scatt/Gather DMA use different formula):
    - 13 for setup packets + control words (up to 3 setup packets).
    - 1 for global NAK (not required/used here).
    - Largest-EPsize/4 + 1. (FS: 64 bytes, HS: 512 bytes). Recommended is  "2 x (Largest-EPsize/4 + 1)"
    - 2 for each used OUT endpoint.

    Therefore, GRXFSIZ = 13 + 1 + 2 x (Largest-EPsize/4 + 1) + 2 x EPOUTnum
*/

static inline uint16_t calc_device_grxfsiz(uint16_t largest_ep_size, uint8_t ep_count) {
  return (uint16_t)(13 + 1 + 2 * ((largest_ep_size / 4) + 1) + 2 * ep_count);
}

static bool dfifo_alloc(uint8_t rhport, uint8_t ep_addr, uint16_t packet_size, bool is_bulk) {
  EspUsbDwc2Registers* dwc2 = espusb_dwc2_registers(rhport);
  const EspUsbDwc2Port* dwc2_controller = espusb_dwc2_port(rhport);
  const uint8_t ep_count = dwc2_controller->ep_count;
  const uint8_t epnum = espusb_endpoint_number(ep_addr);
  const uint8_t dir = espusb_endpoint_direction(ep_addr);

  ESPUSB_REQUIRE(epnum < ep_count);

  uint16_t fifo_size = (uint16_t)espusb_div_ceil(packet_size, 4);
  if (dir == ESPUSB_DIR_OUT) {
    // Calculate required size of RX FIFO
    const uint16_t new_sz = calc_device_grxfsiz(4 * fifo_size, ep_count);

    // If size_rx needs to be extended check if there is enough free space
    if (dwc2->grxfsiz < new_sz) {
      ESPUSB_REQUIRE(new_sz <= runtime->controller.dfifo_top);
      dwc2->grxfsiz = new_sz; // Enlarge RX FIFO
    }
  } else {
    const uint8_t fifo = selected_tx_fifo(epnum);
    // Check IN endpoints concurrently active limit
    if(0 != dwc2_controller->ep_in_count) {
      ESPUSB_REQUIRE(fifo < dwc2_controller->ep_in_count);
      ESPUSB_REQUIRE(runtime->controller.allocated_epin_count < dwc2_controller->ep_in_count);
      runtime->controller.allocated_epin_count++;
    }

    // Enable double buffering if configured, only effective for non-periodic endpoints
    // Since we queue only 1 control transfer at a time, it's only applicable for bulk IN endpoints
    if (((controller_config.bulkInDoubleBuffered & (1 << epnum)) != 0) && epnum > 0 && is_bulk) {
      fifo_size *= 2;
    }

    // Check if free space is available
    ESPUSB_REQUIRE(runtime->controller.dfifo_top >= fifo_size + dwc2->grxfsiz);
    runtime->controller.dfifo_top -= fifo_size;

    // Both TXFD and TXSA are in unit of 32-bit words.
    if (epnum == 0) {
      dwc2->dieptxf0 = ((uint32_t) fifo_size << DIEPTXF0_TX0FD_Pos) | runtime->controller.dfifo_top;
    } else {
      // DIEPTXF starts at FIFO #1.
      dwc2->dieptxf[fifo - 1] = ((uint32_t) fifo_size << DIEPTXF_INEPTXFD_Pos) | runtime->controller.dfifo_top;
    }
  }

  return true;
}

static void dfifo_device_init(uint8_t rhport) {
  const EspUsbDwc2Port* dwc2_controller = espusb_dwc2_port(rhport);
  EspUsbDwc2Registers* dwc2 = espusb_dwc2_registers(rhport);
  dwc2->grxfsiz = calc_device_grxfsiz(ESPUSB_CONTROL_PACKET_BYTES, dwc2_controller->ep_count);

  // Scatter/Gather DMA mode is not yet supported. Buffer DMA only need 1 words per endpoint direction
  const bool is_dma = dma_device_enabled(dwc2);
  runtime->controller.dfifo_top = dwc2_controller->otg_dfifo_depth;
  if (is_dma) {
    runtime->controller.dfifo_top -= 2 * dwc2_controller->ep_count;
  }
  dwc2->gdfifocfg = ((uint32_t) runtime->controller.dfifo_top << GDFIFOCFG_EPINFOBASE_SHIFT) | runtime->controller.dfifo_top;

  // Allocate FIFO for EP0 IN
  (void) dfifo_alloc(rhport, 0x80, ESPUSB_CONTROL_PACKET_BYTES, false);
}

//--------------------------------------------------------------------
// Endpoint
//--------------------------------------------------------------------
static void edpt_activate(uint8_t rhport, const EspUsbEndpointDescriptor* p_endpoint_desc, uint16_t interval) {
  EspUsbDwc2Registers* dwc2 = espusb_dwc2_registers(rhport);
  const uint8_t epnum = espusb_endpoint_number(p_endpoint_desc->bEndpointAddress);
  const uint8_t dir = espusb_endpoint_direction(p_endpoint_desc->bEndpointAddress);

  EspUsbDwc2Transfer* xfer = selected_transfer(epnum, dir);
  xfer->max_size = espusb_endpoint_packet_size(p_endpoint_desc);

  xfer->interval = interval;

  // Endpoint control
  EspUsbDwc2Depctl depctl = {.value = 0};
  depctl.mps = xfer->max_size;
  depctl.active = 1;
  depctl.type = espusb_endpoint_type(p_endpoint_desc);
  if (espusb_endpoint_type(p_endpoint_desc) != ESPUSB_XFER_ISOCHRONOUS) {
    depctl.set_data0_iso_even = 1;
  }
  if (dir == ESPUSB_DIR_IN) {
    depctl.tx_fifo_num = selected_tx_fifo(epnum);
  }

  EspUsbDwc2EndpointRegisters* dep = &dwc2->ep[dir == ESPUSB_DIR_IN ? 0 : 1][epnum];
  dep->ctl = depctl.value;
  dwc2->daintmsk |= ESPUSB_BIT(epnum + DAINT_SHIFT(dir));
}

static void edpt_disable(uint8_t rhport, uint8_t ep_addr, bool stall) {
  (void) rhport;

  EspUsbDwc2Registers* dwc2 = espusb_dwc2_registers(rhport);
  const uint8_t epnum = espusb_endpoint_number(ep_addr);
  const uint8_t dir = espusb_endpoint_direction(ep_addr);
  EspUsbDwc2EndpointRegisters* dep = &dwc2->ep[dir == ESPUSB_DIR_IN ? 0 : 1][epnum];

  const uint32_t stall_mask = (stall ? EPCTL_STALL : 0);

  if (dir == ESPUSB_DIR_IN) {
    if (!edpt_is_enabled(dep)) {
      dep->diepctl |= DIEPCTL_SNAK | stall_mask;
    } else {
      // Stop transmitting packets and NAK IN xfers.
      dep->diepctl |= DIEPCTL_SNAK;
      while ((dep->diepint & DIEPINT_INEPNE) == 0) {}

      // Disable the endpoint.
      dep->diepctl |= DIEPCTL_EPDIS | stall_mask;
      while ((dep->diepint & DIEPINT_EPDISD_Msk) == 0) {}

      dep->diepint = DIEPINT_EPDISD;
    }

    // Flush the FIFO, and wait until we have confirmed it cleared.
    espusb_dwc2_flush_tx(dwc2, selected_tx_fifo(epnum));
  } else {
    if (!edpt_is_enabled(dep) || epnum == 0) {
      // non-control not-enabled: stall if set
      // For EP0 Out, keep it enabled to receive SETUP packets
      dep->doepctl |= stall_mask;
    } else {
      // Asserting GONAK is required to STALL an OUT endpoint.
      // Simpler to use polling here, we don't use the "B"OUTNAKEFF interrupt
      // anyway, and it can't be cleared by user code. If this while loop never
      // finishes, we have bigger problems than just the stack.
      dwc2->dctl |= DCTL_SGONAK;
      while ((dwc2->gintsts & GINTSTS_BOUTNAKEFF_Msk) == 0) {}

      // Ditto here disable the endpoint.
      dep->doepctl |= DOEPCTL_EPDIS | stall_mask;
      while ((dep->doepint & DOEPINT_EPDISD_Msk) == 0) {}

      dep->doepint = DOEPINT_EPDISD;

      // Allow other OUT endpoints to keep receiving.
      dwc2->dctl |= DCTL_CGONAK;
    }
  }

  // Clear ActEP
  if (!stall && epnum != 0) {
    dep->ctl &= ~EPCTL_USBAEP;
  }
}

// Since this function returns void, it is not possible to return a boolean success message
// We must make sure that this function is not called when the EP is disabled
// Must be called from critical section
static void edpt_schedule_packets(uint8_t rhport, const uint8_t epnum, const uint8_t dir) {
  EspUsbDwc2Registers* dwc2 = espusb_dwc2_registers(rhport);
  EspUsbDwc2Transfer* const xfer = selected_transfer(epnum, dir);
  EspUsbDwc2EndpointRegisters* dep = &dwc2->ep[dir == ESPUSB_DIR_IN ? 0 : 1][epnum];

  uint16_t num_packets;
  uint16_t total_bytes;

  // EP0 is limited to one packet per xfer
  if (epnum == 0) {
    total_bytes = espusb_min_u16(runtime->controller.ep0_pending[dir], ESPUSB_CONTROL_PACKET_BYTES);
    runtime->controller.ep0_pending[dir] -= total_bytes;
    num_packets = 1;
  } else {
    total_bytes = xfer->total_len;
    if (dma_device_enabled(dwc2)) {
      // GHWCFG3 describes the *implemented* counters. S3 has 7 packet bits,
      // even though the common TSIZ register type has space for 10.
      const EspUsbDwc2Ghwcfg3 caps = {.value = dwc2->ghwcfg3};
      const uint32_t byte_limit = (1u << espusb_min_u8(19, 11 + caps.xfer_size_width)) - 1;
      const uint32_t packet_limit = (1u << espusb_min_u8(10, 4 + caps.packet_size_width)) - 1;
      const uint32_t limit = espusb_min_u32(UINT16_MAX, espusb_min_u32(byte_limit, packet_limit * xfer->max_size));
      const uint32_t remaining = xfer->total_len - xfer->dma_done;
      // Intermediate chunks must end on a packet boundary, without inserting
      // a short packet into the application's transfer.
      total_bytes = remaining > limit ? (uint16_t)(limit / xfer->max_size * xfer->max_size) : (uint16_t)remaining;
      xfer->dma_chunk = total_bytes;
    }
    num_packets = (uint16_t)espusb_div_ceil(total_bytes, xfer->max_size);
    if (num_packets == 0) {
      num_packets = 1; // zero length packet still count as 1
    }
  }

  // transfer size: A full OUT transfer (multiple packets, possibly) triggers XFRC.
  EspUsbDwc2EpTsize deptsiz = {.value = 0};
  deptsiz.xfer_size = total_bytes;
  deptsiz.packet_count = num_packets;
  dep->tsiz = deptsiz.value;

  // control
  EspUsbDwc2Depctl depctl = {.value = dep->ctl};
  depctl.clear_nak = 1;
  depctl.enable = 1;
  if (depctl.type == DEPCTL_EPTYPE_ISOCHRONOUS) {
    const EspUsbDwc2Dsts dsts = {.value = dwc2->dsts};
    const uint32_t odd_now = dsts.frame_number & 1u;
    if (odd_now != 0) {
      depctl.set_data0_iso_even = 1;
    } else {
      depctl.set_data1_iso_odd = 1;
    }
  }

  const bool is_dma = dma_device_enabled(dwc2);
  if(is_dma) {
    // OUT too: a dirty line written back during the DMA would overwrite
    // received data, e.g. a borrowed buffer the application just filled.
    if (xfer->buffer && total_bytes != 0) {
      espusb_controller_dcache_clean(xfer->buffer, total_bytes);
    }
    dep->diepdma = (uintptr_t) xfer->buffer;
    dep->diepctl = depctl.value; // enable endpoint
  } else
  {
  }
}

//--------------------------------------------------------------------
// Controller API
//--------------------------------------------------------------------
// Runtime options are explicit and typed; no vendor configuration ID/union.
bool espusb_controller_configure(uint8_t rhport, const EspUsbControllerConfig *config) {
  (void)rhport;
  ESPUSB_REQUIRE(config != NULL && runtime == NULL);
  controller_config = *config;
  return true;
}

bool espusb_controller_init(uint8_t rhport, bool highSpeed) {
  ESPUSB_REQUIRE(runtime && runtime->port == rhport);

  ESPUSB_CLEAR_MEMORY(&runtime->controller, sizeof(runtime->controller));

  // Core Initialization
  EspUsbDwc2Registers* dwc2 = espusb_dwc2_registers(rhport);
  const bool is_hs_phy = espusb_dwc2_highspeed_phy(dwc2, highSpeed);
  const bool is_dma = dma_device_enabled(dwc2);
  ESPUSB_REQUIRE(is_dma);
  ESPUSB_REQUIRE(espusb_dwc2_phy_start(rhport, is_hs_phy));

  //------------- 7.1 Device Initialization -------------//
  // Set device max speed
  uint32_t dcfg = dwc2->dcfg & ~DCFG_DSPD_Msk;
  if (is_hs_phy) {
    // dcfg Highspeed's mask is 0

    // XCVRDLY: transceiver delay between xcvr_sel and txvalid during device chirp is required
    // when using with some PHYs such as USB334x (USB3341, USB3343, USB3346, USB3347)
    const EspUsbDwc2Ghwcfg2 ghwcfg2 = {.value = dwc2->ghwcfg2};
    if (ghwcfg2.hs_phy_type == GHWCFG2_HSPHY_ULPI) {
      dcfg |= DCFG_XCVRDLY;
    }
  } else {
    dcfg |= DCFG_DSPD_FS << DCFG_DSPD_Pos;
  }

  dcfg |= DCFG_NZLSOHSK; // send STALL back and discard if host send non-zlp during control status
  dwc2->dcfg = dcfg;

  espusb_controller_disconnect(rhport);

  // Force device mode
  dwc2->gusbcfg = (dwc2->gusbcfg & ~GUSBCFG_FHMOD) | GUSBCFG_FDMOD;

  // OTG Ctrl
  uint32_t gotgctl = dwc2->gotgctl & ~GOTGCTL_AVALOEN; // Clear A-override
  if (!controller_config.vbusSensing) {
    gotgctl |= GOTGCTL_BVALOEN | GOTGCTL_BVALOVAL;     // force B Valid if not sensing VBus
  }
  dwc2->gotgctl = gotgctl;

  // Enable required interrupts
  dwc2->gintmsk |= GINTMSK_OTGINT | GINTMSK_USBRST | GINTMSK_ENUMDNEM | GINTMSK_WUIM;

  uint32_t gahbcfg = dwc2->gahbcfg;
  gahbcfg |= GAHBCFG_GINT; // Enable global interrupt
  dwc2->gahbcfg = gahbcfg;

  espusb_controller_connect(rhport);
  return true;
}

bool espusb_controller_deinit(uint8_t rhport) {
  espusb_controller_disconnect(rhport);
  espusb_dwc2_release_irq(rhport);
  espusb_dwc2_phy_stop(rhport);
  return true;
}

bool espusb_controller_int_enable(uint8_t rhport) {
  return espusb_dwc2_enable_irq(rhport);
}

void espusb_controller_int_disable(uint8_t rhport) {
  espusb_dwc2_mask_irq(rhport);
}

void espusb_controller_set_address(uint8_t rhport, uint8_t dev_addr) {
  EspUsbDwc2Registers* dwc2 = espusb_dwc2_registers(rhport);
  espusb_controller_lock(false);
  if (!espusb_controller_io_ready(rhport)) { espusb_controller_unlock(false); return; }
  dwc2->dcfg = (dwc2->dcfg & ~DCFG_DAD_Msk) | (dev_addr << DCFG_DAD_Pos);
  espusb_controller_unlock(false);

  // Response with status after changing device address
  espusb_controller_edpt_xfer(rhport, espusb_endpoint_address(0, ESPUSB_DIR_IN), NULL, 0, false);
}

void espusb_controller_remote_wakeup(uint8_t rhport) {
  espusb_controller_lock(false);
  if (!espusb_controller_io_ready(rhport)) { espusb_controller_unlock(false); return; }
  EspUsbDwc2Registers* dwc2 = espusb_dwc2_registers(rhport);
  dwc2->dctl |= DCTL_RWUSIG;
  // SOF confirms actual resume; requesting wakeup alone does not clear suspend.
  dwc2->gintsts = GINTSTS_SOF;
  dwc2->gintmsk |= GINTMSK_SOFM;
  // Bound the signal without a scheduler tick or task preemption in the pulse.
  espusb_dwc2_wakeup_delay();
  dwc2->dctl &= ~DCTL_RWUSIG;
  espusb_controller_unlock(false);
}

void espusb_controller_connect(uint8_t rhport) {
  EspUsbDwc2Registers* dwc2 = espusb_dwc2_registers(rhport);

  espusb_dwc2_set_pulls(rhport, true);

  dwc2->dctl &= ~DCTL_SDIS;
}

void espusb_controller_disconnect(uint8_t rhport) {
  EspUsbDwc2Registers* dwc2 = espusb_dwc2_registers(rhport);

  espusb_dwc2_set_pulls(rhport, false);

  dwc2->dctl |= DCTL_SDIS;
}

// Be advised: audio, video and possibly other iso-ep classes use espusb_controller_sof_enable() to enable/disable its corresponding ISR on purpose!
void espusb_controller_sof_enable(uint8_t rhport, bool en) {
  (void) rhport;
  EspUsbDwc2Registers* dwc2 = espusb_dwc2_registers(rhport);

  runtime->controller.sof_en = en;

  if (en) {
    dwc2->gintsts = GINTSTS_SOF;
    dwc2->gintmsk |= GINTMSK_SOFM;
  } else {
    dwc2->gintmsk &= ~GINTMSK_SOFM;
  }
}

/*------------------------------------------------------------------*/
/* DCD Endpoint port
 *------------------------------------------------------------------*/

bool espusb_controller_edpt_open(uint8_t rhport, EspUsbEndpointDescriptor const* desc_edpt) {
  ESPUSB_REQUIRE(selected_transfer(espusb_endpoint_number(desc_edpt->bEndpointAddress), espusb_endpoint_direction(desc_edpt->bEndpointAddress)));
  espusb_controller_lock(false);
  const EspUsbDwc2Dsts status = {.value = espusb_dwc2_registers(rhport)->dsts};
  uint16_t interval = 0;
  const bool opened = espusb_controller_io_ready(rhport) && espusb_dwc2_decode_interval(desc_edpt, status.enum_speed == DCFG_SPEED_HIGH, &interval) &&
      dfifo_alloc(rhport, desc_edpt->bEndpointAddress, espusb_endpoint_packet_size(desc_edpt),
                  espusb_endpoint_type(desc_edpt) == ESPUSB_XFER_BULK);
  if (opened) {
    edpt_activate(rhport, desc_edpt, interval);
    if (espusb_endpoint_direction(desc_edpt->bEndpointAddress) == ESPUSB_DIR_OUT && espusb_endpoint_type(desc_edpt) == ESPUSB_XFER_ISOCHRONOUS)
      espusb_dwc2_registers(rhport)->gintmsk |= GINTMSK_IISOOXFRM;
  }
  espusb_controller_unlock(false);
  return opened;
}

// Close all non-control endpoints, cancel all pending transfers if any.
void espusb_controller_edpt_close_all(uint8_t rhport) {
  EspUsbDwc2Registers* dwc2 = espusb_dwc2_registers(rhport);
  uint8_t const ep_count = espusb_dwc2_port(rhport)->ep_count;

  espusb_controller_lock(false);

  runtime->controller.allocated_epin_count = 0;

  // Disable non-control interrupt
  dwc2->daintmsk = (1 << DAINTMSK_OEPM_Pos) | (1 << DAINTMSK_IEPM_Pos);

  for (uint8_t n = 1; n < ep_count; n++) {
    for (uint8_t d = 0; d < 2; d++) {
      EspUsbDwc2EndpointRegisters* dep = &dwc2->ep[d][n];
      if (edpt_is_enabled(dep)) {
        // EPDIS is asynchronous. Wait until DMA has actually stopped before
        // a class can return its buffers or reopen this endpoint number.
        edpt_disable(rhport, n | (d == 0 ? ESPUSB_DIR_IN_MASK : 0), false);
      }
      dep->ctl &= ~EPCTL_USBAEP;
      // A completed transfer may have left EPENA clear but XFRC latched.
      // It must not be interpreted using the next arm's transfer record.
      dep->intr = 0xffffffffu;
      EspUsbDwc2Transfer *xfer = selected_transfer(n, 1-d);
      if (xfer) ESPUSB_CLEAR_MEMORY(xfer, sizeof(*xfer));
    }
  }

  espusb_dwc2_flush_tx(dwc2, 0x10); // all tx fifo
  espusb_dwc2_flush_rx(dwc2);
  dfifo_device_init(rhport); // re-init dfifo

  espusb_controller_unlock(false);
}

bool espusb_controller_edpt_iso_alloc(uint8_t rhport, uint8_t ep_addr, uint16_t largest_packet_size) {
  ESPUSB_REQUIRE(selected_transfer(espusb_endpoint_number(ep_addr), espusb_endpoint_direction(ep_addr)));
  espusb_controller_lock(false);
  const bool allocated = espusb_controller_io_ready(rhport) && dfifo_alloc(rhport, ep_addr, largest_packet_size, false);
  espusb_controller_unlock(false);
  return allocated;
}

bool espusb_controller_edpt_iso_activate(uint8_t rhport,  EspUsbEndpointDescriptor const * p_endpoint_desc) {
  ESPUSB_REQUIRE(selected_transfer(espusb_endpoint_number(p_endpoint_desc->bEndpointAddress), espusb_endpoint_direction(p_endpoint_desc->bEndpointAddress)));
  espusb_controller_lock(false);
  const EspUsbDwc2Dsts status = {.value = espusb_dwc2_registers(rhport)->dsts};
  uint16_t interval = 0;
  const bool valid = espusb_controller_io_ready(rhport) && espusb_dwc2_decode_interval(p_endpoint_desc, status.enum_speed == DCFG_SPEED_HIGH, &interval);
  if (valid) {
    edpt_disable(rhport, p_endpoint_desc->bEndpointAddress, false);
    edpt_activate(rhport, p_endpoint_desc, interval);
  }
  espusb_controller_unlock(false);
  return valid;
}

bool espusb_controller_edpt_xfer(uint8_t rhport, uint8_t ep_addr, uint8_t* buffer, uint32_t total_bytes, bool is_isr) {
  (void) is_isr;
  uint8_t const epnum = espusb_endpoint_number(ep_addr);
  uint8_t const dir = espusb_endpoint_direction(ep_addr);
  EspUsbDwc2Transfer* xfer = selected_transfer(epnum, dir);
  bool ret;

  espusb_controller_lock(is_isr);

  if (!espusb_controller_io_ready(rhport) || !xfer || xfer->max_size == 0) {
    ret = false;  // Endpoint is closed
  } else if (total_bytes > UINT16_MAX &&
             (epnum == 0 || !dma_device_enabled(espusb_dwc2_registers(rhport)) ||
              (EspUsbDwc2Depctl){.value = espusb_dwc2_registers(rhport)->ep[dir == ESPUSB_DIR_IN ? 0 : 1][epnum].ctl}.type == ESPUSB_XFER_ISOCHRONOUS)) {
    ret = false; // Control and isochronous single-frame submissions stay bounded.
  } else {
    xfer->buffer = buffer;
    xfer->total_len = total_bytes;
    xfer->dma_done = xfer->dma_chunk = 0;
    xfer->iso_retry = xfer->interval; // Reset ISO retry counter to interval value

    // EP0 can only handle one packet
    if (epnum == 0) {
      runtime->controller.ep0_pending[dir] = (uint16_t)total_bytes;
    }

    // Schedule packets to be sent within interrupt
    edpt_schedule_packets(rhport, epnum, dir);
    ret = true;
  }

  espusb_controller_unlock(is_isr);

  return ret;
}

void espusb_controller_edpt_stall(uint8_t rhport, uint8_t ep_addr) {
  EspUsbDwc2Registers* dwc2 = espusb_dwc2_registers(rhport);
  espusb_controller_lock(false);
  edpt_disable(rhport, ep_addr, true);

  // For control endpoint, prepare to receive SETUP packet
  if (espusb_endpoint_number(ep_addr) == 0) {
    if (dma_device_enabled(dwc2)) {
      dma_setup_prepare(rhport);
    }
  }
  espusb_controller_unlock(false);
}

bool espusb_controller_edpt_abort_supported(void) { return true; }
bool espusb_controller_edpt_abort(uint8_t rhport, uint8_t ep_addr) {
  const uint8_t epnum = espusb_endpoint_number(ep_addr), dir = espusb_endpoint_direction(ep_addr);
  if (epnum == 0 || epnum >= espusb_dwc2_port(rhport)->ep_count) return false;
  espusb_controller_lock(false);
  EspUsbDwc2Transfer* xfer = selected_transfer(epnum, dir);
  if (!espusb_controller_io_ready(rhport) || !xfer || xfer->max_size == 0) { espusb_controller_unlock(false); return false; }
  EspUsbDwc2Registers* dwc2 = espusb_dwc2_registers(rhport);
  EspUsbDwc2EndpointRegisters* dep = &dwc2->ep[dir == ESPUSB_DIR_IN ? 0 : 1][epnum];
  edpt_disable(rhport, ep_addr, false);
  // edpt_disable clears active-endpoint. Restore it without resetting PID or
  // reallocating the FIFO; the next submit will clear NAK and enable DMA.
  if (dir == ESPUSB_DIR_IN) dep->diepint = 0xffffffffu;
  else {
    dep->doepint = 0xffffffffu;
    if (xfer->buffer && xfer->dma_chunk) espusb_controller_dcache_invalidate(xfer->buffer, xfer->dma_chunk);
  }
  dep->ctl |= EPCTL_USBAEP;
  xfer->buffer = NULL;
  xfer->total_len = xfer->dma_done = xfer->dma_chunk = 0;
  espusb_controller_unlock(false);
  return true;
}

void espusb_controller_edpt_clear_stall(uint8_t rhport, uint8_t ep_addr) {
  EspUsbDwc2Registers* dwc2 = espusb_dwc2_registers(rhport);
  uint8_t const epnum = espusb_endpoint_number(ep_addr);
  uint8_t const dir = espusb_endpoint_direction(ep_addr);
  EspUsbDwc2EndpointRegisters* dep = &dwc2->ep[dir == ESPUSB_DIR_IN ? 0 : 1][epnum];

  // Clear stall and reset data toggle
  espusb_controller_lock(false);
  dep->ctl &= ~EPCTL_STALL;
  dep->ctl |= EPCTL_SD0PID_SEVNFRM;
  espusb_controller_unlock(false);
}

//--------------------------------------------------------------------
// Interrupt Handler
//--------------------------------------------------------------------

// 7.4.1 Initialization on USB Reset
// Must be called from critical section
static void handle_bus_reset(uint8_t rhport) {
  EspUsbDwc2Registers *dwc2 = espusb_dwc2_registers(rhport);
  const uint8_t ep_count =  dwc2_ep_count(dwc2);

  ESPUSB_CLEAR_MEMORY(runtime->transfers, (runtime->count[0] + runtime->count[1]) * sizeof(EspUsbDwc2Transfer));

  runtime->controller.ep0_pending[ESPUSB_DIR_OUT] = 0;
  runtime->controller.ep0_pending[ESPUSB_DIR_IN] = 0;
  runtime->controller.sof_en = false;
  runtime->controller.allocated_epin_count = 0;

  // 1. NAK for all OUT endpoints
  for (uint8_t n = 0; n < ep_count; n++) {
    dwc2->epout[n].doepctl |= DOEPCTL_SNAK;
  }

  // Disable all IN endpoints
  for (uint8_t n = 0; n < ep_count; n++) {
    EspUsbDwc2EndpointRegisters* dep = &dwc2->epin[n];
    if (edpt_is_enabled(dep)) {
      dep->diepctl |= DIEPCTL_SNAK | DIEPCTL_EPDIS;
    }
  }

  // 2. Set up interrupt mask for EP0
  dwc2->daintmsk = ESPUSB_BIT(DAINTMSK_OEPM_Pos) | ESPUSB_BIT(DAINTMSK_IEPM_Pos);
  dwc2->doepmsk = DOEPMSK_STUPM | DOEPMSK_XFRCM;
  dwc2->diepmsk = DIEPMSK_TOM | DIEPMSK_XFRCM;

  // 4. Set up DFIFO
  espusb_dwc2_flush_tx(dwc2, 0x10); // all tx fifo
  espusb_dwc2_flush_rx(dwc2);
  dfifo_device_init(rhport);

  // 5. Reset device address
  EspUsbDwc2Dcfg dcfg = {.value = dwc2->dcfg};
  dcfg.address = 0;
  dwc2->dcfg = dcfg.value;

  // 6. Configure maximum packet size for EP0
  uint8_t mps = 0;
  switch (ESPUSB_CONTROL_PACKET_BYTES) {
    case 8: mps = 3; break;
    case 16: mps = 2; break;
    case 32: mps = 1; break;
    case 64: mps = 0; break;
    default: mps = 0; break;
  }

  dwc2->epin[0].ctl &= ~DIEPCTL0_MPSIZ_Msk;
  dwc2->epout[0].ctl &= ~DOEPCTL0_MPSIZ_Msk;
  dwc2->epin[0].ctl |= mps << DIEPCTL0_MPSIZ_Pos;
  dwc2->epout[0].ctl |= mps << DOEPCTL0_MPSIZ_Pos;

  selected_transfer(0, ESPUSB_DIR_OUT)->max_size = ESPUSB_CONTROL_PACKET_BYTES;
  selected_transfer(0, ESPUSB_DIR_IN)->max_size = ESPUSB_CONTROL_PACKET_BYTES;

  uint32_t gintmsk = GINTMSK_OTGINT | GINTMSK_IEPINT | GINTMSK_IISOIXFRM;
  if(dma_device_enabled(dwc2)) {
    gintmsk |= GINTMSK_OEPINT;
    dma_setup_prepare(rhport);
  } else {
    dwc2->epout[0].doeptsiz |= (3 << DOEPTSIZ_STUPCNT_Pos);
  }

  dwc2->gintmsk |= gintmsk;
}

static void handle_enum_done(uint8_t rhport) {
  EspUsbDwc2Registers *dwc2 = espusb_dwc2_registers(rhport);
  const EspUsbDwc2Dsts dsts = {.value = dwc2->dsts};
  EspUsbSpeed speed;
  switch (dsts.enum_speed) {
    case DCFG_SPEED_HIGH:
      speed = ESPUSB_SPEED_HIGH;
    break;

    case DCFG_SPEED_LOW:
      speed = ESPUSB_SPEED_LOW;
    break;

    case DCFG_SPEED_FULL_30_60MHZ:
    case DCFG_SPEED_FULL_48MHZ:
    default:
      speed = ESPUSB_SPEED_FULL;
    break;
  }

  // TODO must update GUSBCFG_TRDT according to link speed
  espusb_controller_event_bus_reset(rhport, speed, true);
}

static void handle_epout_dma(uint8_t rhport, uint8_t epnum, EspUsbDwc2Doepint doepint_bm) {
  EspUsbDwc2Registers* dwc2 = espusb_dwc2_registers(rhport);

  if (doepint_bm.setup_phase_done) {
    // Cleanup previous pending EP0 IN transfer if any
    EspUsbDwc2EndpointRegisters* epin0 = &dwc2->epin[0];
    EspUsbDwc2EndpointRegisters* epout0 = &dwc2->epout[0];
    if (edpt_is_enabled(epin0)) {
      edpt_disable(rhport, 0x80, false);
    }
    // a new SETUP aborts any in-progress control transfer: drop leftover EP0 chunking state so a
    // stale latched completion cannot re-arm from it
    runtime->controller.ep0_pending[ESPUSB_DIR_OUT] = 0;
    runtime->controller.ep0_pending[ESPUSB_DIR_IN] = 0;

    espusb_controller_dcache_invalidate(runtime->setup_buffer, DWC2_SETUP_STORAGE_BYTES);

    // DOEPDMA0 has advanced past the last received SETUP packet; back up one packet to the latest valid one
    // (Programming Guide v4.20a section 9.1.2.1: "DOEPDMAn-8 provides the pointer to the last valid SETUP data")
    EspUsbControlRequest *setup_packet = (EspUsbControlRequest *) (uintptr_t) (epout0->doepdma - sizeof(EspUsbControlRequest));
    espusb_controller_event_setup_received(rhport, (uint8_t*)setup_packet, true);

    // Prepare EP0 for next setup if this setup has no data stage
    if (setup_packet->wLength == 0) {
      dma_setup_prepare(rhport);
    }
    return;
  }

  // OUT XFER complete
  if (doepint_bm.xfer_complete) {
    // only handle data skip if it is setup or status related
    // Normal OUT transfer complete
    if (!doepint_bm.status_phase_rx && !doepint_bm.setup_packet_rx) {
      EspUsbDwc2Transfer* xfer = selected_transfer(epnum, ESPUSB_DIR_OUT);
      if ((epnum == 0) && runtime->controller.ep0_pending[ESPUSB_DIR_OUT]) {
        // EP0 can only handle one packet: invalidate and advance past the received bytes, then
        // schedule the next.
        if (xfer->buffer != NULL) {
          espusb_controller_dcache_invalidate(xfer->buffer, ESPUSB_CONTROL_PACKET_BYTES);
          xfer->buffer += ESPUSB_CONTROL_PACKET_BYTES;
        }
        edpt_schedule_packets(rhport, epnum, ESPUSB_DIR_OUT);
      } else {
        EspUsbDwc2EndpointRegisters* epout = &dwc2->epout[epnum];

        // Non-EP0 DMA may require several hardware arms. Invalidate only
        // this chunk, and stop early on a short packet or ZLP.
        const EspUsbDwc2EpTsize tsiz = {.value = epout->tsiz};
        const uint16_t remain = tsiz.xfer_size;
        if (epnum != 0) {
          const uint16_t actual = xfer->dma_chunk - remain;
          espusb_controller_dcache_invalidate(xfer->buffer, actual);
          xfer->dma_done += actual;
          if (actual == xfer->dma_chunk && xfer->dma_done < xfer->total_len) {
            if (xfer->buffer) xfer->buffer += actual;
            edpt_schedule_packets(rhport, epnum, ESPUSB_DIR_OUT);
          } else {
            espusb_controller_event_xfer_complete(rhport, epnum, xfer->dma_done, ESPUSB_TRANSFER_SUCCESS, true);
          }
          return;
        }
        xfer->total_len -= remain;

        // EP0 invalidates only this (final) chunk's DMA-written bytes: DOEPDMA "is incremented on
        // every AHB transaction" (databook 7.1.83), i.e. it points past the last word written.
        // Read it before dma_setup_prepare() re-targets it at the setup buffer
        uint16_t inval_len = xfer->total_len;
        if (epnum == 0) {
          inval_len = (uint16_t)(epout->doepdma - (uintptr_t)xfer->buffer);
        }

        // prepare EP0 for next setup
        if(epnum == 0) {
          dma_setup_prepare(rhport);
        }

        espusb_controller_dcache_invalidate(xfer->buffer, inval_len);
        espusb_controller_event_xfer_complete(rhport, epnum, xfer->total_len, ESPUSB_TRANSFER_SUCCESS, true);
      }
    }
  }
}

static void handle_epin_dma(uint8_t rhport, uint8_t epnum, EspUsbDwc2Diepint diepint_bm) {
  EspUsbDwc2Transfer* xfer = selected_transfer(epnum, ESPUSB_DIR_IN);

  if (diepint_bm.xfer_complete) {
    if (epnum != 0) {
      xfer->dma_done += xfer->dma_chunk;
      if (xfer->dma_done < xfer->total_len) {
        if (xfer->buffer) xfer->buffer += xfer->dma_chunk;
        edpt_schedule_packets(rhport, epnum, ESPUSB_DIR_IN);
      } else {
        espusb_controller_event_xfer_complete(rhport, epnum | ESPUSB_DIR_IN_MASK, xfer->dma_done, ESPUSB_TRANSFER_SUCCESS, true);
      }
      return;
    }
    if ((epnum == 0) && runtime->controller.ep0_pending[ESPUSB_DIR_IN]) {
      // EP0 can only handle one packet: advance past the sent bytes, then schedule the next.
      if (xfer->buffer != NULL) {
        xfer->buffer += ESPUSB_CONTROL_PACKET_BYTES;
      }
      edpt_schedule_packets(rhport, epnum, ESPUSB_DIR_IN);
    } else {
      espusb_controller_event_xfer_complete(rhport, epnum | ESPUSB_DIR_IN_MASK, xfer->total_len, ESPUSB_TRANSFER_SUCCESS, true);
    }
  }
}

static void handle_ep_irq(uint8_t rhport, uint8_t dir) {
  EspUsbDwc2Registers* dwc2 = espusb_dwc2_registers(rhport);
  const bool is_dma = dma_device_enabled(dwc2);
  const uint8_t ep_count = dwc2_ep_count(dwc2);
  const uint8_t daint_offset = (dir == ESPUSB_DIR_IN) ? DAINT_IEPINT_Pos : DAINT_OEPINT_Pos;
  EspUsbDwc2EndpointRegisters* ep_base = &dwc2->ep[dir == ESPUSB_DIR_IN ? 0 : 1][0];

  // DAINT for a given EP clears when DEPINTx is cleared.
  // EPINT will be cleared when DAINT bits are cleared.
  for (uint8_t epnum = 0; epnum < ep_count; epnum++) {
    if (espusb_bit_test(dwc2->daint & dwc2->daintmsk,daint_offset + epnum)) {
      EspUsbDwc2EndpointRegisters* epout = &ep_base[epnum];
      union {
        uint32_t value;
        EspUsbDwc2Diepint diepint_bm;
        EspUsbDwc2Doepint doepint_bm;
      } intr;
      intr.value = epout->intr;

      epout->intr = intr.value; // Clear interrupt //-V::2584::{otg_int}
      if (!selected_transfer(epnum, dir)) continue;

      if (is_dma) {
        if (dir == ESPUSB_DIR_IN) {
          handle_epin_dma(rhport, epnum, intr.diepint_bm);
        } else {
          handle_epout_dma(rhport, epnum, intr.doepint_bm);
        }
      } else {
      }
    }
  }
}

static void handle_incomplete_iso_in(uint8_t rhport) {
  EspUsbDwc2Registers      *dwc2    = espusb_dwc2_registers(rhport);
  const EspUsbDwc2Dsts dsts    = {.value = dwc2->dsts};
  const uint32_t    odd_now = dsts.frame_number & 1u;

  // Loop over all IN endpoints
  const uint8_t ep_count = dwc2_ep_count(dwc2);
  for (uint8_t epnum = 0; epnum < ep_count; epnum++) {
    EspUsbDwc2EndpointRegisters   *epin   = &dwc2->epin[epnum];
    EspUsbDwc2Depctl depctl = {.value = epin->diepctl};
    // Read DSTS and DIEPCTLn for all isochronous endpoints. If the current EP is enabled and the read value of
    // DSTS.SOFFN is the targeted uframe number for this EP, then this EP has an incomplete transfer.
    if (depctl.enable && depctl.type == DEPCTL_EPTYPE_ISOCHRONOUS && depctl.dpid_iso_odd == odd_now) {
      EspUsbDwc2Transfer *xfer = selected_transfer(epnum, ESPUSB_DIR_IN);
      if (!xfer) continue;
      if (xfer->iso_retry > 0) {
        xfer->iso_retry--;
        // Restart ISO transfe: re-write TSIZ and CTL
        EspUsbDwc2EpTsize deptsiz = {.value = 0};
        deptsiz.xfer_size       = xfer->total_len;
        deptsiz.packet_count    = espusb_div_ceil(xfer->total_len, xfer->max_size);
        epin->tsiz              = deptsiz.value;

        if (odd_now) {
          depctl.set_data0_iso_even = 1;
        } else {
          depctl.set_data1_iso_odd = 1;
        }
        epin->diepctl = depctl.value;
      } else {
        // too many retries, give up, but keep endpoint activated
        edpt_disable(rhport, epnum | ESPUSB_DIR_IN_MASK, false);
        epin->diepctl |= DIEPCTL_USBAEP;
        espusb_controller_event_xfer_complete(rhport, epnum | ESPUSB_DIR_IN_MASK, 0, ESPUSB_TRANSFER_FAILED, true);
      }
    }
  }
}

// An ISO OUT arm whose target frame passed may never receive XFRC. Stop its
// DMA before reporting failure; the class can then safely reuse and rearm the
// packet for a later frame. Bulk/interrupt OUT and EP0 are not involved.
static void handle_incomplete_iso_out(uint8_t rhport) {
  EspUsbDwc2Registers *dwc2 = espusb_dwc2_registers(rhport);
  const EspUsbDwc2Dsts status = {.value = dwc2->dsts};
  for (uint8_t epnum = 1; epnum < dwc2_ep_count(dwc2); ++epnum) {
    EspUsbDwc2EndpointRegisters *dep = &dwc2->epout[epnum];
    const EspUsbDwc2Depctl control = {.value = dep->ctl};
    if (!control.enable || control.type != DEPCTL_EPTYPE_ISOCHRONOUS || control.dpid_iso_odd != (status.frame_number & 1u) || !selected_transfer(epnum, ESPUSB_DIR_OUT)) continue;
    edpt_disable(rhport, epnum, false);
    dep->ctl |= EPCTL_USBAEP;
    espusb_controller_event_xfer_complete(rhport, epnum, 0, ESPUSB_TRANSFER_FAILED, true);
  }
}

/* Interrupt Hierarchy
                 DIEPINT  DIEPINT
                    \       /
                     \     /
                      DAINT
                     /     \
                    /       \
     GINTSTS:    OEPInt    IEPInt | USBReset | EnumDone | USBSusp | WkUpInt | OTGInt | SOF | RXFLVL

  Note: when OTG_MULTI_PROC_INTRPT = 1, Device Each endpoint interrupt deachint/deachmsk/diepeachmsk/doepeachmsk
  are combined to generate dedicated interrupt line for each endpoint.
 */
void espusb_controller_int_handler(uint8_t rhport) {
  EspUsbDwc2Registers* dwc2 = espusb_dwc2_registers(rhport);
  const uint32_t gintmask = dwc2->gintmsk;
  const uint32_t gintsts = dwc2->gintsts & gintmask;

  if (gintsts & GINTSTS_USBRST) {
    // USBRST is start of reset.
    dwc2->gintsts = GINTSTS_USBRST;

    espusb_controller_lock(true);
    espusb_controller_reset_begin(rhport);
    handle_bus_reset(rhport);
    espusb_controller_unlock(true);
  }

  if (gintsts & GINTSTS_ENUMDNE) {
    // ENUMDNE is the end of reset where speed of the link is detected
    dwc2->gintsts = GINTSTS_ENUMDNE;
    // There may be a pending suspend event, so we clear it first
    dwc2->gintsts = GINTSTS_USBSUSP;
    dwc2->gintmsk |= GINTMSK_USBSUSPM;
    handle_enum_done(rhport);
  }

  if (gintsts & GINTSTS_USBSUSP) {
    dwc2->gintsts = GINTSTS_USBSUSP;
    dwc2->gintmsk &= ~GINTMSK_USBSUSPM;
    espusb_controller_event_bus_signal(rhport, ESPUSB_CONTROLLER_EVENT_SUSPEND, true);
  }

  if (gintsts & GINTSTS_WKUINT) {
    dwc2->gintsts = GINTSTS_WKUINT;
    dwc2->gintmsk |= GINTMSK_USBSUSPM;
    espusb_controller_event_bus_signal(rhport, ESPUSB_CONTROLLER_EVENT_RESUME, true);
  }

  // TODO check GINTSTS_DISCINT for disconnect detection
  // if(int_status & GINTSTS_DISCINT)

  if (gintsts & GINTSTS_OTGINT) {
    // OTG INT bit is read-only
    const uint32_t otg_int = dwc2->gotgint;

    if (otg_int & GOTGINT_SEDET) {
      dwc2->gintmsk &= ~GINTMSK_OTGINT;
      espusb_controller_event_bus_signal(rhport, ESPUSB_CONTROLLER_EVENT_UNPLUGGED, true);
    }

    dwc2->gotgint = otg_int;
  }

  // SOF callbacks may take class locks and submit transfers. Dispatch them
  // outside the hardware lock used by the endpoint interrupt sections below.
  if(gintsts & GINTSTS_SOF && dwc2->gintmsk & GINTMSK_SOFM) {
    dwc2->gintsts = GINTSTS_SOF;
    dwc2->gintmsk |= GINTMSK_USBSUSPM;
    const uint32_t frame = (dwc2->dsts & DSTS_FNSOF) >> DSTS_FNSOF_Pos;

    // Disable SOF interrupt if SOF was not explicitly enabled since SOF was used for remote wakeup detection
    if (!runtime->controller.sof_en) {
      dwc2->gintmsk &= ~GINTMSK_SOFM;
    }

    espusb_controller_event_sof(rhport, frame, true);
  }

  // IN endpoint interrupt handling.
  if (gintsts & GINTSTS_IEPINT) {
    // IEPINT bit read-only, clear using DIEPINTn
    espusb_controller_lock(true);
    handle_ep_irq(rhport, ESPUSB_DIR_IN);
    espusb_controller_unlock(true);
  }

  // OUT endpoint interrupt handling.
  if (gintsts & GINTSTS_OEPINT) {
    // OEPINT is read-only, clear using DOEPINTn
    espusb_controller_lock(true);
    handle_ep_irq(rhport, ESPUSB_DIR_OUT);
    espusb_controller_unlock(true);
  }

  if (gintsts & GINTSTS_IISOOXFR) {
    dwc2->gintsts = GINTSTS_IISOOXFR;
    espusb_controller_lock(true);
    handle_incomplete_iso_out(rhport);
    espusb_controller_unlock(true);
  }

  // Incomplete isochronous IN transfer interrupt handling.
  if (gintsts & GINTSTS_IISOIXFR) {
    dwc2->gintsts = GINTSTS_IISOIXFR;
    espusb_controller_lock(true);
    handle_incomplete_iso_in(rhport);
    espusb_controller_unlock(true);
  }
}
