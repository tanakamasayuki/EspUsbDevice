#pragma once

// TinyUSB is compiled once for the capabilities of the selected SoC. The USB
// controller, root-hub port, and negotiated bus speed are selected later by the
// EspUsbDevice runtime through tusb_rhport_init().

#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif

#if defined(CONFIG_IDF_TARGET_ESP32S2)
#define CFG_TUSB_MCU OPT_MCU_ESP32S2
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define CFG_TUSB_MCU OPT_MCU_ESP32S3
#elif defined(CONFIG_IDF_TARGET_ESP32P4)
#define CFG_TUSB_MCU OPT_MCU_ESP32P4
#else
#error "EspUsbDevice v2 TinyUSB supports ESP32-S2, ESP32-S3, and ESP32-P4"
#endif

#define CFG_TUSB_OS OPT_OS_FREERTOS
#define CFG_TUSB_DEBUG 0

// Do not define CFG_TUSB_RHPORT0_MODE or CFG_TUSB_RHPORT1_MODE. A fixed
// TUD_OPT_RHPORT would make the P4 controller choice a build-time decision.
#define CFG_TUD_ENABLED 1
#define CFG_TUH_ENABLED 0

#if defined(CONFIG_IDF_TARGET_ESP32P4)
#define CFG_TUD_MAX_SPEED OPT_MODE_HIGH_SPEED
#define CFG_TUSB_MEM_ALIGN TU_ATTR_ALIGNED(64)
#else
#define CFG_TUD_MAX_SPEED OPT_MODE_FULL_SPEED
#define CFG_TUSB_MEM_ALIGN TU_ATTR_ALIGNED(4)
#endif

#define CFG_TUSB_MEM_SECTION
#define CFG_TUD_ENDPOINT0_SIZE 64
// DWC2 transfer mode. Slave mode has the CPU push every packet into the
// controller's TxFIFO, refilling it from the FIFO-empty interrupt that
// handle_epin_slave() disarms as soon as the last byte is written. A sustained
// bulk IN stream can end up with the endpoint enabled, packets still
// outstanding, an empty FIFO and that interrupt already cleared - a transfer
// nothing can feed again, which killed CDC-NCM device-to-host traffic within
// seconds. DMA mode does not use that path.
//
// Cache coherency is upstream's problem here and upstream solves it: P4 is the
// only target of the three that reaches internal SRAM through an L1 data cache,
// and tusb_mcu.h turns dcache maintenance on for it precisely when DMA is on
// (CFG_TUD_MEM_DCACHE_ENABLE_DEFAULT = CFG_TUD_DWC2_DMA_ENABLE, line size 64,
// matching CONFIG_CACHE_L1_CACHE_LINE_SIZE). TUD_EPBUF_TYPE_DEF then aligns and
// pads every endpoint buffer to a whole cache line, so no DMA buffer shares a
// line with anything else. S2/S3 have no such cache and need none of it.
//
// The two modes are mutually exclusive by construction, not merely by
// preference: tusb_option.h derives CFG_TUD_EDPT_DEDICATED_HWFIFO from
// CFG_TUD_DWC2_SLAVE_ENABLE, and that flag decides whether the shared
// tu_edpt_stream layer (CDC, MIDI, Vendor) hands the driver a real buffer or a
// tu_fifo. Leaving slave mode on while the controller actually runs DMA makes
// those classes call usbd_edpt_xfer_fifo(), whose xfer->buffer is NULL, so the
// endpoint DMAs from address 0 and the host receives garbage. Enable exactly
// one.
//
// S2 keeps slave mode: whether its controller reports internal DMA cannot be
// checked without the hardware, and because the choice is compile-time there is
// no run-time fallback if it does not - the device would simply have no
// transfer path. S3 and P4 are both measured. The cost is that S2 keeps the
// stall, which needs an S2 board to fix responsibly.
#if defined(CONFIG_IDF_TARGET_ESP32S2)
#define CFG_TUD_DWC2_DMA_ENABLE 0
#define CFG_TUD_DWC2_SLAVE_ENABLE 1
#else
#define CFG_TUD_DWC2_DMA_ENABLE 1
#define CFG_TUD_DWC2_SLAVE_ENABLE 0
#endif

// Compile one instance of every device class supported by the v2 function
// model. Whether an instance appears in a device is decided by its descriptor
// graph, not by Arduino-ESP32 Kconfig.
#define CFG_TUD_CDC 1
#define CFG_TUD_MSC 1
#define CFG_TUD_HID 1
#define CFG_TUD_MIDI 1
#define CFG_TUD_AUDIO 1
#define CFG_TUD_VENDOR 1
#define CFG_TUD_NCM 1

#define CFG_TUD_CDC_RX_BUFSIZE 512
#define CFG_TUD_CDC_TX_BUFSIZE 512
#define CFG_TUD_MSC_EP_BUFSIZE 4096
#define CFG_TUD_HID_EP_BUFSIZE 64
#define CFG_TUD_MIDI_RX_BUFSIZE 512
#define CFG_TUD_MIDI_TX_BUFSIZE 512
#define CFG_TUD_VENDOR_RX_BUFSIZE 512
#define CFG_TUD_VENDOR_TX_BUFSIZE 512

// TinyUSB defaults both NCM NTB pools to 1, which leaves the transmitter with a
// single buffer: it can only ever have one NTB in flight, so every frame waits
// for the previous transfer to complete. Upstream measures up to 50% more
// throughput at 2 and no "request blocked" at 3 (see class/net/ncm.h). Three
// 3200-byte transmit NTBs cost ~9.6 KB of USB-capable RAM, which is worth it on
// the S3/P4 parts this library targets.
#define CFG_TUD_NCM_IN_NTB_N 3
#define CFG_TUD_NCM_OUT_NTB_N 2

// These are compile-time capacities, not a fixed Audio Card topology.
// Descriptor validation will reject formats that exceed the selected bus and
// controller limits.
#define CFG_TUD_AUDIO_ENABLE_EP_IN 1
#define CFG_TUD_AUDIO_ENABLE_EP_OUT 1
#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP 1
#define CFG_TUD_AUDIO_ENABLE_INTERRUPT_EP 0
#define CFG_TUD_AUDIO_MAX_N_CHANNELS 2
#define CFG_TUD_AUDIO_MAX_N_BYTES_PER_SAMPLE 4
#define CFG_TUD_AUDIO_CTRL_BUF_SZ 128

#if defined(CONFIG_IDF_TARGET_ESP32P4)
#define ESP_USB_TINYUSB_AUDIO_MAX_SAMPLE_RATE 192000
#define ESP_USB_TINYUSB_AUDIO_SW_PACKETS 8
#else
#define ESP_USB_TINYUSB_AUDIO_MAX_SAMPLE_RATE 96000
#define ESP_USB_TINYUSB_AUDIO_SW_PACKETS 4
#endif

// P4's HS controller may negotiate Full Speed. Compile-time storage therefore
// covers the largest packet from either negotiated speed; descriptor/runtime
// validation still decides which rates are legal for the current connection.
#define ESP_USB_TINYUSB_AUDIO_FS_EP_SIZE                                  \
  TUD_AUDIO_EP_SIZE(false, 96000,                                        \
                    CFG_TUD_AUDIO_MAX_N_BYTES_PER_SAMPLE,                 \
                    CFG_TUD_AUDIO_MAX_N_CHANNELS)
#define ESP_USB_TINYUSB_AUDIO_HS_EP_SIZE                                  \
  TUD_AUDIO_EP_SIZE(true, ESP_USB_TINYUSB_AUDIO_MAX_SAMPLE_RATE,          \
                    CFG_TUD_AUDIO_MAX_N_BYTES_PER_SAMPLE,                 \
                    CFG_TUD_AUDIO_MAX_N_CHANNELS)
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX                                 \
  (ESP_USB_TINYUSB_AUDIO_FS_EP_SIZE > ESP_USB_TINYUSB_AUDIO_HS_EP_SIZE    \
       ? ESP_USB_TINYUSB_AUDIO_FS_EP_SIZE                                 \
       : ESP_USB_TINYUSB_AUDIO_HS_EP_SIZE)
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ                             \
  (ESP_USB_TINYUSB_AUDIO_SW_PACKETS * CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX)
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ                            \
  (ESP_USB_TINYUSB_AUDIO_SW_PACKETS * CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX)
