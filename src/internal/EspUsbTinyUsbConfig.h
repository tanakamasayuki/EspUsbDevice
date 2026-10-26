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
// NCM and Video still use the legacy class path during this migration.
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
#ifndef CFG_TUD_NCM
#define CFG_TUD_NCM 1
#endif
#ifndef CFG_TUD_VIDEO
#define CFG_TUD_VIDEO 1
#endif
// Streaming interfaces per video function. One camera, one stream.
#ifndef CFG_TUD_VIDEO_STREAMING
#define CFG_TUD_VIDEO_STREAMING 1
#endif
// DFU in both shapes. They are separate TinyUSB drivers with separate interface
// protocols (DFU_PROTOCOL_RT vs DFU_PROTOCOL_DFU), so both can be compiled in
// and the host's descriptor match decides which one opens; EspUsbDeviceDfu
// emits one or the other from its constructor's mode.
//
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

// TinyUSB defaults both NCM NTB pools to 1, which leaves the transmitter with a
// single buffer: it can only ever have one NTB in flight, so every frame waits
// for the previous transfer to complete. Upstream measures up to 50% more
// throughput at 2 and no "request blocked" at 3 (see class/net/ncm.h). Three
// 3200-byte transmit NTBs cost ~9.6 KB of USB-capable RAM, which is worth it on
// the S3/P4 parts this library targets.
#define CFG_TUD_NCM_IN_NTB_N 3
#define CFG_TUD_NCM_OUT_NTB_N 2

// Audio storage and packet sizes come from the selected runtime formats.
#if defined(CFG_TUD_AUDIO_CTRL_BUF_SZ) || defined(CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX) || \
    defined(CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX) || defined(CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ) || \
    defined(CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ) || defined(ESP_USB_TINYUSB_AUDIO_MAX_SAMPLE_RATE) || \
    defined(ESP_USB_TINYUSB_AUDIO_SW_PACKETS)
#error "Audio storage uses EspUsbAudioConfig and selected formats; remove TinyUSB Audio buffer flags"
#endif

// UVC payload staging buffer, one per streaming interface, statically
// allocated. The class driver assembles one payload here - UVC header plus
// frame bytes - and this is the cap on dwMaxPayloadTransferSize it commits to.
//
// It must not exceed the isochronous endpoint's wMaxPacketSize, because
// TinyUSB refuses to open the streaming alternate setting unless the endpoint
// is at least as large as the committed payload (video_device.c,
// _open_vs_itf). A full-speed isochronous endpoint carries at most 1023 bytes
// per 1 ms frame, so 1023 is the largest value that works at both speeds -
// 1024 leaves a full-speed host unable to start the stream at all, which it
// reports as a failure to build its capture graph rather than as anything
// about descriptors. Measured on Windows 11 with ffmpeg's DirectShow input.
// Two limits meet here, and the smaller one wins.
//
// The transport limit is 1023: a full-speed isochronous endpoint carries at
// most that per 1 ms frame, and TinyUSB refuses to open the streaming
// alternate setting unless the endpoint is at least as large as the committed
// payload (video_device.c, _open_vs_itf). 1024 leaves a full-speed host unable
// to start the stream at all, which it reports as a failure to build its
// capture graph rather than as anything about descriptors.
//
// The controller limit is smaller still on the S2 and S3, and it is the one
// that bites. Their DWC2 has a 256-word (1 KB) FIFO SPRAM shared by the
// receive FIFO, every transmit FIFO and the DMA endpoint-info area; after
// EP0 and the receive FIFO there are about 164 words left, so a 1023-byte
// endpoint (256 words) does not fit. Measured on an ESP32-S3 with a 1023-byte
// endpoint: DIEPTXF1 was programmed 256 words at offset 512, outside the
// 242-word usable area, the device reported sending frames, and not one byte
// reached the host - followed some seconds later by a crash in an unrelated
// EP0 path. 512 bytes (128 words) streams correctly.
//
// EspUsbDevice::begin() checks the whole configuration against the same FIFO
// budget and fails rather than enumerate a camera that cannot transmit, so
// raising this is safe to try: it either works or begin() says it does not.
#ifndef CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#define CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE 1023
#else
#define CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE 512
#endif
#endif
#if CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE > 1023
#error "CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE must be 1023 or less: a larger payload than a full-speed isochronous endpoint can carry stops the host starting the stream"
#endif
// Isochronous by default, which is what a camera is: the host reserves
// bandwidth up front and never retries a lost packet. Set to 1 from
// build_opt.h for bulk streaming, which retries and therefore cannot drop a
// frame, but competes with every other bulk endpoint for whatever bandwidth is
// left. Windows' usbvideo.sys accepts both. This is a build-wide switch
// because the class driver compiles one shape or the other, so a sketch that
// changes it must rebuild the library (arduino-cli --clean).
#ifndef CFG_TUD_VIDEO_STREAMING_BULK
#define CFG_TUD_VIDEO_STREAMING_BULK 0
#endif
