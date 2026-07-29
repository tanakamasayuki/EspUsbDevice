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
static_assert(CFG_TUD_CDC == 1 && CFG_TUD_MSC == 1 && CFG_TUD_HID == 1,
              "non-Audio classes must be library-owned");
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

int main() { return 0; }
