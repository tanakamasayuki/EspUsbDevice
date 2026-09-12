#include <stdint.h>

#define OPT_MCU_ESP32S2 900
#define OPT_MCU_ESP32S3 901
#define OPT_MCU_ESP32P4 907
#define OPT_OS_FREERTOS 2
#define OPT_MODE_FULL_SPEED 0x0200u
#define OPT_MODE_HIGH_SPEED 0x0400u
#define TU_ATTR_ALIGNED(_n) __attribute__((aligned(_n)))
#define TUD_AUDIO_EP_SIZE(_hs, _rate, _bytes, _channels)                 \
  ((((_rate) + ((_hs) ? 7999 : 999)) / ((_hs) ? 8000 : 1000) + 1) *    \
   (_bytes) * (_channels))

#include "internal/EspUsbTinyUsbConfig.h"

static_assert(CFG_TUD_ENABLED == 1, "device stack must be enabled");
static_assert(CFG_TUH_ENABLED == 0, "host stack must not be compiled");
// Device DMA is enabled to keep bulk IN off the slave-mode FIFO refill path
// that permanently stalled CDC-NCM. The cache-coherency audit it needs is
// satisfied on P4 (the only target with an L1 data cache over internal SRAM) by
// tusb_mcu.h enabling dcache maintenance whenever DMA is on. Slave mode stays
// compiled in as the run-time fallback for a controller whose GHWCFG2 reports
// no internal DMA, so neither may be turned off here.
// Exactly one mode, always. Enabling both silently breaks every class built on
// tu_edpt_stream (CDC, MIDI, Vendor) because CFG_TUD_EDPT_DEDICATED_HWFIFO
// follows the slave flag and would have those classes transfer from a tu_fifo
// that the DMA path reads as a NULL pointer - the host then receives whatever
// lives at address 0.
static_assert(CFG_TUD_DWC2_DMA_ENABLE + CFG_TUD_DWC2_SLAVE_ENABLE == 1,
              "DWC2 transfer modes are mutually exclusive");
static_assert(CFG_TUD_DWC2_DMA_ENABLE == 1,
              "device DMA avoids the slave-mode FIFO refill stall");
static_assert(CFG_TUD_MSC == 1 && CFG_TUD_HID == 1,
              "non-Audio classes must be library-owned");
// CDC is the one multi-instance class: its count is the number of serial ports
// the SoC's non-control IN endpoints could ever describe (2 IN per ACM
// function), so that no port is compiled that could not be enumerated.
#if defined(CONFIG_IDF_TARGET_ESP32P4)
static_assert(CFG_TUD_CDC == 3, "P4 HS controller admits 3 CDC ports");
#else
static_assert(CFG_TUD_CDC == 2, "S2/S3 admit 2 CDC ports");
#endif
static_assert(CFG_TUD_MIDI == 1 && CFG_TUD_VENDOR == 1 && CFG_TUD_NCM == 1,
              "non-Audio classes must be library-owned");
static_assert(CFG_TUD_AUDIO == 1, "Audio capacity must be compiled");
static_assert(CFG_TUD_AUDIO_MAX_N_CHANNELS == 2, "mono/stereo capacity");
static_assert(CFG_TUD_AUDIO_MAX_N_BYTES_PER_SAMPLE == 4,
              "16/24/32-bit PCM capacity");
static_assert(CFG_TUD_AUDIO_CTRL_BUF_SZ >= 2 + 8 * 12,
              "UAC2 discrete sample-rate RANGE response capacity");

#if defined(CONFIG_IDF_TARGET_ESP32P4)
static_assert(CFG_TUSB_MCU == OPT_MCU_ESP32P4, "P4 MCU selection");
static_assert(CFG_TUD_MAX_SPEED == OPT_MODE_HIGH_SPEED, "P4 HS capacity");
static_assert(ESP_USB_TINYUSB_AUDIO_HS_EP_SIZE == 200,
              "192 kHz stereo 32-bit HS packet");
static_assert(CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX == 776,
              "P4 capacity covers HS controller negotiating FS");
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
static_assert(CFG_TUSB_MCU == OPT_MCU_ESP32S3, "S3 MCU selection");
static_assert(CFG_TUD_MAX_SPEED == OPT_MODE_FULL_SPEED, "S3 FS capacity");
static_assert(CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX == 776,
              "96 kHz stereo 32-bit FS packet capacity");
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
static_assert(CFG_TUSB_MCU == OPT_MCU_ESP32S2, "S2 MCU selection");
static_assert(CFG_TUD_MAX_SPEED == OPT_MODE_FULL_SPEED, "S2 FS capacity");
static_assert(CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX == 776,
              "96 kHz stereo 32-bit FS packet capacity");
#endif

// Class buffer sizes are overridable from build_opt.h, which is the whole point
// of the library carrying its own tusb_config.h: the core's precompiled TinyUSB
// bakes these into a shipped sdkconfig and no sketch can move them.
#if !defined(CFG_TUD_VENDOR_TX_BUFSIZE) || !defined(CFG_TUD_VENDOR_RX_BUFSIZE) || \
    !defined(CFG_TUD_HID_EP_BUFSIZE) || !defined(CFG_TUD_CDC_TX_BUFSIZE) ||       \
    !defined(CFG_TUD_CDC_RX_BUFSIZE) || !defined(CFG_TUD_MIDI_TX_BUFSIZE) ||      \
    !defined(CFG_TUD_MIDI_RX_BUFSIZE) || !defined(CFG_TUD_MSC_EP_BUFSIZE)
#error "every class buffer size must be defined"
#endif

// tu_edpt_stream_init() takes the FIFO size as uint16_t and tu_fifo runs its
// indices over [0, 2*depth), so nothing tu_fifo-backed may exceed 32768. A
// 65536-byte vendor FIFO truncates to a depth of 0: the device reports ready and
// never mounts. The header turns that into a build failure instead.
static_assert(CFG_TUD_VENDOR_TX_BUFSIZE <= 32768, "vendor TX FIFO within tu_fifo index space");
static_assert(CFG_TUD_VENDOR_RX_BUFSIZE <= 32768, "vendor RX FIFO within tu_fifo index space");
static_assert(CFG_TUD_CDC_TX_BUFSIZE <= 32768, "CDC TX FIFO within tu_fifo index space");
static_assert(CFG_TUD_MIDI_TX_BUFSIZE <= 32768, "MIDI TX FIFO within tu_fifo index space");

#if defined(CONFIG_IDF_TARGET_ESP32P4)
// Two numbers, not one. The FIFO is how much a sketch may queue; the transfer
// size is how much of it one armed transfer carries, and TinyUSB defaults that
// to a single bulk packet - which is what actually capped a high-speed stream,
// because every packet then cost a completion interrupt and a usbd task turn.
// Measured 9.83 MB/s at 512/512 against 21.12 at 4096/4096, using less RAM than
// the 8192/512 in between.
static_assert(CFG_TUD_VENDOR_TX_BUFSIZE == 4096, "P4 vendor TX FIFO is 4 KiB");
static_assert(CFG_TUD_VENDOR_TX_EPSIZE == 4096, "P4 vendor transfer is 8 packets");
static_assert(CFG_TUD_VENDOR_TX_EPSIZE <= CFG_TUD_VENDOR_TX_BUFSIZE,
              "a transfer cannot carry more than the FIFO holds");
// A high-speed interrupt endpoint carries up to 1024 bytes every 125 us. 64
// capped EspUsbDeviceHidVendor at an eighth of what the bus could move, for no
// reason on the device side; 1024 has been measured not to enumerate against
// every host, so 512.
static_assert(CFG_TUD_HID_EP_BUFSIZE == 512, "P4 HID endpoint buffer is 512");
#else
// Full speed: bulk is 64 bytes and tops out near 1.5 MB/s, so eight packets of
// FIFO is already more than the bus drains, and an interrupt endpoint may not
// exceed 64 bytes at all.
static_assert(CFG_TUD_VENDOR_TX_BUFSIZE == 512, "S2/S3 vendor TX FIFO is 512");
static_assert(CFG_TUD_HID_EP_BUFSIZE == 64, "S2/S3 HID endpoint buffer is 64");
// The transfer size is left to TinyUSB here: a full-speed bulk endpoint is 64
// bytes and the turnaround the P4 default fixes is not what limits this bus.
#ifdef CFG_TUD_VENDOR_TX_EPSIZE
#error "the vendor transfer size is a high-speed knob; S2/S3 keep TinyUSB's"
#endif
#endif

int main() { return 0; }
