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
// Every target here has the internal DMA that dma_device_enabled() looks for:
// ESP-IDF records each core's configuration in soc/usb_dwc_cfg.h, and S2 and S3
// both carry OTG_ARCHITECTURE 2, which is GHWCFG2_ARCH_INTERNAL_DMA. S3 and P4
// are measured on hardware; S2 has only that constant and a compile check
// behind it.
#define CFG_TUD_DWC2_DMA_ENABLE 1
#define CFG_TUD_DWC2_SLAVE_ENABLE 0

// Compile one instance of every device class supported by the v2 function
// model. Whether an instance appears in a device is decided by its descriptor
// graph, not by Arduino-ESP32 Kconfig.
// CDC is the one class the v2 function model can instantiate more than once,
// so its count is a capacity rather than a flag. What bounds it is the
// controller's non-control IN endpoint budget, not RAM: every ACM function
// costs two IN endpoints (notification + data), and validateControllerEndpoints()
// enforces the per-controller ceiling at begin(). S2/S3 have 4 non-control IN
// endpoints, so two ports is the hardware maximum; the P4 HS controller has 7,
// so three. Sizing the compile-time array to exactly that maximum keeps the
// static cost at what the SoC could actually enumerate - a port that could
// never be described is not worth its buffers.
#ifndef CFG_TUD_CDC
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#define CFG_TUD_CDC 3
#else
#define CFG_TUD_CDC 2
#endif
#endif
#define CFG_TUD_MSC 1
#define CFG_TUD_HID 1
#define CFG_TUD_MIDI 1
#define CFG_TUD_AUDIO 1
#define CFG_TUD_VENDOR 1
#define CFG_TUD_NCM 1

// Class buffer sizes. Every one of these is behind #ifndef so a sketch can
// raise it from build_opt.h (-DCFG_TUD_VENDOR_TX_BUFSIZE=8192) without copying
// the library: the flag reaches the library's own translation units because
// Arduino puts build_opt.h on the command line for the whole build. That is the
// one thing the core's precompiled TinyUSB cannot offer - its sizes are baked
// into the shipped sdkconfig - so leaving them unguarded here threw away the
// only lever this library has.
//
// The ceiling is 32768 for every tu_fifo-backed buffer, not a matter of RAM:
// tu_edpt_stream_init() takes the size as uint16_t, and tu_fifo keeps its read
// and write indices in the range [0, 2*depth) with uint16_t indices. 65536
// truncates to a depth of 0 - the device still reports usb_ready but never
// mounts - and anything above 32768 overflows the index space. The #error below
// turns both into a build failure instead of a device that enumerates wrong.
#ifndef CFG_TUD_CDC_RX_BUFSIZE
#define CFG_TUD_CDC_RX_BUFSIZE 512
#endif
#ifndef CFG_TUD_CDC_TX_BUFSIZE
#define CFG_TUD_CDC_TX_BUFSIZE 512
#endif
#ifndef CFG_TUD_MSC_EP_BUFSIZE
#define CFG_TUD_MSC_EP_BUFSIZE 4096
#endif
// HID interrupt endpoint buffer, which is also the ceiling on a single HID
// report: tud_hid_n_report() writes the report ID into byte 0 and copies the
// payload behind it, so the largest report a class may declare is
// CFG_TUD_HID_EP_BUFSIZE - 1.
//
// High speed moves the interesting limit. A full-speed interrupt endpoint tops
// out at 64 bytes per packet, but a high-speed one carries up to 1024 every 125
// us, so the 64 that used to be hard-coded here capped ESP32-P4 HID at 0.5 MB/s
// when the bus could carry eight times that. 512 is the default on P4 for that
// reason and because 1024 has been measured not to enumerate against every host
// (the host's periodic FIFO budget, not this device). The cost is 3 * (512 - 64)
// = 1344 bytes of RAM on P4 whether or not a sketch uses HID, since hid_device.c
// defines its control, IN and OUT buffers statically.
//
// Only EspUsbDeviceHidVendor asks for packets this large. Keyboards, mice,
// gamepads and the composite HID interface size their endpoint from the report
// they actually send (8 or 16 bytes), so raising this changes no descriptor
// they emit.
#ifndef CFG_TUD_HID_EP_BUFSIZE
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#define CFG_TUD_HID_EP_BUFSIZE 512
#else
#define CFG_TUD_HID_EP_BUFSIZE 64
#endif
#endif
#ifndef CFG_TUD_MIDI_RX_BUFSIZE
#define CFG_TUD_MIDI_RX_BUFSIZE 512
#endif
#ifndef CFG_TUD_MIDI_TX_BUFSIZE
#define CFG_TUD_MIDI_TX_BUFSIZE 512
#endif
#ifndef CFG_TUD_VENDOR_RX_BUFSIZE
#define CFG_TUD_VENDOR_RX_BUFSIZE 512
#endif
// Vendor transmit path. Two numbers decide it, and only one of them is the one
// people reach for.
//
// CFG_TUD_VENDOR_TX_BUFSIZE is the FIFO: how much a sketch may queue before
// write() starts returning 0. CFG_TUD_VENDOR_TX_EPSIZE is how much of that FIFO
// one armed transfer carries, and TinyUSB defaults it to a single bulk packet.
// That second default is what actually caps a high-speed stream: the vendor
// class submits one transfer per endpoint and re-arms from the completion
// callback, so at 512 bytes per transfer every packet costs a completion
// interrupt, an event-queue hop and a usbd task turn - about 52 us of turnaround
// for 46 us of wire time, which is why a device measured only ~2.4 transactions
// per microframe out of the 13 high speed allows. DWC2 is happy to send several
// packets per transfer; nothing but this default was stopping it.
//
// Measured on ESP32-P4 rev 1.3 over usbip, 4 MiB per run, median of 9, pattern
// verified on the host, with the sending task pinned to core 0:
//
//   FIFO   transfer   MB/s    global RAM
//    512       512     9.83   (8.33-10.21, and 4-53 ZLP-terminated host URBs)
//   8192       512    10.76   (10.50-11.06, 0)
//   8192      1024    14.87
//   8192      2048    18.64
//   8192      4096    20.99
//   8192      8192    22.81
//   8192     16384    22.78   <- saturated
//   4096      4096    21.12   81,176 bytes
//   8192      8192    22.81   89,368 bytes
//   16384     8192    23.28
//   32768     8192    23.34
//
// So P4 defaults to 4096/4096: it roughly doubles what the previous default did
// (10.76 -> 21.12 MB/s) while using 512 bytes *less* RAM than that default, and
// the 8% more that 8192/8192 buys costs another 8 KB. A sketch that wants the
// last 8% raises both from build_opt.h.
//
// S2/S3 keep 512 and TinyUSB's own transfer size. A full-speed bulk endpoint is
// 64 bytes and tops out near 1.5 MB/s, so eight packets of FIFO is already more
// than that bus drains, and the turnaround this fixes is not what limits it.
#ifndef CFG_TUD_VENDOR_TX_BUFSIZE
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#define CFG_TUD_VENDOR_TX_BUFSIZE 4096
#else
#define CFG_TUD_VENDOR_TX_BUFSIZE 512
#endif
#endif
#ifndef CFG_TUD_VENDOR_TX_EPSIZE
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#define CFG_TUD_VENDOR_TX_EPSIZE 4096
#endif
#endif

#if CFG_TUD_CDC_RX_BUFSIZE > 32768 || CFG_TUD_CDC_TX_BUFSIZE > 32768 ||       \
    CFG_TUD_MIDI_RX_BUFSIZE > 32768 || CFG_TUD_MIDI_TX_BUFSIZE > 32768 ||     \
    CFG_TUD_VENDOR_RX_BUFSIZE > 32768 || CFG_TUD_VENDOR_TX_BUFSIZE > 32768
#error "tu_fifo indices are uint16_t over [0, 2*depth): class FIFOs cannot exceed 32768 bytes"
#endif

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
