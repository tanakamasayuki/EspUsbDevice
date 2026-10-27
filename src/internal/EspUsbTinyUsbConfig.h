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

// CDC-ACM and Vendor use independent instance-owned class drivers.
#if defined(CFG_TUD_CDC_RX_BUFSIZE) || defined(CFG_TUD_CDC_TX_BUFSIZE) || \
    defined(CFG_TUD_VENDOR_RX_BUFSIZE) || defined(CFG_TUD_VENDOR_TX_BUFSIZE) || \
    defined(CFG_TUD_VENDOR_RX_BUFFERED) || defined(CFG_TUD_VENDOR_TX_BUFFERED)
#error "CDC/Vendor buffers and modes use EspUsbBulkStreamConfig; remove TinyUSB build flags"
#endif
#if defined(CFG_TUD_CDC) && CFG_TUD_CDC != 0
#error "CDC uses runtime instance resources; do not enable TinyUSB CDC"
#endif
#undef CFG_TUD_CDC
#define CFG_TUD_CDC 0
// The remaining legacy classes are compiled in by default. CDC and Vendor
// use independent drivers, as do HID/MIDI/MSC; these figures describe v2.
// Independent CDC, Vendor, HID, MIDI, MSC and Audio drivers allocate only the
// resources selected by each Function. Keep their legacy TinyUSB classes off
// so the Arduino core's archive cannot supply a second class implementation.
// Video still uses the legacy class path during this migration.
#if defined(CFG_TUD_MSC) && CFG_TUD_MSC != 0
#error "MSC uses runtime resources; do not enable TinyUSB MSC"
#endif
#undef CFG_TUD_MSC
#define CFG_TUD_MSC 0
#if defined(CFG_TUD_HID) && CFG_TUD_HID != 0
#error "HID uses runtime report resources; do not enable TinyUSB HID"
#endif
#undef CFG_TUD_HID
#define CFG_TUD_HID 0
#if defined(CFG_TUD_MIDI) && CFG_TUD_MIDI != 0
#error "MIDI uses runtime instance resources; do not enable TinyUSB MIDI"
#endif
#undef CFG_TUD_MIDI
#define CFG_TUD_MIDI 0
#if defined(CFG_TUD_AUDIO) && CFG_TUD_AUDIO != 0
#error "Audio uses runtime resources; do not enable TinyUSB Audio"
#endif
#undef CFG_TUD_AUDIO
#define CFG_TUD_AUDIO 0
#ifndef CFG_TUD_VENDOR
#define CFG_TUD_VENDOR 0
#endif
#if CFG_TUD_VENDOR
#error "v3 Vendor uses instance configuration, not the TinyUSB Vendor driver"
#endif
#if defined(CFG_TUD_NCM) && CFG_TUD_NCM
#error "NCM uses runtime Function resources; do not enable TinyUSB NCM"
#endif
#undef CFG_TUD_NCM
#define CFG_TUD_NCM 0
#if defined(CFG_TUD_NCM_IN_NTB_N) || defined(CFG_TUD_NCM_OUT_NTB_N) || \
    defined(CFG_TUD_NCM_IN_NTB_MAX_SIZE) || defined(CFG_TUD_NCM_OUT_NTB_MAX_SIZE) || \
    defined(CFG_TUD_NCM_IN_MAX_DATAGRAMS_PER_NTB) || defined(CFG_TUD_NCM_OUT_MAX_DATAGRAMS_PER_NTB) || defined(CFG_TUD_NET_MTU)
#error "NCM storage uses EspUsbNetConfig; remove TinyUSB NCM buffer flags"
#endif
#if (defined(CFG_TUD_VIDEO) && CFG_TUD_VIDEO) || (defined(CFG_TUD_VIDEO_STREAMING) && CFG_TUD_VIDEO_STREAMING)
#error "Video uses runtime Function resources; do not enable TinyUSB Video"
#endif
#undef CFG_TUD_VIDEO
#undef CFG_TUD_VIDEO_STREAMING
#define CFG_TUD_VIDEO 0
#define CFG_TUD_VIDEO_STREAMING 0
#if defined(CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE) || defined(CFG_TUD_VIDEO_STREAMING_BULK)
#error "Video uses EspUsbVideoConfig; remove TinyUSB Video buffer/transport flags"
#endif
// DFU Runtime and Mode also use independent per-Function drivers.
#if (defined(CFG_TUD_DFU) && CFG_TUD_DFU) || (defined(CFG_TUD_DFU_RUNTIME) && CFG_TUD_DFU_RUNTIME)
#error "DFU uses runtime Function resources; do not enable TinyUSB DFU"
#endif
#undef CFG_TUD_DFU
#undef CFG_TUD_DFU_RUNTIME
#define CFG_TUD_DFU 0
#define CFG_TUD_DFU_RUNTIME 0
#ifdef CFG_TUD_DFU_XFER_BUFSIZE
#error "DFU storage uses EspUsbDfuConfig; remove TinyUSB DFU buffer flags"
#endif
#ifdef CFG_TUD_MSC_EP_BUFSIZE
#error "MSC storage uses EspUsbMscConfig; remove TinyUSB MSC buffer flags"
#endif
#if defined(CFG_TUD_HID_EP_BUFSIZE) || defined(CFG_TUD_HID_BUFSIZE)
#error "HID storage is derived from report descriptors; remove TinyUSB HID build flags"
#endif
#if defined(CFG_TUD_MIDI_RX_BUFSIZE) || defined(CFG_TUD_MIDI_TX_BUFSIZE) || \
    defined(CFG_TUD_MIDI_RX_EPSIZE) || defined(CFG_TUD_MIDI_TX_EPSIZE) || defined(CFG_TUD_MIDI_EP_BUFSIZE)
#error "MIDI packet buffers use EspUsbMidiConfig; remove TinyUSB MIDI build flags"
#endif

// Audio storage and packet sizes come from the selected runtime formats.
#if defined(CFG_TUD_AUDIO_CTRL_BUF_SZ) || defined(CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX) || \
    defined(CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX) || defined(CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ) || \
    defined(CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ) || defined(ESP_USB_TINYUSB_AUDIO_MAX_SAMPLE_RATE) || \
    defined(ESP_USB_TINYUSB_AUDIO_SW_PACKETS)
#error "Audio storage uses EspUsbAudioConfig and selected formats; remove TinyUSB Audio buffer flags"
#endif

#if defined(CFG_TUD_TASK_QUEUE_SZ)
#error "USB event storage uses EspUsbDeviceConfig::eventQueueDepth; remove CFG_TUD_TASK_QUEUE_SZ"
#endif

#if defined(CFG_TUD_INTERFACE_MAX) || defined(CFG_TUD_ENDPPOINT_MAX) || defined(CFG_TUD_ENDPOINT0_BUFSIZE) || defined(CFG_TUD_TASK_EVENTS_PER_RUN)
#error "USB core maps, packets and drain budget are derived at runtime; remove TinyUSB core capacity flags"
#endif
