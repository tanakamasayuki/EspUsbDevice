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
#define CFG_TUD_DWC2_DMA_ENABLE 1
#define CFG_TUD_DWC2_SLAVE_ENABLE 0

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
