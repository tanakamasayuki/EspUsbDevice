/*
 * Device register views derived from TinyUSB dwc2_type.h, pin recorded in
 * third_party/tinyusb/ORIGIN.json. Copyright (c) 2024 Ha Thach (MIT) and
 * Copyright (c) 2019 STMicroelectronics (BSD-3-Clause).
 * See third_party/tinyusb/LICENSE and LICENSE-ST. This device-only map is
 * maintained here; host/channel/FIFO windows are deliberately not described.
 */
#pragma once
#include "EspUsbDwc2Support.h"

typedef union {
  uint32_t value;
  struct ESPUSB_PACKED {
    uint32_t speed                    : 2; // 0..1 Speed
    uint32_t nzsts_out_handshake      : 1; // 2 Non-zero-length status OUT handshake
    uint32_t en_32khz_suspsend        : 1; // 3 Enable 32-kHz SUSPEND mode
    uint32_t address                  : 7; // 4..10 Device address
    uint32_t period_frame_interval    : 2; // 11..12 Periodic frame interval
    uint32_t en_out_nak               : 1; // 13 Enable Device OUT NAK
    uint32_t xcvr_delay               : 1; // 14 Transceiver delay
    uint32_t erratic_int_mask         : 1; // 15 Erratic interrupt mask
    uint32_t rsv16                    : 1; // 16 Reserved
    uint32_t ipg_iso_support          : 1; // 17 Interpacket gap ISO support
    uint32_t epin_mismatch_count      : 5; // 18..22 EP IN mismatch count
    uint32_t dma_desc                 : 1; // 23 Enable scatter/gather DMA descriptor
    uint32_t period_schedule_interval : 2; // 24..25 Periodic schedule interval for scatter/gather DMA
    uint32_t resume_valid             : 6; // 26..31 Resume valid period
  };
} EspUsbDwc2Dcfg;
ESPUSB_STATIC_ASSERT(sizeof(EspUsbDwc2Dcfg) == 4, "EspUsbDwc2Dcfg size");
typedef union {
  uint32_t value;
  struct ESPUSB_PACKED {
    uint32_t mps                : 11; // 0..10 Maximum packet size, EP0 only use 2 bits
    uint32_t next_ep            : 4;  // 11..14 Next endpoint number
    uint32_t active             : 1;  // 15 Active
    uint32_t dpid_iso_odd       : 1;  // 16 DATA0/DATA1 for bulk/interrupt, odd frame for isochronous
    uint32_t nak_status         : 1;  // 17 NAK status
    uint32_t type               : 2;  // 18..19 Endpoint type
    uint32_t rsv20              : 1;  // 20 Reserved
    uint32_t stall              : 1;  // 21 Stall
    uint32_t tx_fifo_num        : 4;  // 22..25 Tx FIFO number (IN)
    uint32_t clear_nak          : 1;  // 26 Clear NAK
    uint32_t set_nak            : 1;  // 27 Set NAK
    uint32_t set_data0_iso_even : 1;  // 28 Set DATA0 if bulk/interrupt, even frame for isochronous
    uint32_t set_data1_iso_odd  : 1;  // 29 Set DATA1 if bulk/interrupt, odd frame for isochronous
    uint32_t disable            : 1;  // 30 Disable
    uint32_t enable             : 1;  // 31 Enable
  };
} EspUsbDwc2Depctl;
ESPUSB_STATIC_ASSERT(sizeof(EspUsbDwc2Depctl) == 4, "EspUsbDwc2Depctl size");
typedef union {
  uint32_t value;
  struct ESPUSB_PACKED {
    uint32_t xfer_complete       : 1; // 0 Transfer complete
    uint32_t disabled            : 1; // 1 Endpoint disabled
    uint32_t ahb_err             : 1; // 2 AHB error
    uint32_t timeout             : 1; // 3 Timeout
    uint32_t in_rx_txfe          : 1; // 4 IN token received when TxFIFO is empty
    uint32_t in_rx_ep_mismatch   : 1; // 5 IN token received with EP mismatch
    uint32_t in_ep_nak_effective : 1; // 6 IN endpoint NAK effective
    uint32_t txfifo_empty        : 1; // 7 TX FIFO empty
    uint32_t txfifo_underrun     : 1; // 8 Tx FIFO under run
    uint32_t bna                 : 1; // 9 Buffer not available
    uint32_t rsv10               : 1; // 10 Reserved
    uint32_t iso_packet_drop     : 1; // 11 Isochronous OUT packet drop status
    uint32_t babble_err          : 1; // 12 Babble error
    uint32_t nak                 : 1; // 13 NAK
    uint32_t nyet                : 1; // 14 NYET
    uint32_t rsv14_31            :17; // 15..31 Reserved
  };
} EspUsbDwc2Diepint;
ESPUSB_STATIC_ASSERT(sizeof(EspUsbDwc2Diepint) == 4, "EspUsbDwc2Diepint size");
typedef union {
  uint32_t value;
  struct ESPUSB_PACKED {
    uint32_t xfer_complete      : 1; // 0 Transfer complete
    uint32_t disabled           : 1; // 1 Endpoint disabled
    uint32_t ahb_err            : 1; // 2 AHB error
    uint32_t setup_phase_done   : 1; // 3 Setup phase done
    uint32_t out_rx_ep_disabled : 1; // 4 OUT token received when endpoint disabled
    uint32_t status_phase_rx    : 1; // 5 Status phase received
    uint32_t setup_b2b          : 1; // 6 Setup packet back-to-back
    uint32_t rsv7               : 1; // 7 Reserved
    uint32_t out_packet_err     : 1; // 8 OUT packet error
    uint32_t bna                : 1; // 9 Buffer not available
    uint32_t rsv10              : 1; // 10 Reserved
    uint32_t iso_packet_drop    : 1; // 11 Isochronous OUT packet drop status
    uint32_t babble_err         : 1; // 12 Babble error
    uint32_t nak                : 1; // 13 NAK
    uint32_t nyet               : 1; // 14 NYET
    uint32_t setup_packet_rx    : 1; // 15 Setup packet received (Buffer DMA Mode only)
    uint32_t rsv16_31           :16; // 16..31 Reserved
  };
} EspUsbDwc2Doepint;
ESPUSB_STATIC_ASSERT(sizeof(EspUsbDwc2Doepint) == 4, "EspUsbDwc2Doepint size");
typedef union {
  uint32_t value;
  struct ESPUSB_PACKED {
    uint32_t suspend_status : 1; // 0 Suspend status
    uint32_t enum_speed     : 2; // 1..2 Enumerated speed
    uint32_t erratic_err    : 1; // 3 Erratic error
    uint32_t rsv4_7         : 4; // 4..7 Reserved
    uint32_t frame_number   :14; // 8..21 Frame/MicroFrame number
    uint32_t line_status    : 2; // 22..23 Line status
    uint32_t rsv24_31       : 8; // 24..31 Reserved
  };
} EspUsbDwc2Dsts;
ESPUSB_STATIC_ASSERT(sizeof(EspUsbDwc2Dsts) == 4, "EspUsbDwc2Dsts size");
typedef union {
  uint32_t value;
  struct ESPUSB_PACKED {
    uint32_t xfer_size    : 19; // 0..18 Transfer size in bytes
    uint32_t packet_count : 10; // 19..28 Number of packets
    uint32_t mc_pid       :  2; // 29..30 IN: Multi Count, OUT: PID
  };
} EspUsbDwc2EpTsize;
ESPUSB_STATIC_ASSERT(sizeof(EspUsbDwc2EpTsize) == 4, "EspUsbDwc2EpTsize size");
typedef union {
  uint32_t value;
  struct ESPUSB_PACKED {
    uint32_t op_mode                : 3; // 0..2 HNP/SRP Host/Device/OTG mode
    uint32_t arch                   : 2; // 3..4 Slave/External/Internal DMA
    uint32_t single_point           : 1; // 5 0: support hub and split | 1: no hub, no split
    uint32_t hs_phy_type            : 2; // 6..7 0: not supported | 1: UTMI+ | 2: ULPI | 3: UTMI+ and ULPI
    uint32_t fs_phy_type            : 2; // 8..9 0: not supported | 1: dedicated | 2: UTMI+ | 3: ULPI
    uint32_t num_dev_ep             : 4; // 10..13 Number of device endpoints (excluding EP0)
    uint32_t num_host_ch            : 4; // 14..17 Number of host channel (excluding control)
    uint32_t period_channel_support : 1; // 18 Support Periodic OUT Host Channel
    uint32_t enable_dynamic_fifo    : 1; // 19 Dynamic FIFO Sizing Enabled
    uint32_t mul_proc_intrpt        : 1; // 20 Multi-Processor Interrupt enabled (OTG_MULTI_PROC_INTRPT)
    uint32_t reserved21             : 1; // 21 reserved
    uint32_t nptx_q_depth           : 2; // 22..23 Non-periodic request queue depth: 0 = 2.  1 = 4, 2 = 8
    uint32_t ptx_q_depth            : 2; // 24..25 Host periodic request queue depth: 0 = 2.  1 = 4, 2 = 8
    uint32_t token_q_depth          : 5; // 26..30 Device IN token sequence learning queue depth: 0-30
    uint32_t otg_enable_ic_usb      : 1; // 31 IC_USB mode specified for mode of operation
  };
} EspUsbDwc2Ghwcfg2;
ESPUSB_STATIC_ASSERT(sizeof(EspUsbDwc2Ghwcfg2) == 4, "EspUsbDwc2Ghwcfg2 size");
typedef union {
  uint32_t value;
  struct ESPUSB_PACKED {
    uint32_t xfer_size_width          : 4;  // 0..3 Transfer size counter in bits = 11 + n (max 19 bits)
    uint32_t packet_size_width        : 3;  // 4..6 Packet size counter in bits = 4 + n (max 10 bits)
    uint32_t otg_enable               : 1;  // 7 OTG capable
    uint32_t i2c_enable               : 1;  // 8 I2C interface is available
    uint32_t vendor_ctrl_itf          : 1;  // 9 Vendor control interface is available
    uint32_t optional_feature_removed : 1;  // 10 remove User ID, GPIO, SOF toggle & counter to save gate count
    uint32_t synch_reset              : 1;  // 11 0: async reset | 1: synch reset
    uint32_t otg_adp_support          : 1;  // 12 ADP logic is present along with HSOTG controller
    uint32_t otg_enable_hsic          : 1;  // 13 1: HSIC-capable with shared UTMI PHY interface | 0: non-HSIC
    uint32_t battery_charger_support  : 1;  // s14 upport battery charger
    uint32_t lpm_mode                 : 1;  // 15 LPM mode
    uint32_t dfifo_depth              : 16; // DFIFO depth - EP_LOC_CNT in terms of 32-bit words
  };
} EspUsbDwc2Ghwcfg3;
ESPUSB_STATIC_ASSERT(sizeof(EspUsbDwc2Ghwcfg3) == 4, "EspUsbDwc2Ghwcfg3 size");
typedef union {
  uint32_t value;
  struct ESPUSB_PACKED {
    uint32_t num_dev_period_in_ep  : 4; // 0..3 Number of Device Periodic IN Endpoints
    uint32_t partial_powerdown     : 1; // 4 Partial Power Down Enabled
    uint32_t ahb_freq_min          : 1; // 5 1: minimum of AHB frequency is less than 60 MHz
    uint32_t hibernation           : 1; // 6 Hibernation feature is enabled
    uint32_t extended_hibernation  : 1; // 7 Extended Hibernation feature is enabled
    uint32_t reserved8             : 1; // 8 Reserved
    uint32_t enhanced_lpm_support1 : 1; // 9 Enhanced LPM Support1
    uint32_t service_interval_flow : 1; // 10 Service Interval flow is supported
    uint32_t ipg_isoc_support      : 1; // 11 Interpacket GAP ISO OUT worst-case is supported
    uint32_t acg_support           : 1; // 12 Active clock gating is supported
    uint32_t enhanced_lpm_support  : 1; // 13 Enhanced LPM Support
    uint32_t phy_data_width        : 2; // 14..15 0: 8 bits | 1: 16 bits | 2: 8/16 software selectable
    uint32_t ctrl_ep_num           : 4; // 16..19 Number of Device control endpoints in addition to EP0
    uint32_t iddg_filter           : 1; // 20 IDDG Filter Enabled
    uint32_t vbus_valid_filter     : 1; // 21 VBUS Valid Filter Enabled
    uint32_t a_valid_filter        : 1; // 22 A Valid Filter Enabled
    uint32_t b_valid_filter        : 1; // 23 B Valid Filter Enabled
    uint32_t session_end_filter    : 1; // 24 Session End Filter Enabled
    uint32_t dedicated_fifos       : 1; // 25 Dedicated tx fifo for device IN Endpoint
    uint32_t num_dev_in_eps        : 4; // 26..29 Number of Device IN Endpoints including EP0
    uint32_t dma_desc_enabled      : 1; // scatter/gather DMA configuration enabled
    uint32_t dma_desc_dynamic      : 1; // Dynamic scatter/gather DMA
  };
} EspUsbDwc2Ghwcfg4;
ESPUSB_STATIC_ASSERT(sizeof(EspUsbDwc2Ghwcfg4) == 4, "EspUsbDwc2Ghwcfg4 size");

typedef struct {
  union { volatile uint32_t ctl, diepctl, doepctl; };
  uint32_t reserved04;
  union { volatile uint32_t intr, diepint, doepint; };
  uint32_t reserved0c;
  union { volatile uint32_t tsiz, dieptsiz, doeptsiz; };
  union { volatile uint32_t diepdma, doepdma; };
  uint32_t reserved18[2];
} EspUsbDwc2EndpointRegisters;
ESPUSB_STATIC_ASSERT(sizeof(EspUsbDwc2EndpointRegisters) == 0x20, "endpoint stride");

typedef struct {
  volatile uint32_t gotgctl, gotgint, gahbcfg, gusbcfg, grstctl, gintsts, gintmsk;
  uint32_t reserved01c[2];
  volatile uint32_t grxfsiz, dieptxf0;
  uint32_t reserved02c[5];
  volatile uint32_t gsnpsid;
  uint32_t reserved044;
  volatile uint32_t ghwcfg2, ghwcfg3, ghwcfg4;
  uint32_t reserved054[2];
  volatile uint32_t gdfifocfg;
  uint32_t reserved060[41];
  volatile uint32_t dieptxf[15];
  uint32_t reserved140[432];
  volatile uint32_t dcfg, dctl, dsts;
  uint32_t reserved80c;
  volatile uint32_t diepmsk, doepmsk, daint, daintmsk;
  uint32_t reserved820[5];
  volatile uint32_t diepempmsk;
  uint32_t reserved838[50];
  union {
    EspUsbDwc2EndpointRegisters ep[2][16];
    struct { EspUsbDwc2EndpointRegisters epin[16], epout[16]; };
  };
  uint32_t reservedd00[64];
  volatile uint32_t pcgcctl;
} EspUsbDwc2Registers;
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, gotgctl) == 0x0, "gotgctl offset");
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, gotgint) == 0x4, "gotgint offset");
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, gahbcfg) == 0x8, "gahbcfg offset");
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, gusbcfg) == 0xc, "gusbcfg offset");
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, grstctl) == 0x10, "grstctl offset");
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, gintsts) == 0x14, "gintsts offset");
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, gintmsk) == 0x18, "gintmsk offset");
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, grxfsiz) == 0x24, "grxfsiz offset");
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, dieptxf0) == 0x28, "dieptxf0 offset");
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, gsnpsid) == 0x40, "gsnpsid offset");
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, ghwcfg2) == 0x48, "ghwcfg2 offset");
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, ghwcfg3) == 0x4c, "ghwcfg3 offset");
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, ghwcfg4) == 0x50, "ghwcfg4 offset");
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, gdfifocfg) == 0x5c, "gdfifocfg offset");
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, dieptxf) == 0x104, "dieptxf offset");
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, dcfg) == 0x800, "dcfg offset");
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, dctl) == 0x804, "dctl offset");
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, dsts) == 0x808, "dsts offset");
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, diepmsk) == 0x810, "diepmsk offset");
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, doepmsk) == 0x814, "doepmsk offset");
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, daint) == 0x818, "daint offset");
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, daintmsk) == 0x81c, "daintmsk offset");
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, diepempmsk) == 0x834, "diepempmsk offset");
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, epin) == 0x900, "epin offset");
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, epout) == 0xb00, "epout offset");
ESPUSB_STATIC_ASSERT(offsetof(EspUsbDwc2Registers, pcgcctl) == 0xe00, "pcgcctl offset");

enum {
  DCFG_SPEED_FULL_30_60MHZ = 1,
  DCFG_SPEED_FULL_48MHZ = 3,
  DCFG_SPEED_HIGH = 0,
  DCFG_SPEED_LOW = 2,
  DEPCTL_EPTYPE_ISOCHRONOUS = 1,
  GHWCFG2_ARCH_INTERNAL_DMA = 2,
  GHWCFG2_FSPHY_NOT_SUPPORTED = 0,
  GHWCFG2_HSPHY_NOT_SUPPORTED = 0,
  GHWCFG2_HSPHY_ULPI = 2,
};

#define DWC2_CORE_REV_3_00a   0x4f54300a
#define DWC2_CORE_REV_4_20a   0x4f54420a
#define DWC2_CORE_REV_MASK    0x0000ffff
#define DWC2_OTG_ID           0x4f540000
#define DWC2_FS_IOT_ID        0x55310000
#define DWC2_HS_IOT_ID        0x55320000
#define GOTGCTL_AVALOEN_Pos              (4U)
#define GOTGCTL_AVALOEN_Msk              (0x1UL << GOTGCTL_AVALOEN_Pos)           // 0x00000010
#define GOTGCTL_AVALOEN                  GOTGCTL_AVALOEN_Msk                      // A-peripheral session valid override enable
#define GOTGCTL_BVALOEN_Pos              (6U)
#define GOTGCTL_BVALOEN_Msk              (0x1UL << GOTGCTL_BVALOEN_Pos)           // 0x00000040
#define GOTGCTL_BVALOEN                  GOTGCTL_BVALOEN_Msk                      // B-peripheral session valid override enable
#define GOTGCTL_BVALOVAL_Pos             (7U)
#define GOTGCTL_BVALOVAL_Msk             (0x1UL << GOTGCTL_BVALOVAL_Pos)          // 0x00000080
#define GOTGCTL_BVALOVAL                 GOTGCTL_BVALOVAL_Msk                     // B-peripheral session valid override value
#define GOTGINT_SEDET_Pos                (2U)
#define GOTGINT_SEDET_Msk                (0x1UL << GOTGINT_SEDET_Pos)             // 0x00000004
#define GOTGINT_SEDET                    GOTGINT_SEDET_Msk                        // Session end detected
#define DCFG_DSPD_Pos                    (0U)
#define DCFG_DSPD_Msk                    (0x3UL << DCFG_DSPD_Pos)                 // 0x00000003
#define DCFG_DSPD_FS                     3    // Fullspeed on FS PHY
#define DCFG_NZLSOHSK_Pos                (2U)
#define DCFG_NZLSOHSK_Msk                (0x1UL << DCFG_NZLSOHSK_Pos)             // 0x00000004
#define DCFG_NZLSOHSK                    DCFG_NZLSOHSK_Msk                        // Nonzero-length status OUT handshake
#define DCFG_DAD_Pos                     (4U)
#define DCFG_DAD_Msk                     (0x7FUL << DCFG_DAD_Pos)                 // 0x000007F0
#define DCFG_XCVRDLY_Pos                 (14U)
#define DCFG_XCVRDLY_Msk                 (0x1UL << DCFG_XCVRDLY_Pos)             // 0x00004000
#define DCFG_XCVRDLY                     DCFG_XCVRDLY_Msk                        // Enables delay between xcvr_sel and txvalid during device chirp
#define DCTL_RWUSIG_Pos                  (0U)
#define DCTL_RWUSIG_Msk                  (0x1UL << DCTL_RWUSIG_Pos)               // 0x00000001
#define DCTL_RWUSIG                      DCTL_RWUSIG_Msk                          // Remote wakeup signaling
#define DCTL_SDIS_Pos                    (1U)
#define DCTL_SDIS_Msk                    (0x1UL << DCTL_SDIS_Pos)                 // 0x00000002
#define DCTL_SDIS                        DCTL_SDIS_Msk                            // Soft disconnect
#define DCTL_SGONAK_Pos                  (9U)
#define DCTL_SGONAK_Msk                  (0x1UL << DCTL_SGONAK_Pos)               // 0x00000200
#define DCTL_SGONAK                      DCTL_SGONAK_Msk                          // Set global OUT NAK
#define DCTL_CGONAK_Pos                  (10U)
#define DCTL_CGONAK_Msk                  (0x1UL << DCTL_CGONAK_Pos)               // 0x00000400
#define DCTL_CGONAK                      DCTL_CGONAK_Msk                          // Clear global OUT NAK
#define DSTS_FNSOF_Pos                   (8U)
#define DSTS_FNSOF_Msk                   (0x3FFFUL << DSTS_FNSOF_Pos)             // 0x003FFF00
#define DSTS_FNSOF                       DSTS_FNSOF_Msk                           // Frame number of the received SOF
#define GAHBCFG_GINT_Pos                 (0U)
#define GAHBCFG_GINT_Msk                 (0x1UL << GAHBCFG_GINT_Pos)              // 0x00000001
#define GAHBCFG_GINT                     GAHBCFG_GINT_Msk                         // Global interrupt mask
#define GAHBCFG_HBSTLEN_Pos              (1U)
#define GAHBCFG_HBSTLEN_2                (0x3UL << GAHBCFG_HBSTLEN_Pos)           // INCR4
#define GAHBCFG_DMAEN_Pos                (5U)
#define GAHBCFG_DMAEN_Msk                (0x1UL << GAHBCFG_DMAEN_Pos)             // 0x00000020
#define GAHBCFG_DMAEN                    GAHBCFG_DMAEN_Msk                        // DMA enable
#define GUSBCFG_TOCAL_Pos                (0U)
#define GUSBCFG_PHYIF16_Pos              (3U)
#define GUSBCFG_PHYIF16_Msk              (0x1UL << GUSBCFG_PHYIF16_Pos)           // 0x00000008
#define GUSBCFG_PHYIF16                  GUSBCFG_PHYIF16_Msk                      // PHY Interface (PHYIf)
#define GUSBCFG_ULPI_UTMI_SEL_Pos        (4U)
#define GUSBCFG_ULPI_UTMI_SEL_Msk        (0x1UL << GUSBCFG_ULPI_UTMI_SEL_Pos)     // 0x00000010
#define GUSBCFG_ULPI_UTMI_SEL            GUSBCFG_ULPI_UTMI_SEL_Msk                // ULPI or UTMI+ Select (ULPI_UTMI_Sel)
#define GUSBCFG_PHYSEL_Pos               (6U)
#define GUSBCFG_PHYSEL_Msk               (0x1UL << GUSBCFG_PHYSEL_Pos)            // 0x00000040
#define GUSBCFG_PHYSEL                   GUSBCFG_PHYSEL_Msk                       // USB 2.0 high-speed ULPI PHY or USB 1.1 full-speed serial transceiver select
#define GUSBCFG_DDRSEL                   ESPUSB_BIT(7)                                // Single Data Rate (SDR) or Double Data Rate (DDR) or ULPI interface.
#define GUSBCFG_TRDT_Pos                 (10U)
#define GUSBCFG_TRDT_Msk                 (0xFUL << GUSBCFG_TRDT_Pos)              // 0x00003C00
#define GUSBCFG_TRDT                     GUSBCFG_TRDT_Msk                         // USB turnaround time
#define GUSBCFG_ULPIFSLS_Pos             (17U)
#define GUSBCFG_ULPIFSLS_Msk             (0x1UL << GUSBCFG_ULPIFSLS_Pos)          // 0x00020000
#define GUSBCFG_ULPIFSLS                 GUSBCFG_ULPIFSLS_Msk                     // ULPI FS/LS select
#define GUSBCFG_ULPICSM_Pos              (19U)
#define GUSBCFG_ULPICSM_Msk              (0x1UL << GUSBCFG_ULPICSM_Pos)           // 0x00080000
#define GUSBCFG_ULPICSM                  GUSBCFG_ULPICSM_Msk                      // ULPI Clock SuspendM
#define GUSBCFG_ULPIEVBUSD_Pos           (20U)
#define GUSBCFG_ULPIEVBUSD_Msk           (0x1UL << GUSBCFG_ULPIEVBUSD_Pos)        // 0x00100000
#define GUSBCFG_ULPIEVBUSD               GUSBCFG_ULPIEVBUSD_Msk                   // ULPI External VBUS Drive
#define GUSBCFG_ULPIEVBUSI_Pos           (21U)
#define GUSBCFG_ULPIEVBUSI_Msk           (0x1UL << GUSBCFG_ULPIEVBUSI_Pos)        // 0x00200000
#define GUSBCFG_ULPIEVBUSI               GUSBCFG_ULPIEVBUSI_Msk                   // ULPI external VBUS indicator
#define GUSBCFG_FHMOD_Pos                (29U)
#define GUSBCFG_FHMOD_Msk                (0x1UL << GUSBCFG_FHMOD_Pos)             // 0x20000000
#define GUSBCFG_FHMOD                    GUSBCFG_FHMOD_Msk                        // Forced host mode
#define GUSBCFG_FDMOD_Pos                (30U)
#define GUSBCFG_FDMOD_Msk                (0x1UL << GUSBCFG_FDMOD_Pos)             // 0x40000000
#define GUSBCFG_FDMOD                    GUSBCFG_FDMOD_Msk                        // Forced peripheral mode
#define GRSTCTL_CSRST_Pos                (0U)
#define GRSTCTL_CSRST_Msk                (0x1UL << GRSTCTL_CSRST_Pos)             // 0x00000001
#define GRSTCTL_CSRST                    GRSTCTL_CSRST_Msk                        // Core soft reset
#define GRSTCTL_RXFFLSH_Pos              (4U)
#define GRSTCTL_RXFFLSH_Msk              (0x1UL << GRSTCTL_RXFFLSH_Pos)           // 0x00000010
#define GRSTCTL_RXFFLSH                  GRSTCTL_RXFFLSH_Msk                      // RxFIFO flush
#define GRSTCTL_TXFFLSH_Pos              (5U)
#define GRSTCTL_TXFFLSH_Msk              (0x1UL << GRSTCTL_TXFFLSH_Pos)           // 0x00000020
#define GRSTCTL_TXFFLSH                  GRSTCTL_TXFFLSH_Msk                      // TxFIFO flush
#define GRSTCTL_TXFNUM_Pos               (6U)
#define GRSTCTL_CSRST_DONE_Pos           (29)
#define GRSTCTL_CSRST_DONE               (1u << GRSTCTL_CSRST_DONE_Pos)         // Reset Done, only available from v4.20a
#define GRSTCTL_AHBIDL_Pos               (31U)
#define GRSTCTL_AHBIDL_Msk               (0x1UL << GRSTCTL_AHBIDL_Pos)            // 0x80000000
#define GRSTCTL_AHBIDL                   GRSTCTL_AHBIDL_Msk                       // AHB master idle
#define DIEPMSK_XFRCM_Pos                (0U)
#define DIEPMSK_XFRCM_Msk                (0x1UL << DIEPMSK_XFRCM_Pos)             // 0x00000001
#define DIEPMSK_XFRCM                    DIEPMSK_XFRCM_Msk                        // Transfer completed interrupt mask
#define DIEPMSK_TOM_Pos                  (3U)
#define DIEPMSK_TOM_Msk                  (0x1UL << DIEPMSK_TOM_Pos)               // 0x00000008
#define DIEPMSK_TOM                      DIEPMSK_TOM_Msk                          // Timeout condition mask (nonisochronous endpoints)
#define DOEPMSK_XFRCM_Pos                (0U)
#define DOEPMSK_XFRCM_Msk                (0x1UL << DOEPMSK_XFRCM_Pos)             // 0x00000001
#define DOEPMSK_XFRCM                    DOEPMSK_XFRCM_Msk                        // Transfer completed interrupt mask
#define DOEPMSK_STUPM_Pos                (3U)
#define DOEPMSK_STUPM_Msk                (0x1UL << DOEPMSK_STUPM_Pos)             // 0x00000008
#define DOEPMSK_STUPM                    DOEPMSK_STUPM_Msk                        // SETUP phase done mask
#define GINTSTS_OTGINT_Pos               (2U)
#define GINTSTS_OTGINT_Msk               (0x1UL << GINTSTS_OTGINT_Pos)            // 0x00000004
#define GINTSTS_OTGINT                   GINTSTS_OTGINT_Msk                       // OTG interrupt
#define GINTSTS_SOF_Pos                  (3U)
#define GINTSTS_SOF_Msk                  (0x1UL << GINTSTS_SOF_Pos)               // 0x00000008
#define GINTSTS_SOF                      GINTSTS_SOF_Msk                          // Start of frame
#define GINTSTS_BOUTNAKEFF_Pos           (7U)
#define GINTSTS_BOUTNAKEFF_Msk           (0x1UL << GINTSTS_BOUTNAKEFF_Pos)        // 0x00000080
#define GINTSTS_USBSUSP_Pos              (11U)
#define GINTSTS_USBSUSP_Msk              (0x1UL << GINTSTS_USBSUSP_Pos)           // 0x00000800
#define GINTSTS_USBSUSP                  GINTSTS_USBSUSP_Msk                      // USB suspend
#define GINTSTS_USBRST_Pos               (12U)
#define GINTSTS_USBRST_Msk               (0x1UL << GINTSTS_USBRST_Pos)            // 0x00001000
#define GINTSTS_USBRST                   GINTSTS_USBRST_Msk                       // USB reset
#define GINTSTS_ENUMDNE_Pos              (13U)
#define GINTSTS_ENUMDNE_Msk              (0x1UL << GINTSTS_ENUMDNE_Pos)           // 0x00002000
#define GINTSTS_ENUMDNE                  GINTSTS_ENUMDNE_Msk                      // Enumeration done
#define GINTSTS_IEPINT_Pos               (18U)
#define GINTSTS_IEPINT_Msk               (0x1UL << GINTSTS_IEPINT_Pos)            // 0x00040000
#define GINTSTS_IEPINT                   GINTSTS_IEPINT_Msk                       // IN endpoint interrupt
#define GINTSTS_OEPINT_Pos               (19U)
#define GINTSTS_OEPINT_Msk               (0x1UL << GINTSTS_OEPINT_Pos)            // 0x00080000
#define GINTSTS_OEPINT                   GINTSTS_OEPINT_Msk                       // OUT endpoint interrupt
// Device-mode incomplete isochronous OUT; host mode uses this bit for PXFR.
#define GINTSTS_IISOOXFR (1UL << 21)
#define GINTMSK_IISOOXFRM (1UL << 21)
#define GINTSTS_IISOIXFR_Pos             (20U)
#define GINTSTS_IISOIXFR_Msk             (0x1UL << GINTSTS_IISOIXFR_Pos)          // 0x00100000
#define GINTSTS_IISOIXFR                 GINTSTS_IISOIXFR_Msk                     // Incomplete isochronous IN transfer
#define GINTSTS_DISCINT_Pos              (29U)
#define GINTSTS_DISCINT_Msk              (0x1UL << GINTSTS_DISCINT_Pos)           // 0x20000000
#define GINTSTS_DISCINT                  GINTSTS_DISCINT_Msk                      // Disconnect detected interrupt
#define GINTSTS_WKUINT_Pos               (31U)
#define GINTSTS_WKUINT_Msk               (0x1UL << GINTSTS_WKUINT_Pos)            // 0x80000000
#define GINTSTS_WKUINT                   GINTSTS_WKUINT_Msk                       // Resume/remote wakeup detected interrupt
#define GINTMSK_OTGINT_Pos               (2U)
#define GINTMSK_OTGINT_Msk               (0x1UL << GINTMSK_OTGINT_Pos)            // 0x00000004
#define GINTMSK_OTGINT                   GINTMSK_OTGINT_Msk                       // OTG interrupt mask
#define GINTMSK_SOFM_Pos                 (3U)
#define GINTMSK_SOFM_Msk                 (0x1UL << GINTMSK_SOFM_Pos)              // 0x00000008
#define GINTMSK_SOFM                     GINTMSK_SOFM_Msk                         // Start of frame mask
#define GINTMSK_USBSUSPM_Pos             (11U)
#define GINTMSK_USBSUSPM_Msk             (0x1UL << GINTMSK_USBSUSPM_Pos)          // 0x00000800
#define GINTMSK_USBSUSPM                 GINTMSK_USBSUSPM_Msk                     // USB suspend mask
#define GINTMSK_USBRST_Pos               (12U)
#define GINTMSK_USBRST_Msk               (0x1UL << GINTMSK_USBRST_Pos)            // 0x00001000
#define GINTMSK_USBRST                   GINTMSK_USBRST_Msk                       // USB reset mask
#define GINTMSK_ENUMDNEM_Pos             (13U)
#define GINTMSK_ENUMDNEM_Msk             (0x1UL << GINTMSK_ENUMDNEM_Pos)          // 0x00002000
#define GINTMSK_ENUMDNEM                 GINTMSK_ENUMDNEM_Msk                     // Enumeration done mask
#define GINTMSK_IEPINT_Pos               (18U)
#define GINTMSK_IEPINT_Msk               (0x1UL << GINTMSK_IEPINT_Pos)            // 0x00040000
#define GINTMSK_IEPINT                   GINTMSK_IEPINT_Msk                       // IN endpoints interrupt mask
#define GINTMSK_OEPINT_Pos               (19U)
#define GINTMSK_OEPINT_Msk               (0x1UL << GINTMSK_OEPINT_Pos)            // 0x00080000
#define GINTMSK_OEPINT                   GINTMSK_OEPINT_Msk                       // OUT endpoints interrupt mask
#define GINTMSK_IISOIXFRM_Pos            (20U)
#define GINTMSK_IISOIXFRM_Msk            (0x1UL << GINTMSK_IISOIXFRM_Pos)         // 0x00100000
#define GINTMSK_IISOIXFRM                GINTMSK_IISOIXFRM_Msk                    // Incomplete isochronous IN transfer mask
#define GINTMSK_WUIM_Pos                 (31U)
#define GINTMSK_WUIM_Msk                 (0x1UL << GINTMSK_WUIM_Pos)              // 0x80000000
#define GINTMSK_WUIM                     GINTMSK_WUIM_Msk                         // Resume/remote wakeup detected interrupt mask
#define DAINT_IEPINT_Pos                 (0U)
#define DAINT_OEPINT_Pos                 (16U)
#define DAINTMSK_IEPM_Pos                (0U)
#define DAINTMSK_OEPM_Pos                (16U)
#define DAINT_SHIFT(_dir)                (((_dir) == ESPUSB_DIR_IN) ? 0 : 16)
#define DIEPTXF0_TX0FD_Pos               (16U)
#define GDFIFOCFG_EPINFOBASE_SHIFT  16
#define DIEPCTL0_MPSIZ_Pos               (0U)
#define DIEPCTL0_MPSIZ_Msk               (0x3UL << DIEPCTL0_MPSIZ_Pos)            // 0x00000003
#define DIEPCTL_USBAEP_Pos               (15U)
#define DIEPCTL_USBAEP_Msk               (0x1UL << DIEPCTL_USBAEP_Pos)            // 0x00008000
#define DIEPCTL_USBAEP                   DIEPCTL_USBAEP_Msk                       // USB active endpoint
#define DIEPCTL_SNAK_Pos                 (27U)
#define DIEPCTL_SNAK_Msk                 (0x1UL << DIEPCTL_SNAK_Pos)              // 0x08000000
#define DIEPCTL_SNAK                     DIEPCTL_SNAK_Msk                         // Set NAK
#define DIEPCTL_EPDIS_Pos                (30U)
#define DIEPCTL_EPDIS_Msk                (0x1UL << DIEPCTL_EPDIS_Pos)             // 0x40000000
#define DIEPCTL_EPDIS                    DIEPCTL_EPDIS_Msk                        // Endpoint disable
#define DIEPINT_EPDISD_Pos               (1U)
#define DIEPINT_EPDISD_Msk               (0x1UL << DIEPINT_EPDISD_Pos)            // 0x00000002
#define DIEPINT_EPDISD                   DIEPINT_EPDISD_Msk                       // Endpoint disabled interrupt
#define DIEPINT_INEPNE_Pos               (6U)
#define DIEPINT_INEPNE_Msk               (0x1UL << DIEPINT_INEPNE_Pos)            // 0x00000040
#define DIEPINT_INEPNE                   DIEPINT_INEPNE_Msk                       // IN endpoint NAK effective
#define DIEPTXF_INEPTXFD_Pos             (16U)
#define EPCTL_USBAEP_Pos               (15U)
#define EPCTL_USBAEP_Msk               (0x1UL << EPCTL_USBAEP_Pos)            // 0x00008000
#define EPCTL_USBAEP                   EPCTL_USBAEP_Msk                       // USB active endpoint
#define EPCTL_STALL_Pos                (21U)
#define EPCTL_STALL_Msk                (0x1UL << EPCTL_STALL_Pos)             // 0x00200000
#define EPCTL_STALL                    EPCTL_STALL_Msk                        // STALL handshake
#define EPCTL_SD0PID_SEVNFRM_Pos       (28U)
#define EPCTL_SD0PID_SEVNFRM_Msk       (0x1UL << EPCTL_SD0PID_SEVNFRM_Pos)    // 0x10000000
#define EPCTL_SD0PID_SEVNFRM           EPCTL_SD0PID_SEVNFRM_Msk               // Set DATA0 PID
#define EPCTL_EPENA_Pos                (31U)
#define EPCTL_EPENA_Msk                (0x1UL << EPCTL_EPENA_Pos)             // 0x80000000
#define EPCTL_EPENA                    EPCTL_EPENA_Msk                        // Endpoint enable
#define DOEPCTL0_MPSIZ_Pos               (0U)
#define DOEPCTL0_MPSIZ_Msk               (0x3UL << DOEPCTL0_MPSIZ_Pos)            // 0x00000003
#define DOEPCTL_USBAEP_Pos               (15U)
#define DOEPCTL_USBAEP_Msk               (0x1UL << DOEPCTL_USBAEP_Pos)            // 0x00008000
#define DOEPCTL_USBAEP                   DOEPCTL_USBAEP_Msk                       // USB active endpoint
#define DOEPCTL_SNAK_Pos                 (27U)
#define DOEPCTL_SNAK_Msk                 (0x1UL << DOEPCTL_SNAK_Pos)              // 0x08000000
#define DOEPCTL_SNAK                     DOEPCTL_SNAK_Msk                         // Set NAK
#define DOEPCTL_EPDIS_Pos                (30U)
#define DOEPCTL_EPDIS_Msk                (0x1UL << DOEPCTL_EPDIS_Pos)             // 0x40000000
#define DOEPCTL_EPDIS                    DOEPCTL_EPDIS_Msk                        // Endpoint disable
#define DOEPCTL_EPENA_Pos                (31U)
#define DOEPCTL_EPENA_Msk                (0x1UL << DOEPCTL_EPENA_Pos)             // 0x80000000
#define DOEPCTL_EPENA                    DOEPCTL_EPENA_Msk                        // Endpoint enable
#define DOEPINT_EPDISD_Pos               (1U)
#define DOEPINT_EPDISD_Msk               (0x1UL << DOEPINT_EPDISD_Pos)            // 0x00000002
#define DOEPINT_EPDISD                   DOEPINT_EPDISD_Msk                       // Endpoint disabled interrupt
#define DOEPTSIZ_STUPCNT_Pos             (29U)
#define PCGCCTL_RSTPDWNMODULE            ESPUSB_BIT(3)
#define PCGCCTL_PWRCLMP                  ESPUSB_BIT(2)
#define PCGCCTL_GATEHCLK                 ESPUSB_BIT(1)
#define PCGCCTL_STOPPCLK                 ESPUSB_BIT(0)
