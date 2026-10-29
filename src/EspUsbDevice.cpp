#include "EspUsbDevice.h"
#include "internal/EspUsbHidDescriptor.h"
#include "internal/EspUsbRuntime.h"
#include "internal/EspUsbMemory.h"
#include "internal/EspUsbDeviceRestart.h"
#include <memory>
#include <algorithm>

#include <ctype.h>
#include <new>
#include <string.h>
#include "keymap/keymap_da_dk.h"
#include "keymap/keymap_de_de.h"
#include "keymap/keymap_en_gb.h"
#include "keymap/keymap_en_us.h"
#include "keymap/keymap_es_es.h"
#include "keymap/keymap_fi_fi.h"
#include "keymap/keymap_fr_ch.h"
#include "keymap/keymap_fr_fr.h"
#include "keymap/keymap_hu_hu.h"
#include "keymap/keymap_it_it.h"
#include "keymap/keymap_ja_jp.h"
#include "keymap/keymap_nb_no.h"
#include "keymap/keymap_nl_nl.h"
#include "keymap/keymap_pt_br.h"
#include "keymap/keymap_pt_pt.h"
#include "keymap/keymap_sv_se.h"

#if __has_include("soc/soc_caps.h")
#include "soc/soc_caps.h"
#endif

#if defined(SOC_USB_OTG_SUPPORTED) && SOC_USB_OTG_SUPPORTED
#include "internal/EspUsbDeviceBus.h"
#include "internal/EspUsbDwc2Platform.h"
// espusb_device_edpt_claim() / espusb_device_edpt_xfer(), for EspUsbDeviceVendor::writeDirect().
#include "internal/EspUsbDeviceEndpoint.h"
#if __has_include("esp_memory_utils.h")
#include "esp_memory_utils.h"
#define ESP_USB_DEVICE_HAS_DMA_CAPABLE_CHECK 1
#else
#define ESP_USB_DEVICE_HAS_DMA_CAPABLE_CHECK 0
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#define ESP_USB_DEVICE_HAS_OTG 1
#if __has_include("esp_mac.h")
#include "esp_mac.h"
#define ESP_USB_DEVICE_HAS_ESP_MAC 1
#else
#define ESP_USB_DEVICE_HAS_ESP_MAC 0
#endif
#else
#define ESP_USB_DEVICE_HAS_OTG 0
#define ESP_USB_DEVICE_HAS_ESP_MAC 0
#endif

// OTA partition access for EspUsbDeviceFirmwareUpdate. Part of ESP-IDF's
// app_update component, which Arduino-ESP32 always builds - unlike Arduino's
// Update library, referencing it adds no Arduino library dependency to a sketch
// that never touches firmware update.
#if __has_include("esp_ota_ops.h")
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_err.h"
#define ESP_USB_DEVICE_HAS_OTA 1
#else
#define ESP_USB_DEVICE_HAS_OTA 0
#endif

// The always-on register that asks the ROM to stay in its download loader after
// the next reset, plus the S2/S3 ROM persist flags that ask it to come up as a
// DFU device rather than a serial loader.
#if __has_include("esp_system.h")
#include "esp_system.h"
#include "soc/soc.h"
#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
#include "soc/rtc_cntl_reg.h"
#if defined(CONFIG_IDF_TARGET_ESP32S3) && __has_include("soc/usb_serial_jtag_reg.h")
#include "soc/usb_serial_jtag_reg.h"
#define ESP_USB_DEVICE_HAS_SHARED_PHY 1
#else
#define ESP_USB_DEVICE_HAS_SHARED_PHY 0
#endif
#define ESP_USB_DEVICE_HAS_DOWNLOAD_BOOT 1
#elif defined(CONFIG_IDF_TARGET_ESP32P4)
#include "soc/lp_system_reg.h"
#define ESP_USB_DEVICE_HAS_DOWNLOAD_BOOT 1
#define ESP_USB_DEVICE_HAS_SHARED_PHY 0
#else
#define ESP_USB_DEVICE_HAS_DOWNLOAD_BOOT 0
#define ESP_USB_DEVICE_HAS_SHARED_PHY 0
#endif
#if defined(CONFIG_IDF_TARGET_ESP32S3) && __has_include("esp32s3/rom/usb/usb_persist.h")
#include "esp32s3/rom/usb/usb_persist.h"
#include "esp32s3/rom/usb/chip_usb_dw_wrapper.h"
#if __has_include("esp_efuse.h") && __has_include("esp_efuse_table.h")
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#define ESP_USB_DEVICE_HAS_PHY_SEL_EFUSE 1
#else
#define ESP_USB_DEVICE_HAS_PHY_SEL_EFUSE 0
#endif
#define ESP_USB_DEVICE_HAS_ROM_DFU 1
#elif defined(CONFIG_IDF_TARGET_ESP32S2) && __has_include("esp32s2/rom/usb/usb_persist.h")
#include "esp32s2/rom/usb/usb_persist.h"
#include "esp32s2/rom/usb/chip_usb_dw_wrapper.h"
#define ESP_USB_DEVICE_HAS_PHY_SEL_EFUSE 0
#define ESP_USB_DEVICE_HAS_ROM_DFU 1
#else
#define ESP_USB_DEVICE_HAS_ROM_DFU 0
#define ESP_USB_DEVICE_HAS_PHY_SEL_EFUSE 0
#endif
#else
#define ESP_USB_DEVICE_HAS_DOWNLOAD_BOOT 0
#define ESP_USB_DEVICE_HAS_ROM_DFU 0
#define ESP_USB_DEVICE_HAS_SHARED_PHY 0
#define ESP_USB_DEVICE_HAS_PHY_SEL_EFUSE 0
#endif

// Full speed caps an interrupt endpoint at 64 bytes (USB 2.0 table 9-13).
static constexpr uint16_t ESP_USB_DEVICE_FS_INTERRUPT_MAX_PACKET = 64;
// What EspUsbController::Auto resolves to. Only the P4 has a high-speed
// controller to resolve to.
#if defined(CONFIG_IDF_TARGET_ESP32P4)
static constexpr bool ESP_USB_DEVICE_HIGH_SPEED_DEFAULT = true;
#else
static constexpr bool ESP_USB_DEVICE_HIGH_SPEED_DEFAULT = false;
#endif

static constexpr uint8_t USB_DESC_DEVICE = 0x01;
static constexpr uint8_t USB_DESC_CONFIGURATION = 0x02;
static constexpr uint8_t USB_DESC_STRING = 0x03;
static constexpr uint8_t USB_DESC_INTERFACE = 0x04;
static constexpr uint8_t USB_DESC_ENDPOINT = 0x05;
static constexpr uint8_t USB_DESC_INTERFACE_ASSOCIATION = 0x0b;
static constexpr uint8_t USB_DESC_BOS = 0x0f;
static constexpr uint8_t USB_DESC_DEVICE_CAPABILITY = 0x10;
static constexpr uint8_t USB_DESC_HID = 0x21;

static constexpr uint8_t USB_CLASS_HID = 0x03;
static constexpr uint8_t USB_CLASS_VENDOR_SPECIFIC = 0xff;
static constexpr uint8_t USB_SUBCLASS_BOOT = 0x01;
static constexpr uint8_t USB_PROTOCOL_KEYBOARD = 0x01;
static constexpr uint8_t USB_PROTOCOL_MOUSE = 0x02;

static constexpr uint8_t USB_ENDPOINT_ATTR_ISOCHRONOUS = 0x01;
static constexpr uint8_t USB_ENDPOINT_ATTR_BULK = 0x02;
static constexpr uint8_t USB_ENDPOINT_ATTR_INTERRUPT = 0x03;
static constexpr uint8_t USB_SCSI_CMD_SYNCHRONIZE_CACHE_10 = 0x35;

static EspUsbDevice *g_activeDevice = nullptr;
// The active Audio function registry lives in EspUsbAudio.cpp.

static void put16(uint8_t *dst, uint16_t value)
{
  dst[0] = static_cast<uint8_t>(value & 0xff);
  dst[1] = static_cast<uint8_t>((value >> 8) & 0xff);
}

static void put32(uint8_t *dst, uint32_t value)
{
  dst[0] = static_cast<uint8_t>(value & 0xff);
  dst[1] = static_cast<uint8_t>((value >> 8) & 0xff);
  dst[2] = static_cast<uint8_t>((value >> 16) & 0xff);
  dst[3] = static_cast<uint8_t>((value >> 24) & 0xff);
}

static size_t putUtf16Le(uint8_t *dst, const char *text, bool doubleTerminated)
{
  size_t offset = 0;
  while (*text)
  {
    dst[offset++] = static_cast<uint8_t>(*text++);
    dst[offset++] = 0;
  }
  dst[offset++] = 0;
  dst[offset++] = 0;
  if (doubleTerminated)
  {
    dst[offset++] = 0;
    dst[offset++] = 0;
  }
  return offset;
}

static uint8_t powerToDescriptor(uint16_t milliamps)
{
  uint16_t units = (milliamps + 1) / 2;
  if (units > 250)
  {
    units = 250;
  }
  return static_cast<uint8_t>(units);
}

// Length of the HID item starting at `item`, or 0 if it is malformed or runs
// past `available`.
//
// A HID report descriptor is a stream of items, not a fixed layout, and the
// prefix byte says how much data follows: two bits of size, where 3 means four
// bytes rather than three. 0xFE introduces a long item, which nothing here
// emits but which must still be stepped over correctly rather than silently
// mis-parsed.
static size_t hidItemLength(const uint8_t *item, size_t available)
{
  if (!item || available == 0)
  {
    return 0;
  }
  if (item[0] == 0xfe)
  {
    if (available < 3)
    {
      return 0;
    }
    const size_t length = 3u + item[1];
    return length <= available ? length : 0;
  }
  static constexpr uint8_t DATA_SIZE[4] = {0, 1, 2, 4};
  const size_t length = 1u + DATA_SIZE[item[0] & 0x03];
  return length <= available ? length : 0;
}

// Item prefixes with the size bits masked off.
static constexpr uint8_t HID_ITEM_COLLECTION = 0xa0;
static constexpr uint8_t HID_ITEM_REPORT_ID = 0x84;

static uint16_t writeHidConfigurationDescriptor(uint8_t *dst,
                                                uint8_t interfaceNumber,
                                                uint8_t endpointNumber,
                                                uint16_t endpointSize,
                                                uint8_t subclass,
                                                uint8_t protocol,
                                                uint16_t reportDescriptorLength,
                                                bool hasOutEndpoint)
{
  using espusb::internal::DescriptorBuildContext;
  using espusb::internal::HidFunctionConfig;
  using espusb::internal::HidFunctionLayout;
  using espusb::internal::UsbSpeed;
  using espusb::internal::writeHidFunction;

  if (endpointNumber == 0 || endpointNumber > 15 ||
      endpointSize == 0 || reportDescriptorLength == 0)
  {
    return 0;
  }

  const size_t capacity = hasOutEndpoint ? 32U : 25U;
  if (!dst) return uint16_t(capacity);
  espusb::internal::DescriptorBuffer buffer(dst, capacity);
  DescriptorBuildContext context(UsbSpeed::Full, buffer);
  HidFunctionLayout layout;
  layout.interfaceNumber = interfaceNumber;
  layout.endpoint.out = endpointNumber;
  layout.endpoint.in = static_cast<uint8_t>(0x80U | endpointNumber);
  HidFunctionConfig config;
  config.subclass = subclass;
  config.protocol = protocol;
  config.reportDescriptorLength = reportDescriptorLength;
  config.fullSpeedPacketSize = endpointSize;
  config.highSpeedPacketSize = endpointSize;
  config.hasOutEndpoint = hasOutEndpoint;
  return writeHidFunction(context, layout, config)
             ? static_cast<uint16_t>(buffer.size())
             : 0;
}

static constexpr uint8_t KEYBOARD_REPORT_DESCRIPTOR[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x06,       // Usage (Keyboard)
    0xa1, 0x01,       // Collection (Application)
    0x05, 0x07,       //   Usage Page (Keyboard/Keypad)
    0x19, 0xe0,       //   Usage Minimum (Keyboard LeftControl)
    0x29, 0xe7,       //   Usage Maximum (Keyboard Right GUI)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x08,       //   Report Count (8)
    0x81, 0x02,       //   Input (Data,Var,Abs)
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x08,       //   Report Size (8)
    0x81, 0x01,       //   Input (Const,Array,Abs)
    0x05, 0x08,       //   Usage Page (LEDs)
    0x19, 0x01,       //   Usage Minimum (Num Lock)
    0x29, 0x05,       //   Usage Maximum (Kana)
    0x95, 0x05,       //   Report Count (5)
    0x75, 0x01,       //   Report Size (1)
    0x91, 0x02,       //   Output (Data,Var,Abs)
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x03,       //   Report Size (3)
    0x91, 0x01,       //   Output (Const,Array,Abs)
    0x05, 0x07,       //   Usage Page (Keyboard/Keypad)
    0x19, 0x00,       //   Usage Minimum (Reserved)
    0x29, 0xff,       //   Usage Maximum
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0xff,       //   Logical Maximum (255)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x06,       //   Report Count (6)
    0x81, 0x00,       //   Input (Data,Array,Abs)
    0xc0,             // End Collection
};

// N-key rollover keyboard. Input report = 1 modifier byte (usages 0xE0-0xE7) +
// a 224-bit bitmap for usages 0x00-0xDF (one bit per key), so any number of keys
// can be held at once, including International1-9 (0x87-0x8F) and LANG1-9
// (0x90-0x98) used by JIS / other non-US layouts. The LED output report is kept
// identical to the boot descriptor. The leading 6 bytes (Usage Page / Usage /
// Collection) match KEYBOARD_REPORT_DESCRIPTOR so the composite report-ID
// injection in buildDescriptors() works unchanged.
static constexpr uint8_t NKRO_KEYBOARD_REPORT_DESCRIPTOR[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x06,       // Usage (Keyboard)
    0xa1, 0x01,       // Collection (Application)
    0x05, 0x07,       //   Usage Page (Keyboard/Keypad)
    0x19, 0xe0,       //   Usage Minimum (Keyboard LeftControl)
    0x29, 0xe7,       //   Usage Maximum (Keyboard Right GUI)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x08,       //   Report Count (8)
    0x81, 0x02,       //   Input (Data,Var,Abs) -- modifier byte
    0x05, 0x08,       //   Usage Page (LEDs)
    0x19, 0x01,       //   Usage Minimum (Num Lock)
    0x29, 0x05,       //   Usage Maximum (Kana)
    0x95, 0x05,       //   Report Count (5)
    0x75, 0x01,       //   Report Size (1)
    0x91, 0x02,       //   Output (Data,Var,Abs)
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x03,       //   Report Size (3)
    0x91, 0x01,       //   Output (Const,Array,Abs) -- LED padding
    0x05, 0x07,       //   Usage Page (Keyboard/Keypad)
    0x19, 0x00,       //   Usage Minimum (0)
    0x29, 0xdf,       //   Usage Maximum (0xDF)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0xe0,       //   Report Count (224)
    0x81, 0x02,       //   Input (Data,Var,Abs) -- NKRO key bitmap
    0xc0,             // End Collection
};

static constexpr uint8_t MOUSE_REPORT_DESCRIPTOR[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x02,       // Usage (Mouse)
    0xa1, 0x01,       // Collection (Application)
    0x09, 0x01,       //   Usage (Pointer)
    0xa1, 0x00,       //   Collection (Physical)
    0x05, 0x09,       //     Usage Page (Button)
    0x19, 0x01,       //     Usage Minimum (Button 1)
    0x29, 0x05,       //     Usage Maximum (Button 5)
    0x15, 0x00,       //     Logical Minimum (0)
    0x25, 0x01,       //     Logical Maximum (1)
    0x95, 0x05,       //     Report Count (5)
    0x75, 0x01,       //     Report Size (1)
    0x81, 0x02,       //     Input (Data,Var,Abs)
    0x95, 0x01,       //     Report Count (1)
    0x75, 0x03,       //     Report Size (3)
    0x81, 0x01,       //     Input (Const,Array,Abs)
    0x05, 0x01,       //     Usage Page (Generic Desktop)
    0x09, 0x30,       //     Usage (X)
    0x09, 0x31,       //     Usage (Y)
    0x09, 0x38,       //     Usage (Wheel)
    0x15, 0x81,       //     Logical Minimum (-127)
    0x25, 0x7f,       //     Logical Maximum (127)
    0x75, 0x08,       //     Report Size (8)
    0x95, 0x03,       //     Report Count (3)
    0x81, 0x06,       //     Input (Data,Var,Rel)
    0xc0,             //   End Collection
    0xc0,             // End Collection
};

static constexpr uint8_t GAMEPAD_REPORT_DESCRIPTOR[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x05,       // Usage (Game Pad)
    0xa1, 0x01,       // Collection (Application)
    0x85, 0x03,       //   Report ID (3)
    0x15, 0x81,       //   Logical Minimum (-127)
    0x25, 0x7f,       //   Logical Maximum (127)
    0x09, 0x30,       //   Usage (X)
    0x09, 0x31,       //   Usage (Y)
    0x09, 0x32,       //   Usage (Z)
    0x09, 0x35,       //   Usage (Rz)
    0x09, 0x33,       //   Usage (Rx)
    0x09, 0x34,       //   Usage (Ry)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x06,       //   Report Count (6)
    0x81, 0x02,       //   Input (Data,Var,Abs)
    0x15, 0x01,       //   Logical Minimum (1): directions 1..8
    0x25, 0x08,       //   Logical Maximum (8)
    0x35, 0x00,       //   Physical Minimum (0)
    0x46, 0x3b, 0x01, //   Physical Maximum (315)
    0x65, 0x14,       //   Unit (Eng Rot:Angular Pos)
    0x09, 0x39,       //   Usage (Hat switch)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x42,       //   Input (Data,Var,Abs,Null): center is 0
    0x65, 0x00,       //   Unit (None)
    0x45, 0x01,       //   Physical Maximum (1) for the buttons
    0x05, 0x09,       //   Usage Page (Button)
    0x19, 0x01,       //   Usage Minimum (Button 1)
    0x29, 0x20,       //   Usage Maximum (Button 32)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x20,       //   Report Count (32)
    0x81, 0x02,       //   Input (Data,Var,Abs)
    0xc0,             // End Collection
};

static constexpr uint8_t CONSUMER_CONTROL_REPORT_DESCRIPTOR[] = {
    0x05, 0x0c,       // Usage Page (Consumer)
    0x09, 0x01,       // Usage (Consumer Control)
    0xa1, 0x01,       // Collection (Application)
    0x85, 0x04,       //   Report ID (4)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xff, 0x03, //   Logical Maximum (1023)
    0x19, 0x00,       //   Usage Minimum (Unassigned)
    0x2a, 0xff, 0x03, //   Usage Maximum (1023)
    0x75, 0x10,       //   Report Size (16)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x00,       //   Input (Data,Array,Abs)
    0xc0,             // End Collection
};

static constexpr uint8_t SYSTEM_CONTROL_REPORT_DESCRIPTOR[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x80,       // Usage (System Control)
    0xa1, 0x01,       // Collection (Application)
    0x85, 0x05,       //   Report ID (5)
    0x15, 0x01,       //   Logical Minimum (1)
    0x25, 0x03,       //   Logical Maximum (3)
    0x19, 0x81,       //   Usage Minimum (System Power Down)
    0x29, 0x83,       //   Usage Maximum (System Wake Up)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x00,       //   Input (Data,Array,Abs): zero means no asserted item
    0xc0,             // End Collection
};


static constexpr uint8_t WEBUSB_VENDOR_CODE = 0x01;
static constexpr uint8_t MICROSOFT_OS_20_VENDOR_CODE = 0x02;
static constexpr uint16_t MICROSOFT_OS_20_DESCRIPTOR_INDEX = 0x0007;

#if ESP_USB_DEVICE_HAS_OTG
static constexpr uint8_t USB_REQ_GET_DESCRIPTOR = 0x06;

// Note for the descriptor callbacks below: each one records the length it just
// returned so the ACK observation can report it. The stack asks for a pointer
// and reads the length out of the descriptor itself, so this is the only place
// that number exists. Nothing is recorded when no sketch is watching.
static void recordDescriptorResponse(uint16_t wValue, uint16_t wIndex, uint16_t length)
{
  if (g_activeDevice && g_activeDevice->hasControlObserver())
  {
    g_activeDevice->recordControlResponse(USB_REQ_GET_DESCRIPTOR, wValue, wIndex,
                                          length);
  }
}

extern "C" uint8_t const *espusb_device_descriptor_device(void)
{
  const uint8_t *descriptor = g_activeDevice ? g_activeDevice->deviceDescriptor() : nullptr;
  recordDescriptorResponse(0x0100, 0, descriptor ? descriptor[0] : 0);
  return descriptor;
}

extern "C" uint8_t const *espusb_device_descriptor_configuration(uint8_t index)
{
  const uint8_t *descriptor =
      g_activeDevice ? g_activeDevice->configurationDescriptorForSpeed(
                           index, espusb_device_speed() == ESPUSB_SPEED_HIGH)
                     : nullptr;
  recordDescriptorResponse(static_cast<uint16_t>(0x0200 | index), 0,
                           descriptor ? static_cast<uint16_t>(descriptor[2] | (descriptor[3] << 8)) : 0);
  return descriptor;
}

extern "C" uint16_t const *espusb_device_descriptor_string(uint8_t index, uint16_t langid)
{
  const uint16_t *descriptor =
      g_activeDevice ? g_activeDevice->stringDescriptor(index, langid) : nullptr;
  recordDescriptorResponse(static_cast<uint16_t>(0x0300 | index), langid,
                           descriptor ? static_cast<uint16_t>(descriptor[0] & 0xff) : 0);
  return descriptor;
}

extern "C" uint8_t const *espusb_device_descriptor_bos(void)
{
  const uint8_t *descriptor = g_activeDevice ? g_activeDevice->bosDescriptor() : nullptr;
  recordDescriptorResponse(0x0f00, 0,
                           g_activeDevice ? g_activeDevice->bosDescriptorLength() : 0);
  return descriptor;
}

extern "C" uint8_t const *espusb_device_descriptor_device_qualifier(void)
{
  const uint8_t *descriptor = g_activeDevice ? g_activeDevice->deviceQualifierDescriptor() : nullptr;
  recordDescriptorResponse(0x0600, 0, descriptor ? descriptor[0] : 0);
  return descriptor;
}

extern "C" uint8_t const *espusb_device_descriptor_other_speed_configuration(uint8_t index)
{
  const uint8_t *descriptor =
      g_activeDevice ? g_activeDevice->otherSpeedConfigurationDescriptor(
                           index, espusb_device_speed() == ESPUSB_SPEED_HIGH)
                     : nullptr;
  recordDescriptorResponse(static_cast<uint16_t>(0x0700 | index), 0,
                           descriptor ? static_cast<uint16_t>(descriptor[2] | (descriptor[3] << 8)) : 0);
  return descriptor;
}

// The independent EP0 engine reports acknowledged standard/class/vendor
// requests here. Stalled requests have no status stage; the vendor SETUP path
// reports them separately when a control observer is registered.
extern "C" void espusb_device_control_acknowledged(uint8_t rhport, const EspUsbControlRequest *request)
{
  if (!g_activeDevice || !request || !g_activeDevice->hasControlObserver())
  {
    return;
  }
  EspUsbDeviceControlRequestInfo info;
  info.rhport = rhport;
  info.stage = ESP_USB_DEVICE_CONTROL_STAGE_ACK;
  info.bmRequestType = request->bmRequestType;
  info.bRequest = request->bRequest;
  info.wValue = request->wValue;
  info.wIndex = request->wIndex;
  info.wLength = request->wLength;
  info.responseLength = g_activeDevice->takeControlResponse(
      request->bRequest, request->wValue, request->wIndex);
  info.handled = true;
  g_activeDevice->notifyControlRequest(info);
}



void espusb_device_on_mount(void)
{
  if (g_activeDevice)
  {
    g_activeDevice->handleBusAttached();
  }
}

void espusb_device_on_unmount(void)
{
  if (g_activeDevice)
  {
    g_activeDevice->handleBusDetached();
  }
}

// The vendor-request handling itself. `observation`, when not null, collects
// what this decided so the caller can report it.
static bool handleVendorControlXfer(uint8_t rhport, uint8_t stage,
                                    EspUsbControlRequest const *request,
                                    EspUsbDeviceControlRequestInfo *observation)
{
  if (g_activeDevice && request &&
      request->bRequest == WEBUSB_VENDOR_CODE &&
      g_activeDevice->config().webusbEnabled)
  {
    if (stage != ESPUSB_CONTROL_STAGE_SETUP)
    {
      return true;
    }
    const uint8_t *url = g_activeDevice->webUsbUrlDescriptor();
    if (request->bmRequestType != 0xc0 || request->wIndex != 2 ||
        request->wValue != 1 || !url) return false;
    if (observation)
    {
      observation->responseLength = url[0];
      observation->handled = true;
    }
    return espusb_device_control_transfer(rhport, request, const_cast<uint8_t *>(url), url[0]);
  }
  if (g_activeDevice && request &&
      request->bRequest == MICROSOFT_OS_20_VENDOR_CODE &&
      request->wIndex == MICROSOFT_OS_20_DESCRIPTOR_INDEX &&
      g_activeDevice->microsoftOs20Descriptor())
  {
    if (stage != ESPUSB_CONTROL_STAGE_SETUP)
    {
      return true;
    }
    if (observation)
    {
      observation->responseLength = g_activeDevice->microsoftOs20DescriptorLength();
      observation->handled = true;
    }
    return espusb_device_control_transfer(
        rhport, request,
        const_cast<uint8_t *>(g_activeDevice->microsoftOs20Descriptor()),
        g_activeDevice->microsoftOs20DescriptorLength());
  }
  const bool handled =
      g_activeDevice ? g_activeDevice->handleVendorControlRequest(rhport, stage, request) : false;
  if (observation)
  {
    observation->handled = handled;
  }
  return handled;
}

extern "C" bool espusb_device_vendor_control(uint8_t rhport, uint8_t stage, EspUsbControlRequest const *request)
{
  // Every vendor request is reported at SETUP, before the library decides what
  // to do with it, so a sketch can tell "the host never asked" from "the host
  // asked and the answer was wrong" - which is the whole question when Windows
  // refuses to bind WinUSB to a vendor interface. SETUP rather than ACK because
  // a request the device stalls never reaches a status stage.
  const bool observing = g_activeDevice && request &&
                         stage == ESPUSB_CONTROL_STAGE_SETUP &&
                         g_activeDevice->hasControlObserver();
  EspUsbDeviceControlRequestInfo observation;
  if (observing)
  {
    observation.rhport = rhport;
    observation.stage = ESP_USB_DEVICE_CONTROL_STAGE_SETUP;
    observation.bmRequestType = request->bmRequestType;
    observation.bRequest = request->bRequest;
    observation.wValue = request->wValue;
    observation.wIndex = request->wIndex;
    observation.wLength = request->wLength;
  }
  const bool result = handleVendorControlXfer(rhport, stage, request,
                                              observing ? &observation : nullptr);
  if (observing)
  {
    g_activeDevice->notifyControlRequest(observation);
  }
  return result;
}


#endif


EspUsbDevice::EspUsbDevice()
{
}

EspUsbDevice::~EspUsbDevice() { end(); }

void EspUsbDevice::releaseDescriptors()
{
  for (auto *pointer : {configDescriptor_, configDescriptorHighSpeed_, otherSpeedDescriptor_,
       bosDescriptor_, microsoftOs20Descriptor_, hidReportDescriptor_}) espusb::internal::freeUsbMemory(pointer);
  espusb::internal::freeUsbMemory(stringDescriptors_);
  espusb::internal::freeUsbMemory(webUsbUrlDescriptor_);
  stringDescriptors_ = nullptr; stringDescriptorSlots_ = 0;
  webUsbUrlDescriptor_ = nullptr;
  configDescriptor_ = configDescriptorHighSpeed_ = otherSpeedDescriptor_ = nullptr;
  bosDescriptor_ = microsoftOs20Descriptor_ = hidReportDescriptor_ = nullptr;
  configDescriptorLength_ = highSpeedDescriptorLength_ = 0;
  bosDescriptorLength_ = microsoftOs20DescriptorLength_ = hidReportDescriptorLength_ = 0;
  microsoftOs20VendorRevision_ = 0; hidInterfacesLength_ = 0; hidInterfaceCount_ = 0;
  for (auto *function = firstClass_; function; function = function->nextRegistered_) {
    function->assignedReportId_ = 0; function->descriptorInterfaceNumber_ = 0xff;
  }
}

bool EspUsbDevice::needsHighSpeedDescriptors() const
{
#if defined(CONFIG_IDF_TARGET_ESP32P4) || defined(CONFIG_IDF_TARGET_ESP32S31)
  return config_.controller != EspUsbController::FullSpeed;
#else
  return false;
#endif
}

namespace
{
// Defined with the rest of the Microsoft OS 2.0 helpers, below; begin() needs it
// before that.
bool isDeviceInterfaceGuid(const char *guid);
#if ESP_USB_DEVICE_HAS_OTG
const EspUsbDwc2Port *descriptorController(bool highSpeed) {
  const auto *port = espusb_dwc2_port_for_speed(highSpeed);
  // Detached HS inspection on FS-only hardware retains its physical budget.
  // Runtime selection still refuses an unavailable physical controller.
  return port ? port : espusb_dwc2_port_for_speed(false);
}
#endif
} // namespace

struct EspUsbDevice::RegistrationTransaction {
  EspUsbDevice &device;
  bool keep = false;
  ~RegistrationTransaction() { if (!keep) { device.releaseDescriptors(); device.releaseClassRegistry(); } }
};

bool EspUsbDevice::begin()
{
  return begin(EspUsbDeviceConfig());
}

bool EspUsbDevice::begin(const EspUsbDeviceConfig &config)
{
  if (running_)
  {
    setLastError(ESP_ERR_INVALID_STATE);
    return false;
  }
  // Driver registration must not rebuild a table retained by a running stack.
  if (g_activeDevice && g_activeDevice != this)
  {
    setLastError(ESP_ERR_INVALID_STATE);
    return false;
  }
  if (rejectedClassCount_) { setLastError(ESP_ERR_INVALID_STATE); return false; }
  // String indices and logical Function/report identities are USB uint8_t values.
  // This is a protocol bound, independent of controller endpoint limits.
  if (classCount_ > size_t(UINT8_MAX) - FIRST_FUNCTION_STRING_INDEX + 1) {
    setLastError(ESP_ERR_INVALID_SIZE); return false;
  }
  if (config.startMode != EspUsbDeviceStartMode::Usb && config.startMode != EspUsbDeviceStartMode::DescriptorsOnly) {
    setLastError(ESP_ERR_INVALID_ARG); return false;
  }
  config_ = config;
  // Checked before the descriptors are built: the registry property writes a
  // constant length, so a GUID of the wrong shape would produce a set Windows
  // rejects with nothing here to say why.
  if (config_.deviceInterfaceGuid && !isDeviceInterfaceGuid(config_.deviceInterfaceGuid))
  {
    lastError_ = ESP_ERR_INVALID_ARG;
    return false;
  }
  RegistrationTransaction registryGuard{*this};
  if (!allocateClassRegistry()) return false;
  if (!buildDescriptors())
  {
    return false;
  }

  // Decided here rather than beside the runtime start so a sketch that only
  // builds descriptors (config.startMode = EspUsbDeviceStartMode::DescriptorsOnly) can still read it back,
  // which is how the arithmetic is checked without a host.
  const bool highSpeedBudget =
      config_.controller == EspUsbController::HighSpeed ||
      (config_.controller == EspUsbController::Auto && ESP_USB_DEVICE_HIGH_SPEED_DEFAULT);
  bool doubleBufferFits = true;
  bulkInDoubleBuffered_ = bulkInDoubleBufferMask(highSpeedBudget, &doubleBufferFits);
  if (config_.bulkInBuffering == EspUsbBulkInBuffering::Double && !doubleBufferFits)
  {
    // Asked for two packets per bulk IN endpoint and the controller's FIFO
    // cannot hold that. Saying so here is the whole point: the alternative is
    // an endpoint that silently fails to open at enumeration.
    setLastError(ESP_ERR_INVALID_SIZE);
    return false;
  }
  // Isochronous IN endpoints are allocated up front and their allocation is
  // not refused when it does not fit, so a camera or microphone that outgrows
  // the FIFO enumerates, binds a host driver and transmits nothing. Checked
  // only when the configuration has one, because every other transfer type
  // already fails visibly at enumeration and this arithmetic should not be
  // able to refuse a configuration that has been working.
  if (hasIsochronousInEndpoint(highSpeedBudget) && !transmitFifoFits(highSpeedBudget))
  {
    setLastError(ESP_ERR_NO_MEM);
    return false;
  }

  size_t begunClassCount = 0;
  for (size_t i = 0; i < classCount_; i++)
  {
    if (classAt(i) && !classAt(i)->begin())
    {
      const esp_err_t error = classAt(i)->startError();
      classAt(i)->end();
      while (begunClassCount > 0)
      {
        --begunClassCount;
        classAt(begunClassCount)->end();
      }
      setLastError(error);
      return false;
    }
    ++begunClassCount;
  }
  if (config_.startMode == EspUsbDeviceStartMode::Usb && classCount_ > 0)
  {
#if ESP_USB_DEVICE_HAS_OTG
    if (g_activeDevice && g_activeDevice != this)
    {
      while (begunClassCount > 0)
      {
        --begunClassCount;
        classAt(begunClassCount)->end();
      }
      setLastError(ESP_ERR_INVALID_STATE);
      return false;
    }
    g_activeDevice = this;

    espusb::internal::UsbRuntimeOptions runtimeOptions;
    switch (config_.controller)
    {
    case EspUsbController::FullSpeed:
      runtimeOptions.controller = espusb::internal::UsbController::FullSpeed;
      break;
    case EspUsbController::HighSpeed:
      runtimeOptions.controller = espusb::internal::UsbController::HighSpeed;
      break;
    case EspUsbController::Auto:
    default:
      runtimeOptions.controller = espusb::internal::UsbController::Auto;
      break;
    }

    runtimeOptions.bulkInDoubleBuffered = bulkInDoubleBuffered_;
    runtimeOptions.vbusSensing = config_.vbusSensing;
    runtimeOptions.taskStackBytes = config_.taskStackBytes;
    runtimeOptions.taskPriority = config_.taskPriority;
    runtimeOptions.taskCoreId = config_.taskCoreId;
    runtimeOptions.eventQueueDepth = config_.eventQueueDepth;

    const esp_err_t err = espusb::internal::startUsbRuntime(runtimeOptions);
    if (err != ESP_OK)
    {
      g_activeDevice = nullptr;
      while (begunClassCount > 0)
      {
        --begunClassCount;
        classAt(begunClassCount)->end();
      }
      setLastError(err);
      return false;
    }
    usbStarted_ = true;
    for (size_t i = 0; i < classCount_; i++)
    {
      if (classAt(i) && !classAt(i)->afterDeviceStarted())
      {
        espusb::internal::stopUsbRuntime();
        usbStarted_ = false;
        g_activeDevice = nullptr;
        while (begunClassCount > 0)
        {
          --begunClassCount;
          classAt(begunClassCount)->end();
        }
        setLastError(ESP_FAIL);
        return false;
      }
    }
#else
    while (begunClassCount > 0)
    {
      --begunClassCount;
      classAt(begunClassCount)->end();
    }
    setLastError(ESP_ERR_NOT_SUPPORTED);
    return false;
#endif
  }

  registryGuard.keep = true;
  running_ = true;
  ready_ = usbStarted_;
  setLastError(ESP_OK);
  return true;
}

esp_err_t EspUsbDevice::end()
{
#if ESP_USB_DEVICE_HAS_OTG
  if (espusb::internal::usbRuntimeInTask())
  {
    setLastError(ESP_ERR_INVALID_STATE);
    return ESP_ERR_INVALID_STATE;
  }
#endif
  for (size_t i = 0; i < classCount_; ++i)
  {
    if (classAt(i)) classAt(i)->prepareToStop();
  }
#if ESP_USB_DEVICE_HAS_OTG
  if (usbStarted_)
  {
    if (!espusb::internal::stopUsbRuntime())
    {
      setLastError(ESP_FAIL);
      return ESP_FAIL;
    }
    usbStarted_ = false;
  }
#endif
  if (running_)
  {
    for (size_t i = classCount_; i > 0; --i)
    {
      if (classAt(i - 1))
      {
        classAt(i - 1)->end();
      }
    }
  }
  if (g_activeDevice == this)
  {
    g_activeDevice = nullptr;
  }
  running_ = false;
  ready_ = false;
  releaseDescriptors();
  releaseClassRegistry();
  setLastError(ESP_OK);
  return ESP_OK;
}

bool EspUsbDevice::handleVendorControlRequest(uint8_t rhport, uint8_t stage, const void *request)
{
  for (size_t i = 0; i < classCount_; ++i)
  {
    if (classAt(i) && classAt(i)->onVendorControlRequest(rhport, stage, request)) return true;
  }
  return false;
}

bool EspUsbDevice::started() const { return running_.load(); }

bool EspUsbDevice::ready() const
{
#if ESP_USB_DEVICE_HAS_OTG
  if (usbStarted_)
  {
    return espusb_device_mounted();
  }
#endif
  return ready_;
}

bool EspUsbDevice::suspended() const
{
#if ESP_USB_DEVICE_HAS_OTG
  return usbStarted_ && espusb_device_suspended();
#else
  return false;
#endif
}

const EspUsbDeviceConfig &EspUsbDevice::config() const
{
  return config_;
}

uint16_t EspUsbDevice::bulkInDoubleBuffered() const
{
  return bulkInDoubleBuffered_;
}

uint16_t EspUsbDevice::hidEndpointSize() const
{
  return 8;
}

esp_err_t EspUsbDevice::lastError() const
{
  return lastError_;
}

const char *EspUsbDevice::lastErrorName() const
{
  switch (lastError_)
  {
  case ESP_OK:
    return "ESP_OK";
  case ESP_FAIL:
    return "ESP_FAIL";
  case ESP_ERR_INVALID_STATE:
    return "ESP_ERR_INVALID_STATE";
  case ESP_ERR_INVALID_ARG:
    return "ESP_ERR_INVALID_ARG";
  case ESP_ERR_NO_MEM:
    return "ESP_ERR_NO_MEM";
  case ESP_ERR_INVALID_SIZE:
    return "ESP_ERR_INVALID_SIZE";
  case ESP_ERR_NOT_SUPPORTED:
    return "ESP_ERR_NOT_SUPPORTED";
  default:
    return "ESP_ERR_UNKNOWN";
  }
}

EspUsbDeviceClass *EspUsbDevice::classAt(size_t index) const
{
  if (index >= classCount_) return nullptr;
  if (classes_) return classes_[index];
  auto *function = firstClass_;
  while (function && index--) function = function->nextRegistered_;
  return function;
}

void EspUsbDevice::releaseClassRegistry()
{
  espusb::internal::freeUsbMemory(classes_);
  classes_ = nullptr;
}

bool EspUsbDevice::allocateClassRegistry()
{
  releaseClassRegistry();
  if (!classCount_) return true;
  const size_t bytes = classCount_ * sizeof(*classes_);
  classes_ = static_cast<EspUsbDeviceClass **>(espusb::internal::allocateUsbMemory(bytes));
  if (!classes_) { setLastError(ESP_ERR_NO_MEM); return false; }
  auto *function = firstClass_;
  for (size_t i = 0; i < classCount_; ++i, function = function->nextRegistered_) classes_[i] = function;
  return true;
}

bool EspUsbDevice::addClass(EspUsbDeviceClass *function)
{
  if (!function || &function->device_ != this) { setLastError(ESP_ERR_INVALID_ARG); return false; }
  if (function->registered_) return true;
  if (running_) {
    if (!function->registrationRejected_) { function->registrationRejected_ = true; ++rejectedClassCount_; }
    setLastError(ESP_ERR_INVALID_STATE); return false;
  }
  if (function->registrationRejected_) { function->registrationRejected_ = false; --rejectedClassCount_; }
  releaseClassRegistry();
  for (auto *item = firstClass_; item; item = item->nextRegistered_) item->assignedReportId_ = 0;
  function->nextRegistered_ = nullptr;
  function->registered_ = true;
  function->hidInstance_ = static_cast<uint8_t>(classCount_++);
  if (lastClass_) lastClass_->nextRegistered_ = function;
  else firstClass_ = function;
  lastClass_ = function;
  configDescriptorLength_ = 0;
  setLastError(ESP_OK); return true;
}

void EspUsbDevice::removeClass(EspUsbDeviceClass *function)
{
  if (!function) return;
  if (function->registrationRejected_) { function->registrationRejected_ = false; --rejectedClassCount_; }
  if (!function->registered_) return;
  if (running_ && end() != ESP_OK) return;
  releaseClassRegistry();
  auto **link = &firstClass_;
  EspUsbDeviceClass *previous = nullptr;
  while (*link && *link != function) { previous = *link; link = &(*link)->nextRegistered_; }
  if (!*link) return;
  *link = function->nextRegistered_;
  if (lastClass_ == function) lastClass_ = previous;
  function->nextRegistered_ = nullptr; function->registered_ = false; function->hidInstance_ = UINT8_MAX; --classCount_;
  size_t index = 0;
  for (auto *item = firstClass_; item; item = item->nextRegistered_) {
    item->hidInstance_ = static_cast<uint8_t>(index++); item->assignedReportId_ = 0;
  }
  configDescriptorLength_ = 0;
}

bool EspUsbDevice::sendHidReport(uint8_t instance, uint8_t reportId, const void *data, size_t length, uint32_t timeoutMs)
{
  if (!usbStarted_) { setLastError(ESP_ERR_INVALID_STATE); return false; }
  if (instance != 0 || !data || !length) { setLastError(ESP_ERR_INVALID_ARG); return false; }
  for (size_t i = 0; i < classCount_; ++i) {
    if (classAt(i)->isHid()) return classAt(i)->sendHidInput(reportId, data, length, timeoutMs);
  }
  setLastError(ESP_ERR_INVALID_STATE); return false;
}

const uint8_t *EspUsbDevice::deviceDescriptor()
{
  return buildDescriptors() ? deviceDescriptor_ : nullptr;
}

const uint8_t *EspUsbDevice::configurationDescriptor(uint8_t index)
{
  return configurationDescriptorForSpeed(index, false);
}

const uint8_t *EspUsbDevice::configurationDescriptorForSpeed(uint8_t index, bool highSpeed)
{
  if (index != 0)
  {
    return nullptr;
  }
  if (!buildDescriptors())
  {
    return nullptr;
  }
  if (highSpeed && !configDescriptorHighSpeed_) {
    // On FS-only hardware this is an explicit detached inspection, never a
    // USB worker allocation. Dual-speed controllers prepare both at begin.
    if (usbStarted_) { setLastError(ESP_ERR_NOT_SUPPORTED); return nullptr; }
    if (!buildConfigurationDescriptor(true)) return nullptr;
  }
  return highSpeed ? configDescriptorHighSpeed_ : configDescriptor_;
}

const uint8_t *EspUsbDevice::deviceQualifierDescriptor()
{
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (config_.controller == EspUsbController::FullSpeed)
  {
    return nullptr;
  }
  return buildDescriptors() ? deviceQualifierDescriptor_ : nullptr;
#else
  return nullptr;
#endif
}

const uint8_t *EspUsbDevice::otherSpeedConfigurationDescriptor(uint8_t index, bool currentHighSpeed)
{
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (index != 0 || config_.controller == EspUsbController::FullSpeed ||
      !buildDescriptors())
  {
    return nullptr;
  }
  const uint8_t *source =
      currentHighSpeed ? configDescriptor_ : configDescriptorHighSpeed_;
  const uint16_t length = uint16_t(source[2] | (uint16_t(source[3]) << 8));
  memcpy(otherSpeedDescriptor_, source, length);
  otherSpeedDescriptor_[1] = 0x07;
  return otherSpeedDescriptor_;
#else
  (void)index;
  (void)currentHighSpeed;
  return nullptr;
#endif
}

const uint8_t *EspUsbDevice::bosDescriptor() const
{
  return bosDescriptorLength_ ? bosDescriptor_ : nullptr;
}

uint16_t EspUsbDevice::bosDescriptorLength() const
{
  return bosDescriptorLength_;
}

const uint8_t *EspUsbDevice::microsoftOs20Descriptor() const
{
  return microsoftOs20DescriptorLength_ ? microsoftOs20Descriptor_ : nullptr;
}

uint16_t EspUsbDevice::microsoftOs20DescriptorLength() const
{
  return microsoftOs20DescriptorLength_;
}

uint16_t EspUsbDevice::microsoftOs20VendorRevision() const
{
  return microsoftOs20VendorRevision_;
}

uint16_t EspUsbDevice::hidInterfacesLength() const
{
  return hidInterfacesLength_;
}

uint8_t EspUsbDevice::hidInterfaceCount() const
{
  return hidInterfaceCount_;
}

namespace
{
// Reject overlong encodings, surrogate code points, truncated sequences and
// values above Unicode's scalar range. No read crosses a terminating NUL.
bool nextUtf8Scalar(const char *&text, uint32_t &scalar)
{
  const uint8_t lead = static_cast<uint8_t>(*text++);
  unsigned continuation = 0;
  uint32_t minimum = 0;
  if (lead < 0x80) { scalar = lead; return true; }
  if (lead >= 0xc2 && lead <= 0xdf) { scalar = lead & 0x1f; continuation = 1; minimum = 0x80; }
  else if (lead >= 0xe0 && lead <= 0xef) { scalar = lead & 0x0f; continuation = 2; minimum = 0x800; }
  else if (lead >= 0xf0 && lead <= 0xf4) { scalar = lead & 7; continuation = 3; minimum = 0x10000; }
  else return false;
  while (continuation--) {
    const uint8_t next = static_cast<uint8_t>(*text);
    if ((next & 0xc0) != 0x80) return false;
    ++text; scalar = (scalar << 6) | (next & 0x3f);
  }
  return scalar >= minimum && scalar <= 0x10ffff && !(scalar >= 0xd800 && scalar <= 0xdfff);
}

esp_err_t encodeUsbString(const char *text, uint16_t *output, size_t capacity, size_t &units)
{
  units = 0;
  while (*text) {
    uint32_t scalar;
    if (!nextUtf8Scalar(text, scalar)) return ESP_ERR_INVALID_ARG;
    const size_t needed = scalar > 0xffff ? 2 : 1;
    if (needed > capacity - units) return ESP_ERR_INVALID_SIZE;
    if (output) {
      if (needed == 1) output[units] = uint16_t(scalar);
      else {
        scalar -= 0x10000;
        output[units] = uint16_t(0xd800 | (scalar >> 10));
        output[units + 1] = uint16_t(0xdc00 | (scalar & 0x3ff));
      }
    }
    units += needed;
  }
  return ESP_OK;
}
} // namespace

bool EspUsbDevice::buildStringDescriptors()
{
  const size_t nameEnd = FIRST_FUNCTION_STRING_INDEX + classCount_;
  size_t stringEnd = nameEnd;
  for (size_t i = 0; i < classCount_; ++i) {
    const size_t count = classAt(i)->auxiliaryStringCount();
    if (count > 256 - stringEnd) { setLastError(ESP_ERR_INVALID_SIZE); return false; }
    stringEnd += count;
  }
  const auto source = [&](size_t index) -> const char * {
    if (index == 1) return config_.manufacturer;
    if (index == 2) return config_.product;
    if (index == 3) return config_.serialNumber;
    if (index == 4) return nullptr;
    if (index < nameEnd) {
      const char *name = classAt(index - FIRST_FUNCTION_STRING_INDEX)->functionName();
      return name && *name ? name : nullptr;
    }
    size_t offset = nameEnd;
    for (size_t i = 0; i < classCount_; ++i) {
      const size_t count = classAt(i)->auxiliaryStringCount();
      if (count > 256 - offset) return nullptr;
      if (index - offset < count) return classAt(i)->auxiliaryString(index - offset);
      offset += count;
    }
    return nullptr;
  };
  size_t slots = 0, words = 0;
  for (size_t i = 1; i < stringEnd; ++i) {
    const char *value = source(i);
    if (!value) {
      if (i >= nameEnd) { setLastError(ESP_ERR_INVALID_ARG); return false; }
      continue;
    }
    size_t units;
    const esp_err_t error = encodeUsbString(value, nullptr, 126, units);
    if (error != ESP_OK) { setLastError(error); return false; }
    words += 1 + units; slots = i + 1;
  }
  if (!slots) return true;
  words += slots;
  stringDescriptors_ = static_cast<uint16_t *>(espusb::internal::allocateUsbMemory(words * sizeof(uint16_t)));
  if (!stringDescriptors_) { setLastError(ESP_ERR_NO_MEM); return false; }
  memset(stringDescriptors_, 0, slots * sizeof(uint16_t));
  size_t offset = slots;
  for (size_t i = 1; i < slots; ++i) {
    const char *value = source(i);
    if (!value) {
      if (i >= nameEnd) { setLastError(ESP_ERR_INVALID_ARG); return false; }
      continue;
    }
    if (offset >= words) { setLastError(ESP_ERR_INVALID_SIZE); return false; }
    size_t units;
    const esp_err_t error = encodeUsbString(value, stringDescriptors_ + offset + 1,
        std::min(size_t(126), words - offset - 1), units);
    if (error != ESP_OK) { setLastError(error); return false; }
    stringDescriptors_[i] = uint16_t(offset);
    stringDescriptors_[offset] = uint16_t((USB_DESC_STRING << 8) | (2 + units * 2));
    offset += 1 + units;
  }
  if (offset != words) { setLastError(ESP_ERR_INVALID_SIZE); return false; }
  stringDescriptorSlots_ = uint16_t(slots);
  return true;
}

bool EspUsbDevice::buildWebUsbUrlDescriptor()
{
  if (!config_.webusbEnabled || !config_.webusbUrl) return true;
  const char *url = config_.webusbUrl;
  uint8_t scheme = 0xff;
  if (strncmp(url, "https://", 8) == 0) { scheme = 1; url += 8; }
  else if (strncmp(url, "http://", 7) == 0) { scheme = 0; url += 7; }
  if (!*url) { setLastError(ESP_ERR_INVALID_ARG); return false; }
  size_t length = 0;
  for (const char *text = url; *text;) {
    uint32_t scalar;
    if (!nextUtf8Scalar(text, scalar)) { setLastError(ESP_ERR_INVALID_ARG); return false; }
    length = size_t(text - url);
    if (length > 252) { setLastError(ESP_ERR_INVALID_SIZE); return false; }
  }
  webUsbUrlDescriptor_ = static_cast<uint8_t *>(espusb::internal::allocateUsbMemory(length + 3));
  if (!webUsbUrlDescriptor_) { setLastError(ESP_ERR_NO_MEM); return false; }
  webUsbUrlDescriptor_[0] = uint8_t(length + 3);
  webUsbUrlDescriptor_[1] = 3; webUsbUrlDescriptor_[2] = scheme;
  memcpy(webUsbUrlDescriptor_ + 3, url, length);
  return true;
}

const uint16_t *EspUsbDevice::stringDescriptor(uint8_t index, uint16_t langid)
{
  if (!buildDescriptors()) return nullptr;
  static constexpr uint16_t language[] = {(USB_DESC_STRING << 8) | 4, 0x0409};
  if (!index) return language;
  // Keep langid=0 as a convenient alias for direct descriptor inspection.
  if ((langid && langid != 0x0409) || index >= stringDescriptorSlots_ ||
      !stringDescriptors_ || !stringDescriptors_[index]) return nullptr;
  return stringDescriptors_ + stringDescriptors_[index];
}

const uint8_t *EspUsbDevice::webUsbUrlDescriptor() const
{
  return webUsbUrlDescriptor_;
}

// Physical HID interface 0 represents either one Function or their merged
// report descriptor. Logical registration order is a separate identity.
EspUsbDeviceClass *EspUsbDevice::hidClassForInstance(uint8_t instance) const
{
  if (instance != 0 || compositeHid())
  {
    return nullptr;
  }
  for (size_t i = 0; i < classCount_; i++)
  {
    if (classAt(i) && classAt(i)->isHid())
    {
      return classAt(i);
    }
  }
  return nullptr;
}

const uint8_t *EspUsbDevice::hidReportDescriptor(uint8_t instance)
{
  if (compositeHid())
  {
    return instance == 0 ? hidReportDescriptor_ : nullptr;
  }
  EspUsbDeviceClass *hid = hidClassForInstance(instance);
  return hid ? hid->hidReportDescriptor() : nullptr;
}

uint16_t EspUsbDevice::hidReportDescriptorLength(uint8_t instance)
{
  if (compositeHid())
  {
    return instance == 0 ? hidReportDescriptorLength_ : 0;
  }
  EspUsbDeviceClass *hid = hidClassForInstance(instance);
  return hid ? hid->hidReportDescriptorLength() : 0;
}

void EspUsbDevice::handleHidSetReport(uint8_t instance, uint8_t reportId, uint8_t reportType, const uint8_t *data, uint16_t length)
{
  if (compositeHid())
  {
    for (size_t i = 0; i < classCount_; i++)
    {
      if (!classAt(i) || !classAt(i)->isHid())
      {
        continue;
      }
      if (classReportId(static_cast<uint8_t>(i)) == reportId && classAt(i))
      {
        classAt(i)->onHidSetReport(reportId, reportType, data, length);
        return;
      }
    }
    return;
  }
  EspUsbDeviceClass *hid = hidClassForInstance(instance);
  if (hid)
  {
    hid->onHidSetReport(reportId, reportType, data, length);
  }
}

void EspUsbDevice::handleHidSetProtocol(uint8_t instance, uint8_t protocol)
{
  if (compositeHid())
  {
    if (instance != 0)
    {
      return;
    }
    for (size_t i = 0; i < classCount_; i++)
    {
      if (classAt(i))
      {
        classAt(i)->onHidSetProtocol(protocol);
      }
    }
    return;
  }
  EspUsbDeviceClass *hid = hidClassForInstance(instance);
  if (hid)
  {
    hid->onHidSetProtocol(protocol);
  }
}

void EspUsbDevice::onAnyControlRequest(ControlRequestObserver callback)
{
  controlObserver_ = callback;
  controlResponseLength_ = 0;
}

void EspUsbDevice::notifyControlRequest(const EspUsbDeviceControlRequestInfo &info)
{
  if (controlObserver_)
  {
    controlObserver_(info);
  }
}

void EspUsbDevice::recordControlResponse(uint8_t bRequest, uint16_t wValue,
                                         uint16_t wIndex, uint16_t length)
{
  controlResponseRequest_ = bRequest;
  controlResponseValue_ = wValue;
  controlResponseIndex_ = wIndex;
  controlResponseLength_ = length;
}

uint16_t EspUsbDevice::takeControlResponse(uint8_t bRequest, uint16_t wValue,
                                           uint16_t wIndex)
{
  if (controlResponseLength_ == 0 || bRequest != controlResponseRequest_ ||
      wValue != controlResponseValue_ || wIndex != controlResponseIndex_)
  {
    return 0;
  }
  const uint16_t length = controlResponseLength_;
  controlResponseLength_ = 0;
  return length;
}

void EspUsbDevice::handleBusAttached()
{
  // Every class hears this, composite or not: the bus is shared by all of them.
  for (size_t i = 0; i < classCount_; i++)
  {
    if (classAt(i))
    {
      classAt(i)->onBusAttached();
    }
  }
}

void EspUsbDevice::handleBusDetached()
{
  for (size_t i = 0; i < classCount_; i++)
  {
    if (classAt(i))
    {
      classAt(i)->onBusDetached();
    }
  }
}

namespace {
bool mergeHidReport(uint8_t *dst, size_t capacity, const uint8_t *source, size_t length,
    uint8_t reportId, size_t &written)
{
  written = 0;
  bool inserted = false, skipExistingId = false;
  if (!source || length < 6 || !reportId || (dst && !capacity)) return false;
  // Each Function describes an independent HID. Keep its global units,
  // ranges, report sizes and IDs from leaking into the next Function.
  if (dst) dst[written] = 0xa4; // Push
  ++written;
  for (size_t offset = 0; offset < length;) {
    const size_t bytes = hidItemLength(source + offset, length - offset);
    if (!bytes) return false;
    const uint8_t prefix = source[offset];
    if (skipExistingId && (prefix & 0xfc) == HID_ITEM_REPORT_ID) {
      skipExistingId = false; offset += bytes; continue;
    }
    skipExistingId = false;
    if (written + bytes > UINT16_MAX || (dst && written + bytes > capacity)) return false;
    if (dst) memcpy(dst + written, source + offset, bytes);
    written += bytes;
    if (!inserted && (prefix & 0xfc) == HID_ITEM_COLLECTION) {
      if (written + 2 > UINT16_MAX || (dst && written + 2 > capacity)) return false;
      if (dst) { dst[written] = 0x85; dst[written + 1] = reportId; }
      written += 2; inserted = skipExistingId = true;
    }
    offset += bytes;
  }
  if (!inserted || written == UINT16_MAX || (dst && written == capacity)) return false;
  if (dst) dst[written] = 0xb4; // Pop
  ++written;
  return true;
}
}

bool EspUsbDevice::buildMergedHidReport()
{
  if (!compositeHid()) return true;
  size_t total = 0;
  for (size_t i = 0; i < classCount_; ++i) {
    auto *function = classAt(i);
    if (!function->isHid()) continue;
    size_t bytes = 0;
    if (!mergeHidReport(nullptr, 0, function->hidReportDescriptor(), function->hidReportDescriptorLength(),
          function->assignedReportId_, bytes) || bytes > UINT16_MAX - total) {
      setLastError(ESP_ERR_INVALID_SIZE); return false;
    }
    total += bytes;
  }
  hidReportDescriptor_ = static_cast<uint8_t *>(espusb::internal::allocateUsbMemory(total));
  if (!hidReportDescriptor_) { setLastError(ESP_ERR_NO_MEM); return false; }
  size_t offset = 0;
  for (size_t i = 0; i < classCount_; ++i) {
    auto *function = classAt(i);
    if (!function->isHid()) continue;
    size_t bytes = 0;
    if (!mergeHidReport(hidReportDescriptor_ + offset, total - offset,
        function->hidReportDescriptor(), function->hidReportDescriptorLength(), function->assignedReportId_, bytes)) {
      setLastError(ESP_ERR_INVALID_SIZE); return false;
    }
    offset += bytes;
  }
  if (offset != total) { setLastError(ESP_ERR_INVALID_SIZE); return false; }
  hidReportDescriptorLength_ = uint16_t(total);
  return true;
}

bool EspUsbDevice::buildConfigurationDescriptor(bool highSpeed)
{
  const bool composite = compositeHid();
  uint16_t compositePacket = 16;
  if (composite) {
    for (size_t i = 0; i < classCount_; ++i) if (classAt(i)->isHid()) {
      const uint16_t hint = classAt(i)->hidInEndpointSize();
      if (hint > compositePacket) compositePacket = hint;
      const uint16_t hsHint = highSpeed ? classAt(i)->hidHighSpeedEndpointSize() : 0;
      if (hsHint > compositePacket) compositePacket = hsHint;
    }
    if (compositePacket > (highSpeed ? 1024 : 64)) { setLastError(ESP_ERR_INVALID_SIZE); return false; }
  }
  auto encode = [&](uint8_t *dst, size_t capacity, size_t &total, unsigned &interfaces) -> bool {
    if (dst && capacity < 9) return false;
    size_t offset = 9;
    unsigned itf = 0, ep = 1;
    if (composite) {
      const uint16_t bytes = writeHidConfigurationDescriptor(nullptr, 0, 1, compositePacket, 0, 0, hidReportDescriptorLength_, true);
      if (!bytes || (dst && bytes > capacity - offset) || (dst && writeHidConfigurationDescriptor(dst + offset, 0, 1, compositePacket, 0, 0, hidReportDescriptorLength_, true) != bytes)) return false;
      offset += bytes; itf = 1; ep = 2;
      for (size_t i = 0; i < classCount_; ++i) if (classAt(i)->isHid() && !highSpeed) classAt(i)->descriptorInterfaceNumber_ = 0;
    }
    for (unsigned group = 0; group < 2; ++group) {
      for (size_t i = 0; i < classCount_; ++i) {
        auto *function = classAt(i);
        if (function->isHid() != (group == 0) || (composite && function->isHid())) continue;
        const unsigned count = function->interfaceCount(), endpoints = function->endpointCount();
        if (count > UINT8_MAX - itf || (endpoints && (ep > 15 || endpoints - 1 > 15 - ep))) return false;
        const uint16_t bytes = function->configurationDescriptorForSpeed(nullptr, 0, uint8_t(itf), uint8_t(ep), highSpeed);
        if (!bytes || bytes > UINT16_MAX - offset || (dst && bytes > capacity - offset)) return false;
        if (dst && function->configurationDescriptorForSpeed(dst + offset, bytes, uint8_t(itf), uint8_t(ep), highSpeed) != bytes) return false;
        if (!highSpeed) function->descriptorInterfaceNumber_ = uint8_t(itf);
        offset += bytes; itf += count; ep += endpoints;
      }
      if (!highSpeed && dst && group == 0) { hidInterfacesLength_ = uint16_t(offset - 9); hidInterfaceCount_ = uint8_t(itf); }
    }
    total = offset; interfaces = itf;
    return true;
  };
  size_t required = 0; unsigned interfaces = 0;
  if (!encode(nullptr, 0, required, interfaces)) { setLastError(ESP_ERR_INVALID_SIZE); return false; }
  std::unique_ptr<uint8_t, decltype(&espusb::internal::freeUsbMemory)> buffer(
      static_cast<uint8_t *>(espusb::internal::allocateUsbMemory(required)), espusb::internal::freeUsbMemory);
  if (!buffer) { setLastError(ESP_ERR_NO_MEM); return false; }
  size_t written = 0; unsigned emittedInterfaces = 0;
  if (!encode(buffer.get(), required, written, emittedInterfaces) || written != required || emittedInterfaces != interfaces) {
    setLastError(ESP_ERR_INVALID_SIZE); return false;
  }
  uint8_t *bytes = buffer.get();
  bytes[0] = 9; bytes[1] = USB_DESC_CONFIGURATION; put16(bytes + 2, uint16_t(required));
  bytes[4] = uint8_t(interfaces); bytes[5] = 1; bytes[6] = 0;
  bytes[7] = uint8_t(0x80 | (config_.selfPowered ? 0x40 : 0)); bytes[8] = powerToDescriptor(config_.maxPowerMilliamps);
  if (!highSpeed && !validateControllerEndpoints(bytes, uint16_t(required))) { setLastError(ESP_ERR_INVALID_SIZE); return false; }
  if (highSpeed) { configDescriptorHighSpeed_ = buffer.release(); highSpeedDescriptorLength_ = uint16_t(required); }
  else { configDescriptor_ = buffer.release(); configDescriptorLength_ = uint16_t(required); }
  return true;
}

bool EspUsbDevice::buildDescriptors()
{
  if ((running_ || g_activeDevice == this) && configDescriptorLength_) return true;
  if (rejectedClassCount_) { setLastError(ESP_ERR_INVALID_STATE); return false; }
  if (classCount_ > size_t(UINT8_MAX) - FIRST_FUNCTION_STRING_INDEX + 1) { setLastError(ESP_ERR_INVALID_SIZE); return false; }
  if (config_.deviceInterfaceGuid && !isDeviceInterfaceGuid(config_.deviceInterfaceGuid)) { setLastError(ESP_ERR_INVALID_ARG); return false; }
  RegistrationTransaction descriptorGuard{*this};
  if (classCount_ && !classes_ && !allocateClassRegistry()) return false;
  releaseDescriptors();
  // Hand every function its identity before any descriptor bytes are written.
  // CDC port() is a logical index in descriptor order. Its independent bulk
  // driver dispatches by interface number, without TinyUSB CDC instance slots.
  // HID functions are emitted first, but are never counted as CDC ports.
  uint8_t cdcInstance = 0, netInstance = 0;
  size_t auxiliaryIndex = FIRST_FUNCTION_STRING_INDEX + classCount_;
  for (size_t i = 0; i < classCount_; i++)
  {
    if (!classAt(i))
    {
      continue;
    }
    const char *name = classAt(i)->functionName();
    uint8_t stringIndex = 0;
    if (name && name[0] != '\0')
    {
      stringIndex = static_cast<uint8_t>(FIRST_FUNCTION_STRING_INDEX + i);
    }
    classAt(i)->assignFunctionIds(classAt(i)->isCdc() ? cdcInstance++ : classAt(i)->isNet() ? netInstance++ : 0,
                                   stringIndex);
    const size_t auxiliaryCount = classAt(i)->auxiliaryStringCount();
    if (auxiliaryCount > 256 - auxiliaryIndex) { setLastError(ESP_ERR_INVALID_SIZE); return false; }
    classAt(i)->assignAuxiliaryStringIds(auxiliaryCount ? uint8_t(auxiliaryIndex) : 0);
    auxiliaryIndex += auxiliaryCount;
  }

  const bool composite = compositeHid();
  // Reserve unique preferred IDs first, then assign unused IDs to repeated
  // types and custom Functions. No fixed per-type slot or temporary ID table.
  for (size_t i = 0; i < classCount_; ++i) classAt(i)->assignedReportId_ = 0;
  if (composite) {
    for (size_t i = 0; i < classCount_; ++i) {
      auto *function = classAt(i);
      if (!function->isHid()) continue;
      const uint8_t preferred = function->hidReportId();
      bool used = false;
      for (size_t j = 0; preferred && j < i; ++j) used |= classAt(j)->assignedReportId_ == preferred;
      if (preferred && !used) function->assignedReportId_ = preferred;
    }
    for (size_t i = 0; i < classCount_; ++i) {
      auto *function = classAt(i);
      if (!function->isHid() || function->assignedReportId_) continue;
      for (unsigned candidate = 1; candidate <= UINT8_MAX; ++candidate) {
        bool used = false;
        for (size_t j = 0; j < classCount_; ++j) used |= classAt(j)->assignedReportId_ == candidate;
        if (!used) { function->assignedReportId_ = uint8_t(candidate); break; }
      }
      if (!function->assignedReportId_) { setLastError(ESP_ERR_INVALID_SIZE); return false; }
    }
  }
  if (!buildMergedHidReport() || !buildConfigurationDescriptor(false)) return false;
  if (needsHighSpeedDescriptors()) {
    if (!buildConfigurationDescriptor(true)) return false;
    const size_t otherBytes = std::max(size_t(configDescriptorLength_), size_t(highSpeedDescriptorLength_));
    otherSpeedDescriptor_ = static_cast<uint8_t *>(espusb::internal::allocateUsbMemory(otherBytes));
    if (!otherSpeedDescriptor_) { setLastError(ESP_ERR_NO_MEM); return false; }
  }
  memset(deviceDescriptor_, 0, sizeof(deviceDescriptor_));
  deviceDescriptor_[0] = 18; deviceDescriptor_[1] = USB_DESC_DEVICE; put16(deviceDescriptor_ + 2, 0x0200);
  deviceDescriptor_[7] = 64; put16(deviceDescriptor_ + 8, config_.vid); put16(deviceDescriptor_ + 10, config_.pid);
  put16(deviceDescriptor_ + 12, config_.deviceVersion);
  deviceDescriptor_[14] = config_.manufacturer ? 1 : 0; deviceDescriptor_[15] = config_.product ? 2 : 0;
  deviceDescriptor_[16] = config_.serialNumber ? 3 : 0; deviceDescriptor_[17] = 1;
  for (size_t offset = 9; offset < configDescriptorLength_;) {
    const uint8_t length = configDescriptor_[offset];
    if (length < 2 || length > configDescriptorLength_ - offset) { setLastError(ESP_ERR_INVALID_SIZE); return false; }
    if (configDescriptor_[offset + 1] == USB_DESC_INTERFACE_ASSOCIATION) {
      deviceDescriptor_[4] = 0xef; deviceDescriptor_[5] = 2; deviceDescriptor_[6] = 1;
    }
    offset += length;
  }
  memset(deviceQualifierDescriptor_, 0, sizeof(deviceQualifierDescriptor_));
  deviceQualifierDescriptor_[0] = 10; deviceQualifierDescriptor_[1] = 6;
  put16(deviceQualifierDescriptor_ + 2, 0x0200);
  deviceQualifierDescriptor_[4] = deviceDescriptor_[4]; deviceQualifierDescriptor_[5] = deviceDescriptor_[5];
  deviceQualifierDescriptor_[6] = deviceDescriptor_[6]; deviceQualifierDescriptor_[7] = 64; deviceQualifierDescriptor_[8] = 1;
  if (!buildStringDescriptors() || !buildWebUsbUrlDescriptor() || !buildWebUsbDescriptors()) return false;
  if (bosDescriptorLength_) { put16(deviceDescriptor_ + 2, 0x0201); put16(deviceQualifierDescriptor_ + 2, 0x0201); }
  descriptorGuard.keep = true;
  setLastError(ESP_OK);
  return true;
}

bool EspUsbDevice::microsoftOs20SubsetLayout() const
{
  switch (config_.msOs20Layout)
  {
  case ESP_USB_DEVICE_MS_OS_20_FLAT:
    return false;
  case ESP_USB_DEVICE_MS_OS_20_SUBSETS:
    return true;
  default:
    break;
  }
  // Asking to be treated as composite makes usbccgp load whatever the interface
  // count is, so the function subsets it needs become the right shape.
  if (config_.msOs20CcgpDevice)
  {
    return true;
  }
  // Windows resolves a function subset through usbccgp.sys, which it loads only
  // for a composite device. bNumInterfaces is what decides that here: one
  // interface means no usbccgp, so the compatible ID has to sit directly under
  // the set header or it reaches nothing.
  return configDescriptor_ && configDescriptor_[4] > 1;
}

bool EspUsbDevice::microsoftOs20UsesSubsets() const
{
  return microsoftOs20DescriptorLength_ > 0 && microsoftOs20SubsetLayout();
}

namespace
{
// The GUID every EspUsbDevice vendor interface answered to before
// EspUsbDeviceConfig::deviceInterfaceGuid existed. Kept as the default so an
// existing sketch's host-side lookup does not stop finding it.
constexpr char kDefaultDeviceInterfaceGuid[] = "{975F44D9-0D08-43FD-8B3E-127CA8AFFF9D}";

// `{8-4-4-4-12}`, braces included. The registry property's length is written
// into the descriptor as a constant, so a GUID of any other length would
// silently produce a malformed set - which is why this is checked at begin()
// rather than clamped here.
bool isDeviceInterfaceGuid(const char *guid)
{
  if (!guid)
  {
    return false;
  }
  static const uint8_t groups[] = {8, 4, 4, 4, 12};
  size_t index = 0;
  if (guid[index++] != '{')
  {
    return false;
  }
  for (size_t group = 0; group < sizeof(groups); group++)
  {
    for (uint8_t i = 0; i < groups[group]; i++)
    {
      const char c = guid[index++];
      if (!isxdigit(static_cast<unsigned char>(c)))
      {
        return false;
      }
    }
    const char expected = (group + 1 < sizeof(groups)) ? '-' : '}';
    if (guid[index++] != expected)
    {
      return false;
    }
  }
  return guid[index] == '\0';
}

// CRC-16/CCITT over the finished descriptor set, so wVendorRevision moves
// whenever any byte of the set moves. Folded to 15 bits and offset by one: the
// value has to be non-zero to be a revision, and never colliding with "unset"
// matters more here than using the top bit.
uint16_t descriptorSetRevision(const uint8_t *data, size_t length)
{
  uint16_t crc = 0xffff;
  for (size_t i = 0; i < length; i++)
  {
    crc = static_cast<uint16_t>(crc ^ (static_cast<uint16_t>(data[i]) << 8));
    for (int bit = 0; bit < 8; bit++)
    {
      crc = static_cast<uint16_t>((crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1));
    }
  }
  return static_cast<uint16_t>((crc & 0x7fff) + 1);
}
} // namespace

bool EspUsbDevice::buildWebUsbDescriptors()
{
  size_t winUsbCount = 0, registryProperties = 0;
  for (size_t i = 0; i < classCount_; ++i) {
    const auto *function = classAt(i);
    if (function->isHid() || function->descriptorInterfaceNumber_ == 0xff || (!function->isVendor() && !function->isDfu())) continue;
    ++winUsbCount;
    if (function->isVendor()) ++registryProperties;
  }
  if (!config_.webusbEnabled && !winUsbCount) return true;
  const bool useSubsets = microsoftOs20SubsetLayout();
  if (winUsbCount > 1 && !useSubsets) { setLastError(ESP_ERR_INVALID_ARG); return false; }
  const size_t msBytes = winUsbCount ? 10 + (config_.msOs20CcgpDevice ? 4 : 0) +
      (useSubsets ? 8 : 6) + winUsbCount * (20 + (useSubsets ? 14 : 0)) + registryProperties * 132 : 0;
  const size_t bosBytes = 5 + (config_.webusbEnabled ? 24 : 0) + (winUsbCount ? 28 : 0);
  if (msBytes > UINT16_MAX) { setLastError(ESP_ERR_INVALID_SIZE); return false; }
  if (msBytes) {
    microsoftOs20Descriptor_ = static_cast<uint8_t *>(espusb::internal::allocateUsbMemory(msBytes));
    if (!microsoftOs20Descriptor_) { setLastError(ESP_ERR_NO_MEM); return false; }
    memset(microsoftOs20Descriptor_, 0, msBytes);
  }
  bosDescriptor_ = static_cast<uint8_t *>(espusb::internal::allocateUsbMemory(bosBytes));
  if (!bosDescriptor_) { setLastError(ESP_ERR_NO_MEM); return false; }
  memset(bosDescriptor_, 0, bosBytes);

  static constexpr uint8_t webUsbUuid[] = {
      0x38, 0xb6, 0x08, 0x34, 0xa9, 0x09, 0xa0, 0x47,
      0x8b, 0xfd, 0xa0, 0x76, 0x88, 0x15, 0xb6, 0x65,
  };
  static constexpr uint8_t microsoftOs20Uuid[] = {
      0xdf, 0x60, 0xdd, 0xd8, 0x89, 0x45, 0xc7, 0x4c,
      0x9c, 0xd2, 0x65, 0x9d, 0x9e, 0x64, 0x8a, 0x9f,
  };

  // The Microsoft descriptor set is built first: the BOS platform capability
  // has to publish its total length, and that length depends on how many
  // functions are named and whether the subsets are there at all.
  if (winUsbCount > 0)
  {
    uint8_t *const set = microsoftOs20Descriptor_;
    size_t offset = 0;

    // Set header: wLength, wDescriptorType, dwWindowsVersion, wTotalLength.
    // wTotalLength is filled in once the rest is written.
    put16(&set[offset], 10);
    offset += 2;
    put16(&set[offset], 0); // MS_OS_20_SET_HEADER_DESCRIPTOR
    offset += 2;
    put32(&set[offset], 0x06030000); // Windows 8.1 and later
    offset += 4;
    const size_t setLengthOffset = offset;
    offset += 2;

    // Device scope, so it goes under the set header ahead of any subset: it is
    // what makes usbccgp load in the first place.
    if (config_.msOs20CcgpDevice)
    {
      put16(&set[offset], 4);
      offset += 2;
      put16(&set[offset], 7); // MS_OS_20_FEATURE_CCGP_DEVICE
      offset += 2;
    }

    // Flat sets carry it once, directly under the set header. Subsets carry one
    // in every function subset, which is where Windows looks for it when
    // usbccgp has split the device up.
    if (!useSubsets)
    {
      put16(&set[offset], 6);
      offset += 2;
      put16(&set[offset], 8); // MS_OS_20_FEATURE_VENDOR_REVISION
      offset += 2;
      put16(&set[offset], 0);
      offset += 2;
    }

    size_t configurationLengthOffset = 0;
    size_t configurationOffset = offset;
    if (useSubsets)
    {
      configurationOffset = offset;
      put16(&set[offset], 8);
      offset += 2;
      put16(&set[offset], 1); // MS_OS_20_SUBSET_HEADER_CONFIGURATION
      offset += 2;
      set[offset++] = 0; // configuration index, not bConfigurationValue
      set[offset++] = 0; // bReserved
      configurationLengthOffset = offset;
      offset += 2;
    }

    bool built = true;
    // Non-HID Functions are emitted in registration order, hence ascending
    // interface order. Visit every selected Function, not just one per type.
    for (size_t i = 0; i < classCount_; ++i)
    {
      const auto *function = classAt(i);
      if (function->isHid() || function->descriptorInterfaceNumber_ == 0xff || (!function->isVendor() && !function->isDfu())) continue;
      size_t functionLengthOffset = 0;
      const size_t functionOffset = offset;
      if (useSubsets)
      {
        put16(&set[offset], 8);
        offset += 2;
        put16(&set[offset], 2); // MS_OS_20_SUBSET_HEADER_FUNCTION
        offset += 2;
        set[offset++] = function->descriptorInterfaceNumber_;
        set[offset++] = 0; // bReserved
        functionLengthOffset = offset;
        offset += 2;

        put16(&set[offset], 6);
        offset += 2;
        put16(&set[offset], 8); // MS_OS_20_FEATURE_VENDOR_REVISION
        offset += 2;
        put16(&set[offset], 0);
        offset += 2;
      }

      put16(&set[offset], 20);
      offset += 2;
      put16(&set[offset], 3); // MS_OS_20_FEATURE_COMPATIBLE_ID
      offset += 2;
      static constexpr uint8_t winUsbId[16] = {
          'W', 'I', 'N', 'U', 'S', 'B', 0, 0,
          0, 0, 0, 0, 0, 0, 0, 0,
      };
      memcpy(&set[offset], winUsbId, sizeof(winUsbId));
      offset += sizeof(winUsbId);

      if (function->isVendor())
      {
        const size_t propertyOffset = offset;
        put16(&set[offset], 132);
        offset += 2;
        put16(&set[offset], 4); // MS_OS_20_FEATURE_REG_PROPERTY
        offset += 2;
        put16(&set[offset], 7); // REG_MULTI_SZ
        offset += 2;
        put16(&set[offset], 42);
        offset += 2;
        offset += putUtf16Le(&set[offset], "DeviceInterfaceGUIDs", false);
        put16(&set[offset], 80);
        offset += 2;
        offset += putUtf16Le(&set[offset],
                             config_.deviceInterfaceGuid ? config_.deviceInterfaceGuid
                                                         : kDefaultDeviceInterfaceGuid,
                             true);
        if (offset != propertyOffset + 132)
        {
          built = false;
          break;
        }
      }

      if (offset > msBytes)
      {
        built = false;
        break;
      }
      if (useSubsets)
      {
        put16(&set[functionLengthOffset], static_cast<uint16_t>(offset - functionOffset));
      }
    }

    if (built && offset <= msBytes)
    {
      put16(&set[setLengthOffset], static_cast<uint16_t>(offset));
      if (useSubsets)
      {
        put16(&set[configurationLengthOffset],
              static_cast<uint16_t>(offset - configurationOffset));
      }
      // Derived from the completed set, with the revision fields still zero, so
      // the same descriptors always produce the same number and any change to
      // them - a different GUID, another function, a different layout - produces
      // a different one. That is what makes Windows re-read the registry
      // properties it cached the first time it saw this VID/PID/serial.
      const uint16_t revision =
          config_.msOs20VendorRevision != 0
              ? config_.msOs20VendorRevision
              : descriptorSetRevision(set, offset);
      // Walk the completed descriptors to patch revision fields, avoiding a
      // fixed-size offsets array and one more allocation per MS feature.
      for (size_t position = 10; position + 4 <= offset;) {
        const uint16_t length = uint16_t(set[position] | (uint16_t(set[position + 1]) << 8));
        const uint16_t type = uint16_t(set[position + 2] | (uint16_t(set[position + 3]) << 8));
        if (length < 4 || length > offset - position) { built = false; break; }
        if (type == 8 && length == 6) put16(set + position + 4, revision);
        position += length;
      }
      microsoftOs20VendorRevision_ = revision;
      microsoftOs20DescriptorLength_ = static_cast<uint16_t>(offset);
    }
    if (!built || offset != msBytes) { setLastError(ESP_ERR_INVALID_SIZE); return false; }
  }

  uint8_t capabilities = 0;
  size_t bosOffset = 5;
  if (config_.webusbEnabled)
  {
    bosDescriptor_[bosOffset++] = 24;
    bosDescriptor_[bosOffset++] = USB_DESC_DEVICE_CAPABILITY;
    bosDescriptor_[bosOffset++] = 0x05;
    bosDescriptor_[bosOffset++] = 0x00;
    memcpy(&bosDescriptor_[bosOffset], webUsbUuid, sizeof(webUsbUuid));
    bosOffset += sizeof(webUsbUuid);
    put16(&bosDescriptor_[bosOffset], 0x0100);
    bosOffset += 2;
    bosDescriptor_[bosOffset++] = WEBUSB_VENDOR_CODE;
    bosDescriptor_[bosOffset++] = webUsbUrlDescriptor_ ? 1 : 0;
    capabilities++;
  }

  if (microsoftOs20DescriptorLength_)
  {
    bosDescriptor_[bosOffset++] = 28;
    bosDescriptor_[bosOffset++] = USB_DESC_DEVICE_CAPABILITY;
    bosDescriptor_[bosOffset++] = 0x05;
    bosDescriptor_[bosOffset++] = 0x00;
    memcpy(&bosDescriptor_[bosOffset], microsoftOs20Uuid,
           sizeof(microsoftOs20Uuid));
    bosOffset += sizeof(microsoftOs20Uuid);
    put32(&bosDescriptor_[bosOffset], 0x06030000);
    bosOffset += 4;
    put16(&bosDescriptor_[bosOffset], microsoftOs20DescriptorLength_);
    bosOffset += 2;
    bosDescriptor_[bosOffset++] = MICROSOFT_OS_20_VENDOR_CODE;
    bosDescriptor_[bosOffset++] = 0;
    capabilities++;
  }

  if (capabilities == 0)
  {
    return true;
  }

  bosDescriptor_[0] = 5;
  bosDescriptor_[1] = USB_DESC_BOS;
  put16(&bosDescriptor_[2], static_cast<uint16_t>(bosOffset));
  bosDescriptor_[4] = capabilities;
  if (bosOffset != bosBytes) { setLastError(ESP_ERR_INVALID_SIZE); return false; }
  bosDescriptorLength_ = static_cast<uint16_t>(bosOffset);
  return true;
}

uint16_t EspUsbDevice::bulkInDoubleBufferMask(bool highSpeed, bool *fits) const
{
  if (fits)
  {
    *fits = true;
  }
  if (config_.bulkInBuffering == EspUsbBulkInBuffering::Single)
  {
    return 0;
  }

#if !ESP_USB_DEVICE_HAS_OTG
  (void)highSpeed;
  if (fits) *fits = false;
  return 0;
#else
  const auto *port = descriptorController(highSpeed);
  if (!port) { if (fits) *fits = false; return 0; }
  const uint16_t fifoDepthWords = port->otg_dfifo_depth;
  const uint8_t endpointCount = port->ep_count;

  // grxfsiz as calc_device_grxfsiz() computes it: a fixed overhead plus room
  // for the largest OUT packet, plus two words per endpoint.
  const auto receiveWords = [endpointCount](uint16_t largestOutPacket) -> uint16_t {
    return static_cast<uint16_t>(14 + 2 * ((largestOutPacket / 4) + 1) + 2 * endpointCount);
  };
  const auto packetWords = [](uint16_t packet) -> uint16_t {
    return static_cast<uint16_t>((packet + 3) / 4);
  };

  // begin has already built the selected speed. Inspect that immutable
  // snapshot instead of re-entering the public detached descriptor builder.
  const uint8_t *descriptor = highSpeed ? configDescriptorHighSpeed_ : configDescriptor_;
  if (!descriptor)
  {
    if (fits)
    {
      *fits = false;
    }
    return 0;
  }
  const uint16_t total = static_cast<uint16_t>(descriptor[2] | (descriptor[3] << 8));

  // EP0 IN is allocated before any class endpoint, and its packet size sets the
  // receive FIFO's floor.
  uint32_t inWords = 0;
  uint16_t inPacketWords[16] = {};
  inPacketWords[0] = packetWords(ESPUSB_CONTROL_PACKET_BYTES);
  uint16_t largestOutPacket = ESPUSB_CONTROL_PACKET_BYTES;
  uint16_t candidateMask = 0;
  uint32_t candidateWords = 0;

  uint16_t offset = 0;
  while (offset + 2 <= total && descriptor[offset] > 0)
  {
    const uint8_t length = descriptor[offset];
    if (descriptor[offset + 1] == USB_DESC_ENDPOINT && length >= 7)
    {
      const uint8_t address = descriptor[offset + 2];
      const uint8_t transferType = static_cast<uint8_t>(descriptor[offset + 3] & 0x03);
      const uint16_t packet =
          static_cast<uint16_t>((descriptor[offset + 4] | (descriptor[offset + 5] << 8)) & 0x07ff);
      if (address & 0x80)
      {
        const uint16_t words = packetWords(packet);
        const uint8_t number = address & 0x0f;
        inPacketWords[number] = std::max(inPacketWords[number], words);
        if (transferType == USB_ENDPOINT_ATTR_BULK)
        {
          // Only bulk IN endpoints are double buffered; the port ignores the
          // bit for anything else.
          candidateMask = static_cast<uint16_t>(candidateMask | (1u << (address & 0x0f)));
        }
      }
      else if (packet > largestOutPacket)
      {
        largestOutPacket = packet;
      }
    }
    offset = static_cast<uint16_t>(offset + length);
  }

  // Alternate descriptors reuse physical FIFOs. Budget each endpoint once.
  for (unsigned number = 0; number < 16; ++number) {
    inWords += inPacketWords[number];
    if (candidateMask & (1u << number)) candidateWords += inPacketWords[number];
  }
  if (candidateMask == 0)
  {
    return 0;
  }

  // Buffer DMA reserves two words per endpoint for its endpoint-info area
  // before anything else is allocated.
  const uint32_t available = fifoDepthWords - (2u * endpointCount);
  const uint32_t needed = inWords + candidateWords + receiveWords(largestOutPacket);
  if (needed > available)
  {
    if (fits)
    {
      *fits = false;
    }
    return 0;
  }
  return candidateMask;
#endif
}

// Whether every transmit FIFO this configuration needs can be placed in the
// controller's FIFO SPRAM.
//
// The DWC2 shares one small SPRAM between the receive FIFO, one transmit FIFO
// per IN endpoint and, in DMA mode, two words per endpoint of endpoint-info.
// The DWC2 endpoint allocator allocates downward and refuses when the space is gone, which
// for a bulk or interrupt endpoint surfaces as a failed enumeration. For an
// isochronous endpoint it does not: the allocation happens up front, through
// the old ISO allocation path. An ESP32-S3 measured on 2026-09-16 programmed
// DIEPTXF1 with 256 words at offset 512 - past the end of a 242-word area -
// without complaint. The device then enumerated, bound its host driver,
// reported frames going out, and delivered nothing.
//
// So this is checked before starting, in the same arithmetic dcd_dwc2.c uses.
// Does this configuration declare an isochronous IN endpoint? Audio capture
// and video both do; everything else in the library does not.
bool EspUsbDevice::hasIsochronousInEndpoint(bool highSpeed) const
{
  // begin has already built the selected speed. Inspect that immutable
  // snapshot instead of re-entering the public detached descriptor builder.
  const uint8_t *descriptor = highSpeed ? configDescriptorHighSpeed_ : configDescriptor_;
  if (!descriptor)
  {
    return false;
  }
  const uint16_t total = static_cast<uint16_t>(descriptor[2] | (descriptor[3] << 8));
  for (uint16_t offset = 0; offset + 2 <= total && descriptor[offset] > 0;
       offset = static_cast<uint16_t>(offset + descriptor[offset]))
  {
    if (descriptor[offset + 1] == USB_DESC_ENDPOINT && descriptor[offset] >= 7 &&
        (descriptor[offset + 2] & 0x80) != 0 &&
        (descriptor[offset + 3] & 0x03) == USB_ENDPOINT_ATTR_ISOCHRONOUS)
    {
      return true;
    }
  }
  return false;
}

bool EspUsbDevice::transmitFifoFits(bool highSpeed) const
{
#if !ESP_USB_DEVICE_HAS_OTG
  (void)highSpeed;
  return true;
#else
  const auto *port = descriptorController(highSpeed);
  if (!port) return false;
  const uint16_t fifoDepthWords = port->otg_dfifo_depth;
  const uint8_t endpointCount = port->ep_count;

  // begin has already built the selected speed. Inspect that immutable
  // snapshot instead of re-entering the public detached descriptor builder.
  const uint8_t *descriptor = highSpeed ? configDescriptorHighSpeed_ : configDescriptor_;
  if (!descriptor)
  {
    return true;
  }
  const uint16_t total = static_cast<uint16_t>(descriptor[2] | (descriptor[3] << 8));

  const auto packetWords = [](uint16_t packet) -> uint16_t {
    return static_cast<uint16_t>((packet + 3) / 4);
  };

  // EP0 IN is allocated first and its packet size also sets the receive FIFO's
  // floor. Alternate settings of the same interface reuse one endpoint number,
  // so an address is counted once, at its largest packet size.
  uint16_t inPacket[16] = {};
  inPacket[0] = ESPUSB_CONTROL_PACKET_BYTES;
  uint16_t largestOutPacket = ESPUSB_CONTROL_PACKET_BYTES;
  uint16_t offset = 0;
  while (offset + 2 <= total && descriptor[offset] > 0)
  {
    const uint8_t length = descriptor[offset];
    if (descriptor[offset + 1] == USB_DESC_ENDPOINT && length >= 7)
    {
      const uint8_t address = descriptor[offset + 2];
      const uint16_t packet =
          static_cast<uint16_t>((descriptor[offset + 4] | (descriptor[offset + 5] << 8)) & 0x07ff);
      const uint8_t number = static_cast<uint8_t>(address & 0x0f);
      if (address & 0x80)
      {
        if (packet > inPacket[number])
        {
          inPacket[number] = packet;
        }
      }
      else if (packet > largestOutPacket)
      {
        largestOutPacket = packet;
      }
    }
    offset = static_cast<uint16_t>(offset + length);
  }

  uint32_t needed = 0;
  for (uint8_t number = 0; number < 16; number++)
  {
    if (inPacket[number] != 0)
    {
      needed += packetWords(inPacket[number]);
    }
  }
  // Double-buffered bulk IN endpoints take twice their packet in FIFO.
  const uint16_t doubled = bulkInDoubleBuffered_;
  for (uint8_t number = 0; number < 16; number++)
  {
    if ((doubled & (1u << number)) != 0 && inPacket[number] != 0)
    {
      needed += packetWords(inPacket[number]);
    }
  }
  // calc_device_grxfsiz(): fixed overhead, room for the largest OUT packet,
  // two words per endpoint.
  needed += static_cast<uint32_t>(14 + 2 * ((largestOutPacket / 4) + 1) +
                                  2 * endpointCount);
  // Buffer DMA reserves two words per endpoint above everything else.
  const uint32_t available = fifoDepthWords - (2u * endpointCount);
  return needed <= available;
#endif
}

bool EspUsbDevice::validateControllerEndpoints(const uint8_t *descriptor,
                                               uint16_t length)
{
  if (!descriptor || length < 9)
  {
    return false;
  }
#if ESP_USB_DEVICE_HAS_OTG
  const auto *port = descriptorController(config_.controller != EspUsbController::FullSpeed);
  if (!port || !port->ep_count) return false;
  const uint8_t maxEndpointNumber = port->ep_count - 1;
  const uint8_t maxNonControlInEndpoints = port->ep_in_count ? port->ep_in_count - 1 : maxEndpointNumber;
  const uint8_t maxNonControlOutEndpoints = maxEndpointNumber;
#else
  constexpr uint8_t maxEndpointNumber = 15;
  constexpr uint8_t maxNonControlInEndpoints = 15;
  constexpr uint8_t maxNonControlOutEndpoints = 15;
#endif
  uint16_t inMask = 0;
  uint16_t outMask = 0;
  uint8_t endpointOwner[2][16];
  memset(endpointOwner, 0xff, sizeof(endpointOwner));
  uint8_t currentInterface = 0xff;
  for (uint16_t offset = 9; offset + 2 <= length;)
  {
    const uint8_t descriptorLength = descriptor[offset];
    if (descriptorLength < 2 ||
        offset + descriptorLength > length)
    {
      return false;
    }
    if (descriptor[offset + 1] == USB_DESC_INTERFACE &&
        descriptorLength >= 9)
    {
      currentInterface = descriptor[offset + 2];
    }
    if (descriptor[offset + 1] == USB_DESC_ENDPOINT &&
        descriptorLength >= 7)
    {
      const uint8_t address = descriptor[offset + 2];
      const uint8_t number = static_cast<uint8_t>(address & 0x0f);
      if (currentInterface == 0xff || number == 0 ||
          number > maxEndpointNumber)
      {
        return false;
      }
      const uint8_t direction = (address & 0x80) != 0 ? 1 : 0;
      uint8_t &owner = endpointOwner[direction][number];
      if (owner != 0xff && owner != currentInterface)
      {
        return false;
      }
      owner = currentInterface;
      uint16_t &mask = direction != 0 ? inMask : outMask;
      const uint16_t bit = static_cast<uint16_t>(1U << number);
      mask = static_cast<uint16_t>(mask | bit);
    }
    offset = static_cast<uint16_t>(offset + descriptorLength);
  }
  const auto endpointCount = [](uint16_t mask) {
    uint8_t count = 0;
    while (mask != 0)
    {
      count = static_cast<uint8_t>(count + (mask & 1U));
      mask >>= 1;
    }
    return count;
  };
  return endpointCount(inMask) <= maxNonControlInEndpoints &&
         endpointCount(outMask) <= maxNonControlOutEndpoints;
}

bool EspUsbDevice::compositeHid() const
{
  size_t hidCount = 0;
  for (size_t i = 0; i < classCount_; i++)
  {
    if (classAt(i) && classAt(i)->isHid())
    {
      hidCount++;
    }
  }
  return hidCount > 1;
}

bool EspUsbDevice::hasHidClass() const
{
  for (size_t i = 0; i < classCount_; i++)
  {
    if (classAt(i) && classAt(i)->isHid())
    {
      return true;
    }
  }
  return false;
}

bool EspUsbDevice::hasCdcClass() const
{
  for (size_t i = 0; i < classCount_; i++)
  {
    if (classAt(i) && classAt(i)->isCdc())
    {
      return true;
    }
  }
  return false;
}

bool EspUsbDevice::hasMidiClass() const
{
  for (size_t i = 0; i < classCount_; i++)
  {
    if (classAt(i) && classAt(i)->isMidi())
    {
      return true;
    }
  }
  return false;
}

bool EspUsbDevice::hasMscClass() const
{
  for (size_t i = 0; i < classCount_; i++)
  {
    if (classAt(i) && classAt(i)->isMsc())
    {
      return true;
    }
  }
  return false;
}

bool EspUsbDevice::hasVendorClass() const
{
  for (size_t i = 0; i < classCount_; i++)
  {
    if (classAt(i) && classAt(i)->isVendor())
    {
      return true;
    }
  }
  return false;
}

bool EspUsbDevice::hasAudioClass() const
{
  for (size_t i = 0; i < classCount_; i++)
  {
    if (classAt(i) && classAt(i)->isAudio())
    {
      return true;
    }
  }
  return false;
}

bool EspUsbDevice::hasVideoClass() const
{
  for (size_t i = 0; i < classCount_; i++)
  {
    if (classAt(i) && classAt(i)->isVideo())
    {
      return true;
    }
  }
  return false;
}

bool EspUsbDevice::hasNetClass() const
{
  for (size_t i = 0; i < classCount_; i++)
  {
    if (classAt(i) && classAt(i)->isNet())
    {
      return true;
    }
  }
  return false;
}

uint8_t EspUsbDevice::classReportId(uint8_t classInstance) const
{
  return compositeHid() && classInstance < classCount_ ? classAt(classInstance)->assignedReportId_ : 0;
}

// Registered HID Functions share physical interface 0, independently of
// their positions in the complete Function registry.
uint8_t EspUsbDevice::classRuntimeInstance(uint8_t classInstance) const
{
  // An unregistered HID must never send using another Function's slot 0.
  return classInstance < classCount_ && classAt(classInstance)->isHid() ? 0 : UINT8_MAX;
}

void EspUsbDevice::setLastError(esp_err_t error)
{
  lastError_ = error;
}

uint16_t EspUsbDeviceClass::configurationDescriptorForSpeed(uint8_t *dst, size_t capacity,
    uint8_t interfaceNumber, uint8_t endpointNumber, bool highSpeed)
{
  const uint16_t packet = isHid() ? 8 : highSpeed ? 512 : 64;
  const uint16_t required = configurationDescriptor(nullptr, interfaceNumber, endpointNumber, packet);
  if (!dst) return required;
  if (!required || capacity < required) return 0;
  if (configurationDescriptor(dst, interfaceNumber, endpointNumber, packet) != required) return 0;
  const uint16_t hidPacket = highSpeed && isHid() ? hidHighSpeedEndpointSize() : 0;
  if (hidPacket) {
    for (size_t offset = 0; offset < required;) {
      const uint8_t length = dst[offset];
      if (length < 2 || offset + length > required) return 0;
      if (dst[offset + 1] == USB_DESC_ENDPOINT && length >= 7 && (dst[offset + 3] & 3) == 3)
        put16(dst + offset + 4, hidPacket);
      offset += length;
    }
  }
  return required;
}

uint16_t EspUsbDeviceClass::maximumBulkPacketSize() const
{
  return device_.needsHighSpeedDescriptors() ? 512 : 64;
}

EspUsbDeviceClass::EspUsbDeviceClass(EspUsbDevice &device) : device_(device)
{
  device_.addClass(this);
}

EspUsbDeviceClass::~EspUsbDeviceClass()
{
  device_.removeClass(this);
}

EspUsbDeviceMscRamDisk::EspUsbDeviceMscRamDisk(uint8_t *storage, uint32_t blockCount, uint16_t blockSize)
    : storage_(storage), blockCount_(blockCount), blockSize_(blockSize)
{
}

bool EspUsbDeviceMscRamDisk::attach(EspUsbDeviceMsc &msc)
{
  if (!valid())
  {
    return false;
  }
  msc.onRead([this](uint32_t lba, uint32_t offset, void *buffer, uint32_t size)
             { return read(lba, offset, buffer, size); });
  msc.onWrite([this](uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size)
              { return write(lba, offset, buffer, size); });
  return msc.begin(blockCount_, blockSize_);
}

bool EspUsbDeviceMscRamDisk::valid() const
{
  return storage_ && blockCount_ > 0 && blockSize_ > 0;
}

uint8_t *EspUsbDeviceMscRamDisk::data()
{
  return storage_;
}

const uint8_t *EspUsbDeviceMscRamDisk::data() const
{
  return storage_;
}

uint32_t EspUsbDeviceMscRamDisk::blockCount() const
{
  return blockCount_;
}

uint16_t EspUsbDeviceMscRamDisk::blockSize() const
{
  return blockSize_;
}

size_t EspUsbDeviceMscRamDisk::byteSize() const
{
  return static_cast<size_t>(blockCount_) * blockSize_;
}

void EspUsbDeviceMscRamDisk::clear(uint8_t value)
{
  if (valid())
  {
    memset(storage_, value, byteSize());
  }
}

bool EspUsbDeviceMscRamDisk::readBlock(uint32_t lba, void *buffer) const
{
  return read(lba, 0, buffer, blockSize_) == blockSize_;
}

bool EspUsbDeviceMscRamDisk::writeBlock(uint32_t lba, const void *buffer)
{
  if (!buffer)
  {
    return false;
  }
  return write(lba, 0, const_cast<uint8_t *>(static_cast<const uint8_t *>(buffer)), blockSize_) == blockSize_;
}

void EspUsbDeviceMscRamDisk::writeByte(uint32_t lba, uint16_t offset, uint8_t value)
{
  if (!valid() || lba >= blockCount_ || offset >= blockSize_)
  {
    return;
  }
  storage_[static_cast<size_t>(lba) * blockSize_ + offset] = value;
}

int32_t EspUsbDeviceMscRamDisk::read(uint32_t lba, uint32_t offset, void *buffer, uint32_t size) const
{
  if (!valid() || !buffer || lba >= blockCount_ || offset >= blockSize_)
  {
    return -1;
  }
  const size_t start = static_cast<size_t>(lba) * blockSize_ + offset;
  const size_t end = start + size;
  if (end < start || end > byteSize())
  {
    return -1;
  }
  memcpy(buffer, storage_ + start, size);
  return static_cast<int32_t>(size);
}

int32_t EspUsbDeviceMscRamDisk::write(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size)
{
  if (!valid() || !buffer || lba >= blockCount_ || offset >= blockSize_)
  {
    return -1;
  }
  const size_t start = static_cast<size_t>(lba) * blockSize_ + offset;
  const size_t end = start + size;
  if (end < start || end > byteSize())
  {
    return -1;
  }
  memcpy(storage_ + start, buffer, size);
  return static_cast<int32_t>(size);
}

static void put16le(uint8_t *dst, uint16_t value)
{
  dst[0] = static_cast<uint8_t>(value & 0xff);
  dst[1] = static_cast<uint8_t>((value >> 8) & 0xff);
}

static void put32le(uint8_t *dst, uint32_t value)
{
  dst[0] = static_cast<uint8_t>(value & 0xff);
  dst[1] = static_cast<uint8_t>((value >> 8) & 0xff);
  dst[2] = static_cast<uint8_t>((value >> 16) & 0xff);
  dst[3] = static_cast<uint8_t>((value >> 24) & 0xff);
}

EspUsbDeviceMscFatRamDisk::EspUsbDeviceMscFatRamDisk(uint8_t *storage, size_t size)
    : storage_(storage),
      size_(size),
      blockCount_(static_cast<uint32_t>(size / 512)),
      blocks_(storage, static_cast<uint32_t>(size / 512), 512)
{
}

bool EspUsbDeviceMscFatRamDisk::format(const char *volumeLabel)
{
  if (!storage_ || blockCount_ < 16)
  {
    return false;
  }

  memset(storage_, 0, blockCount_ * 512);
  rootDirSectors_ = static_cast<uint16_t>(((rootEntryCount_ * 32) + 511) / 512);

  uint16_t sectorsPerFat = 1;
  uint16_t clusters = 0;
  for (uint8_t i = 0; i < 8; ++i)
  {
    const uint32_t dataSectors = blockCount_ - 1 - rootDirSectors_ - (2 * sectorsPerFat);
    clusters = static_cast<uint16_t>(dataSectors);
    const uint32_t fatBytes = ((static_cast<uint32_t>(clusters) + 2) * 3 + 1) / 2;
    const uint16_t requiredSectorsPerFat = static_cast<uint16_t>((fatBytes + 511) / 512);
    if (requiredSectorsPerFat == sectorsPerFat)
    {
      break;
    }
    sectorsPerFat = requiredSectorsPerFat;
  }

  sectorsPerFat_ = sectorsPerFat;
  dataStartSector_ = static_cast<uint16_t>(1 + (2 * sectorsPerFat_) + rootDirSectors_);
  if (dataStartSector_ >= blockCount_)
  {
    return false;
  }
  clusterCount_ = static_cast<uint16_t>(blockCount_ - dataStartSector_);
  nextFreeCluster_ = 2;
  nextRootEntry_ = 0;

  uint8_t *boot = storage_;
  boot[0] = 0xeb;
  boot[1] = 0x3c;
  boot[2] = 0x90;
  memcpy(boot + 3, "MSDOS5.0", 8);
  put16le(boot + 11, 512);
  boot[13] = 1;
  put16le(boot + 14, 1);
  boot[16] = 2;
  put16le(boot + 17, rootEntryCount_);
  put16le(boot + 19, static_cast<uint16_t>(blockCount_));
  boot[21] = 0xf8;
  put16le(boot + 22, sectorsPerFat_);
  put16le(boot + 24, 1);
  put16le(boot + 26, 1);
  put32le(boot + 28, 0);
  put32le(boot + 32, 0);
  boot[36] = 0x80;
  boot[38] = 0x29;
  put32le(boot + 39, 0x45535055);
  memset(boot + 43, ' ', 11);
  if (volumeLabel)
  {
    for (uint8_t i = 0; i < 11 && volumeLabel[i]; ++i)
    {
      boot[43 + i] = static_cast<uint8_t>(toupper(static_cast<unsigned char>(volumeLabel[i])));
    }
  }
  memcpy(boot + 54, "FAT12   ", 8);
  boot[510] = 0x55;
  boot[511] = 0xaa;

  storage_[512] = 0xf8;
  storage_[513] = 0xff;
  storage_[514] = 0xff;
  memcpy(storage_ + (1 + sectorsPerFat_) * 512, storage_ + 512, sectorsPerFat_ * 512);
  return true;
}

bool EspUsbDeviceMscFatRamDisk::attach(EspUsbDeviceMsc &msc)
{
  if (!blocks_.valid())
  {
    return false;
  }
  msc.onStartStop([this](uint8_t powerCondition, bool start, bool loadEject)
                  { return handleStartStop(powerCondition, start, loadEject); });
  return blocks_.attach(msc);
}

void EspUsbDeviceMscFatRamDisk::onEject(EjectCallback callback)
{
  ejectCallback_ = callback;
}

bool EspUsbDeviceMscFatRamDisk::addFile(const char *name, const uint8_t *data, size_t size)
{
  char fatName[11];
  if (!normalizeName(name, fatName) || !data)
  {
    return false;
  }
  if (exists(name) || nextRootEntry_ >= rootEntryCount_)
  {
    return false;
  }

  uint16_t firstCluster = 0;
  size_t clusterTotal = 0;
  if (!allocateClusters(size, &firstCluster, &clusterTotal))
  {
    return false;
  }

  size_t remaining = size;
  const uint8_t *src = data;
  uint16_t cluster = firstCluster;
  for (size_t i = 0; i < clusterTotal; ++i)
  {
    uint8_t *dst = clusterPtr(cluster);
    const size_t chunk = remaining > 512 ? 512 : remaining;
    if (chunk > 0)
    {
      memcpy(dst, src, chunk);
      src += chunk;
      remaining -= chunk;
    }
    cluster = fatEntry(cluster);
  }

  uint8_t *entry = rootEntry(nextRootEntry_++);
  memcpy(entry, fatName, 11);
  entry[11] = 0x20;
  put16le(entry + 26, firstCluster);
  put32le(entry + 28, static_cast<uint32_t>(size));
  return true;
}

bool EspUsbDeviceMscFatRamDisk::addTextFile(const char *name, const char *text)
{
  if (!text)
  {
    return false;
  }
  return addFile(name, reinterpret_cast<const uint8_t *>(text), strlen(text));
}

bool EspUsbDeviceMscFatRamDisk::exists(const char *name) const
{
  char fatName[11];
  return normalizeName(name, fatName) && findFile(fatName, nullptr, nullptr);
}

size_t EspUsbDeviceMscFatRamDisk::fileSize(const char *name) const
{
  char fatName[11];
  uint32_t size = 0;
  if (!normalizeName(name, fatName) || !findFile(fatName, nullptr, &size))
  {
    return 0;
  }
  return size;
}

size_t EspUsbDeviceMscFatRamDisk::readFile(const char *name, uint8_t *buffer, size_t size) const
{
  char fatName[11];
  uint32_t firstCluster = 0;
  uint32_t storedSize = 0;
  if (!normalizeName(name, fatName) || !findFile(fatName, &firstCluster, &storedSize) || !buffer)
  {
    return 0;
  }
  size_t copied = 0;
  size_t remaining = storedSize < size ? storedSize : size;
  uint16_t cluster = static_cast<uint16_t>(firstCluster);
  while (remaining > 0 && cluster >= 2 && cluster < 0xff8)
  {
    const size_t chunk = remaining > 512 ? 512 : remaining;
    memcpy(buffer + copied, clusterPtr(cluster), chunk);
    copied += chunk;
    remaining -= chunk;
    cluster = fatEntry(cluster);
  }
  return copied;
}

uint32_t EspUsbDeviceMscFatRamDisk::blockCount() const
{
  return blockCount_;
}

uint16_t EspUsbDeviceMscFatRamDisk::blockSize() const
{
  return 512;
}

size_t EspUsbDeviceMscFatRamDisk::byteSize() const
{
  return blockCount_ * 512;
}

bool EspUsbDeviceMscFatRamDisk::normalizeName(const char *name, char out[11]) const
{
  if (!name || !name[0])
  {
    return false;
  }
  memset(out, ' ', 11);
  uint8_t index = 0;
  uint8_t extIndex = 8;
  bool extension = false;
  for (const char *p = name; *p; ++p)
  {
    if (*p == '.')
    {
      extension = true;
      continue;
    }
    const uint8_t outIndex = extension ? extIndex++ : index++;
    if ((!extension && outIndex >= 8) || (extension && outIndex >= 11))
    {
      return false;
    }
    const unsigned char c = static_cast<unsigned char>(*p);
    if (!(isalnum(c) || c == '_' || c == '-' || c == '~'))
    {
      return false;
    }
    out[outIndex] = static_cast<char>(toupper(c));
  }
  return index > 0;
}

bool EspUsbDeviceMscFatRamDisk::findFile(const char name[11], uint32_t *firstCluster, uint32_t *size) const
{
  for (uint16_t i = 0; i < rootEntryCount_; ++i)
  {
    const uint8_t *entry = rootEntry(i);
    if (entry[0] == 0x00)
    {
      return false;
    }
    if (entry[0] == 0xe5 || (entry[11] & 0x08))
    {
      continue;
    }
    if (memcmp(entry, name, 11) == 0)
    {
      if (firstCluster)
      {
        *firstCluster = entry[26] | (static_cast<uint32_t>(entry[27]) << 8);
      }
      if (size)
      {
        *size = entry[28] | (static_cast<uint32_t>(entry[29]) << 8) | (static_cast<uint32_t>(entry[30]) << 16) | (static_cast<uint32_t>(entry[31]) << 24);
      }
      return true;
    }
  }
  return false;
}

bool EspUsbDeviceMscFatRamDisk::allocateClusters(size_t size, uint16_t *firstCluster, size_t *clusterTotal)
{
  const size_t needed = size == 0 ? 1 : ((size + 511) / 512);
  if (nextFreeCluster_ + needed > static_cast<uint16_t>(clusterCount_ + 2))
  {
    return false;
  }
  *firstCluster = nextFreeCluster_;
  *clusterTotal = needed;
  for (size_t i = 0; i < needed; ++i)
  {
    const uint16_t cluster = static_cast<uint16_t>(nextFreeCluster_ + i);
    const uint16_t value = (i + 1 == needed) ? 0xfff : static_cast<uint16_t>(cluster + 1);
    setFatEntry(cluster, value);
  }
  nextFreeCluster_ = static_cast<uint16_t>(nextFreeCluster_ + needed);
  return true;
}

void EspUsbDeviceMscFatRamDisk::setFatEntry(uint16_t cluster, uint16_t value)
{
  value &= 0x0fff;
  for (uint8_t fat = 0; fat < 2; ++fat)
  {
    uint8_t *base = storage_ + (1 + fat * sectorsPerFat_) * 512;
    const uint32_t offset = cluster + (cluster / 2);
    if (cluster & 1)
    {
      base[offset] = static_cast<uint8_t>((base[offset] & 0x0f) | ((value << 4) & 0xf0));
      base[offset + 1] = static_cast<uint8_t>((value >> 4) & 0xff);
    }
    else
    {
      base[offset] = static_cast<uint8_t>(value & 0xff);
      base[offset + 1] = static_cast<uint8_t>((base[offset + 1] & 0xf0) | ((value >> 8) & 0x0f));
    }
  }
}

uint16_t EspUsbDeviceMscFatRamDisk::fatEntry(uint16_t cluster) const
{
  const uint8_t *base = storage_ + 512;
  const uint32_t offset = cluster + (cluster / 2);
  if (cluster & 1)
  {
    return static_cast<uint16_t>(((base[offset] >> 4) | (base[offset + 1] << 4)) & 0x0fff);
  }
  return static_cast<uint16_t>((base[offset] | ((base[offset + 1] & 0x0f) << 8)) & 0x0fff);
}

uint8_t *EspUsbDeviceMscFatRamDisk::clusterPtr(uint16_t cluster)
{
  return storage_ + static_cast<size_t>(dataStartSector_ + cluster - 2) * 512;
}

const uint8_t *EspUsbDeviceMscFatRamDisk::clusterPtr(uint16_t cluster) const
{
  return storage_ + static_cast<size_t>(dataStartSector_ + cluster - 2) * 512;
}

uint8_t *EspUsbDeviceMscFatRamDisk::rootEntry(uint16_t index)
{
  return storage_ + static_cast<size_t>(1 + 2 * sectorsPerFat_) * 512 + static_cast<size_t>(index) * 32;
}

const uint8_t *EspUsbDeviceMscFatRamDisk::rootEntry(uint16_t index) const
{
  return storage_ + static_cast<size_t>(1 + 2 * sectorsPerFat_) * 512 + static_cast<size_t>(index) * 32;
}

bool EspUsbDeviceMscFatRamDisk::handleStartStop(uint8_t powerCondition, bool start, bool loadEject)
{
  (void)powerCondition;
  (void)loadEject;
  if (!start && ejectCallback_)
  {
    ejectCallback_();
  }
  return true;
}

#if ESP_USB_DEVICE_HAS_ARDUINO_SD
EspUsbDeviceMscSdCard::EspUsbDeviceMscSdCard(fs::SDFS &sd) : sd_(&sd)
{
}

bool EspUsbDeviceMscSdCard::begin(uint8_t ssPin, SPIClass &spi, uint32_t frequency, const char *mountpoint, uint8_t maxFiles)
{
  if (!sd_ || !sd_->begin(ssPin, spi, frequency, mountpoint, maxFiles, false))
  {
    mounted_ = false;
    blockCount_ = 0;
    blockSize_ = 0;
    return false;
  }
  blockCount_ = static_cast<uint32_t>(sd_->numSectors());
  blockSize_ = static_cast<uint16_t>(sd_->sectorSize());
  mounted_ = blockCount_ > 0 && blockSize_ == 512;
  return mounted_;
}

bool EspUsbDeviceMscSdCard::attach(EspUsbDeviceMsc &msc)
{
  if (!mounted_ || blockSize_ != 512)
  {
    return false;
  }
  msc.onRead([this](uint32_t lba, uint32_t offset, void *buffer, uint32_t size)
             { return read(lba, offset, buffer, size); });
  msc.onWrite([this](uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size)
              { return write(lba, offset, buffer, size); });
  msc.onStartStop([this](uint8_t powerCondition, bool start, bool loadEject)
                  { return handleStartStop(powerCondition, start, loadEject); });
  return msc.begin(blockCount_, blockSize_);
}

void EspUsbDeviceMscSdCard::onEject(EjectCallback callback)
{
  ejectCallback_ = callback;
}

void EspUsbDeviceMscSdCard::readOnly(bool value)
{
  readOnly_ = value;
}

bool EspUsbDeviceMscSdCard::readOnly() const
{
  return readOnly_;
}

uint32_t EspUsbDeviceMscSdCard::blockCount() const
{
  return blockCount_;
}

uint16_t EspUsbDeviceMscSdCard::blockSize() const
{
  return blockSize_;
}

bool EspUsbDeviceMscSdCard::mounted() const
{
  return mounted_;
}

int32_t EspUsbDeviceMscSdCard::read(uint32_t lba, uint32_t offset, void *buffer, uint32_t size)
{
  if (!mounted_ || !buffer || offset >= blockSize_ || lba >= blockCount_)
  {
    return -1;
  }
  const uint32_t end = lba + ((offset + size + blockSize_ - 1) / blockSize_);
  if (end < lba || end > blockCount_)
  {
    return -1;
  }

  uint8_t sector[512];
  uint8_t *dst = static_cast<uint8_t *>(buffer);
  uint32_t remaining = size;
  uint32_t sectorLba = lba;
  uint32_t sectorOffset = offset;
  while (remaining > 0)
  {
    if (!sd_->readRAW(sector, sectorLba))
    {
      return -1;
    }
    const uint32_t chunk = remaining < (blockSize_ - sectorOffset) ? remaining : (blockSize_ - sectorOffset);
    memcpy(dst, sector + sectorOffset, chunk);
    dst += chunk;
    remaining -= chunk;
    sectorOffset = 0;
    sectorLba++;
  }
  return static_cast<int32_t>(size);
}

int32_t EspUsbDeviceMscSdCard::write(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size)
{
  if (readOnly_ || !mounted_ || !buffer || offset >= blockSize_ || lba >= blockCount_)
  {
    return -1;
  }
  const uint32_t end = lba + ((offset + size + blockSize_ - 1) / blockSize_);
  if (end < lba || end > blockCount_)
  {
    return -1;
  }

  uint8_t sector[512];
  uint8_t *src = buffer;
  uint32_t remaining = size;
  uint32_t sectorLba = lba;
  uint32_t sectorOffset = offset;
  while (remaining > 0)
  {
    const uint32_t chunk = remaining < (blockSize_ - sectorOffset) ? remaining : (blockSize_ - sectorOffset);
    if (chunk != blockSize_)
    {
      if (!sd_->readRAW(sector, sectorLba))
      {
        return -1;
      }
    }
    memcpy(sector + sectorOffset, src, chunk);
    if (!sd_->writeRAW(sector, sectorLba))
    {
      return -1;
    }
    src += chunk;
    remaining -= chunk;
    sectorOffset = 0;
    sectorLba++;
  }
  return static_cast<int32_t>(size);
}

bool EspUsbDeviceMscSdCard::handleStartStop(uint8_t powerCondition, bool start, bool loadEject)
{
  (void)powerCondition;
  (void)loadEject;
  if (!start && ejectCallback_)
  {
    ejectCallback_();
  }
  return true;
}
#endif

// --- Firmware update -------------------------------------------------------

#if ESP_USB_DEVICE_HAS_OTA
static const esp_partition_t *firmwareTargetPartition()
{
  return esp_ota_get_next_update_partition(nullptr);
}
#endif

EspUsbDeviceFirmwareUpdate::EspUsbDeviceFirmwareUpdate()
{
}

EspUsbDeviceFirmwareUpdate::~EspUsbDeviceFirmwareUpdate()
{
  abort();
}

bool EspUsbDeviceFirmwareUpdate::available()
{
#if ESP_USB_DEVICE_HAS_OTA
  return firmwareTargetPartition() != nullptr;
#else
  return false;
#endif
}

size_t EspUsbDeviceFirmwareUpdate::capacity()
{
#if ESP_USB_DEVICE_HAS_OTA
  const esp_partition_t *target = firmwareTargetPartition();
  return target ? static_cast<size_t>(target->size) : 0;
#else
  return 0;
#endif
}

const char *EspUsbDeviceFirmwareUpdate::targetLabel()
{
#if ESP_USB_DEVICE_HAS_OTA
  const esp_partition_t *target = firmwareTargetPartition();
  return target ? target->label : nullptr;
#else
  return nullptr;
#endif
}

bool EspUsbDeviceFirmwareUpdate::begin(size_t expectedSize)
{
#if ESP_USB_DEVICE_HAS_OTA
  abort();
  written_ = 0;
  expectedSize_ = 0;
  randomAccess_ = false;
  lastError_ = ESP_OK;

  const esp_partition_t *target = firmwareTargetPartition();
  if (!target)
  {
    // Single-app partition scheme: there is nowhere to put a new image. Worth
    // saying plainly rather than failing on the first write, because the fix is
    // a different partition scheme and not a different upload.
    lastError_ = ESP_ERR_NOT_FOUND;
    return false;
  }
  if (expectedSize > static_cast<size_t>(target->size))
  {
    lastError_ = ESP_ERR_INVALID_SIZE;
    return false;
  }

  // OTA_WITH_SEQUENTIAL_WRITES rather than the real size on purpose: given a
  // size, esp_ota_begin() erases that many bytes before returning, and erasing
  // even a 1.25 MB partition takes seconds the caller is usually spending
  // inside a USB callback. Sequential mode erases each sector as the write
  // first reaches it, which spreads the same work across the transfer.
  esp_ota_handle_t handle = 0;
  const esp_err_t err = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &handle);
  if (err != ESP_OK)
  {
    lastError_ = err;
    return false;
  }

  handle_ = static_cast<uint32_t>(handle);
  partition_ = target;
  expectedSize_ = expectedSize;
  active_ = true;
  return true;
#else
  (void)expectedSize;
  lastError_ = ESP_ERR_NOT_SUPPORTED;
  return false;
#endif
}

bool EspUsbDeviceFirmwareUpdate::beginRandomAccess(size_t imageSize)
{
#if ESP_USB_DEVICE_HAS_OTA
  abort();
  written_ = 0;
  expectedSize_ = 0;
  randomAccess_ = false;
  lastError_ = ESP_OK;

  if (imageSize == 0)
  {
    lastError_ = ESP_ERR_INVALID_ARG;
    return false;
  }
  const esp_partition_t *target = firmwareTargetPartition();
  if (!target)
  {
    lastError_ = ESP_ERR_NOT_FOUND;
    return false;
  }
  if (imageSize > static_cast<size_t>(target->size))
  {
    lastError_ = ESP_ERR_INVALID_SIZE;
    return false;
  }

  // The real size rather than OTA_WITH_SEQUENTIAL_WRITES: esp_ota_begin()
  // erases exactly this much before returning, which is what makes a later
  // write at an arbitrary offset land in erased flash. Erasing the whole
  // partition instead would cost the caller a second it does not need to spend.
  esp_ota_handle_t handle = 0;
  const esp_err_t err = esp_ota_begin(target, imageSize, &handle);
  if (err != ESP_OK)
  {
    lastError_ = err;
    return false;
  }

  handle_ = static_cast<uint32_t>(handle);
  partition_ = target;
  expectedSize_ = imageSize;
  randomAccess_ = true;
  active_ = true;
  return true;
#else
  (void)imageSize;
  lastError_ = ESP_ERR_NOT_SUPPORTED;
  return false;
#endif
}

bool EspUsbDeviceFirmwareUpdate::write(const void *data, size_t length)
{
#if ESP_USB_DEVICE_HAS_OTA
  if (!active_ || randomAccess_)
  {
    lastError_ = ESP_ERR_INVALID_STATE;
    return false;
  }
  if (length == 0)
  {
    return true;
  }
  if (!data)
  {
    lastError_ = ESP_ERR_INVALID_ARG;
    return false;
  }

  const esp_partition_t *target = static_cast<const esp_partition_t *>(partition_);
  const size_t limit = expectedSize_ ? expectedSize_ : static_cast<size_t>(target->size);
  if (written_ + length > limit)
  {
    // Overrunning the partition would be caught by esp_ota_write() too, but a
    // host that keeps sending deserves to be stopped at the first byte past the
    // end rather than at the end of a long upload.
    lastError_ = ESP_ERR_INVALID_SIZE;
    abort();
    return false;
  }

  const esp_err_t err = esp_ota_write(static_cast<esp_ota_handle_t>(handle_), data, length);
  if (err != ESP_OK)
  {
    lastError_ = err;
    abort();
    return false;
  }
  written_ += length;
  return true;
#else
  (void)data;
  (void)length;
  lastError_ = ESP_ERR_NOT_SUPPORTED;
  return false;
#endif
}

bool EspUsbDeviceFirmwareUpdate::writeAt(size_t offset, const void *data, size_t length)
{
#if ESP_USB_DEVICE_HAS_OTA
  if (!active_ || !randomAccess_)
  {
    lastError_ = ESP_ERR_INVALID_STATE;
    return false;
  }
  if (length == 0)
  {
    return true;
  }
  if (!data)
  {
    lastError_ = ESP_ERR_INVALID_ARG;
    return false;
  }
  if (offset + length > expectedSize_)
  {
    // Past the end of what beginRandomAccess() erased, so the write would land
    // in flash nobody prepared.
    lastError_ = ESP_ERR_INVALID_SIZE;
    abort();
    return false;
  }

  const esp_err_t err = esp_ota_write_with_offset(static_cast<esp_ota_handle_t>(handle_),
                                                 data, length, offset);
  if (err != ESP_OK)
  {
    lastError_ = err;
    abort();
    return false;
  }
  written_ += length;
  return true;
#else
  (void)offset;
  (void)data;
  (void)length;
  lastError_ = ESP_ERR_NOT_SUPPORTED;
  return false;
#endif
}

bool EspUsbDeviceFirmwareUpdate::end()
{
#if ESP_USB_DEVICE_HAS_OTA
  if (!active_)
  {
    lastError_ = ESP_ERR_INVALID_STATE;
    return false;
  }

  // esp_ota_end() is the verification step: it checks the image header, the
  // declared length and the checksum, so a truncated or corrupted upload fails
  // here instead of at the next boot. Only after it passes is the boot
  // partition moved.
  const esp_err_t closed = esp_ota_end(static_cast<esp_ota_handle_t>(handle_));
  active_ = false;
  randomAccess_ = false;
  handle_ = 0;
  if (closed != ESP_OK)
  {
    lastError_ = closed;
    return false;
  }

  const esp_partition_t *target = static_cast<const esp_partition_t *>(partition_);
  const esp_err_t selected = esp_ota_set_boot_partition(target);
  if (selected != ESP_OK)
  {
    lastError_ = selected;
    return false;
  }
  lastError_ = ESP_OK;
  return true;
#else
  lastError_ = ESP_ERR_NOT_SUPPORTED;
  return false;
#endif
}

void EspUsbDeviceFirmwareUpdate::abort()
{
#if ESP_USB_DEVICE_HAS_OTA
  if (active_)
  {
    esp_ota_abort(static_cast<esp_ota_handle_t>(handle_));
  }
#endif
  active_ = false;
  randomAccess_ = false;
  handle_ = 0;
}

bool EspUsbDeviceFirmwareUpdate::active() const
{
  return active_;
}

size_t EspUsbDeviceFirmwareUpdate::written() const
{
  return written_;
}

size_t EspUsbDeviceFirmwareUpdate::expectedSize() const
{
  return expectedSize_;
}

bool EspUsbDeviceFirmwareUpdate::randomAccess() const
{
  return randomAccess_;
}

esp_err_t EspUsbDeviceFirmwareUpdate::lastError() const
{
  return lastError_;
}

const char *EspUsbDeviceFirmwareUpdate::lastErrorName() const
{
#if ESP_USB_DEVICE_HAS_OTA
  return esp_err_to_name(lastError_);
#else
  return lastError_ == ESP_OK ? "ESP_OK" : "ESP_ERR_NOT_SUPPORTED";
#endif
}

bool EspUsbDeviceFirmwareUpdate::markValid()
{
#if ESP_USB_DEVICE_HAS_OTA
  return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK;
#else
  return false;
#endif
}

bool EspUsbDeviceFirmwareUpdate::rollback()
{
#if ESP_USB_DEVICE_HAS_OTA
  return esp_ota_mark_app_invalid_rollback_and_reboot() == ESP_OK;
#else
  return false;
#endif
}

bool EspUsbDeviceFirmwareUpdate::cancelPendingBoot()
{
#if ESP_USB_DEVICE_HAS_OTA
  const esp_partition_t *running = esp_ota_get_running_partition();
  return running && esp_ota_set_boot_partition(running) == ESP_OK;
#else
  return false;
#endif
}

// --- Restarting into the ROM ----------------------------------------------

#if ESP_USB_DEVICE_HAS_OTG
// A restart cannot be issued from the callback that decided to restart.
//
// Every class callback runs on the usbd task, and the host is mid-transfer when
// it asks for one of these: DFU_DETACH still has a status stage to finish, and a
// finished DFU download still owes the host one GETSTATUS. Restarting inline
// cuts those off, and blocking the usbd task to wait for them is the same thing
// - nothing answers the host while that task is asleep.
//
// So the decision is made on the usbd task and carried out on a throwaway one
// at the lowest useful priority, which lets the usbd task finish the exchange
// during the delay. The task never returns; the chip restarts underneath it.
struct EspUsbDeviceDeferredRestart
{
  EspUsbDevice *device;
  uint32_t delayMs;
  bool toBootloader;
};

static void espUsbDeviceDeferredRestartTask(void *argument)
{
  EspUsbDeviceDeferredRestart *request =
      static_cast<EspUsbDeviceDeferredRestart *>(argument);
  const uint32_t delayMs = request->delayMs;
  EspUsbDevice *device = request->device;
  const bool toBootloader = request->toBootloader;
  delete request;

  vTaskDelay(pdMS_TO_TICKS(delayMs));
  if (toBootloader && device)
  {
    device->rebootToBootloader();
  }
  espusb_device_disconnect();
  vTaskDelay(pdMS_TO_TICKS(20));
  esp_restart();
  vTaskDelete(nullptr);
}

bool espusb::internal::scheduleDeviceRestart(EspUsbDevice *device, uint32_t delayMs,
                                        bool toBootloader)
{
  EspUsbDeviceDeferredRestart *request = new (std::nothrow)
      EspUsbDeviceDeferredRestart{device, delayMs, toBootloader};
  if (!request)
  {
    return false;
  }
  if (xTaskCreate(espUsbDeviceDeferredRestartTask, "espusb-restart", 4096,
                  request, tskIDLE_PRIORITY + 1, nullptr) != pdPASS)
  {
    delete request;
    return false;
  }
  return true;
}
#endif

bool EspUsbDevice::rebootToBootloader()
{
#if ESP_USB_DEVICE_HAS_DOWNLOAD_BOOT
#if ESP_USB_DEVICE_HAS_OTG
  // Remove the pull-up so the host records a disconnect rather than a device
  // that stopped answering. Safe from any task, unlike end(), which deletes the
  // usbd task and would therefore never return when called from it.
  if (usbStarted_)
  {
    espusb_device_disconnect();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
#endif
#if ESP_USB_DEVICE_HAS_SHARED_PHY
  // Hand the shared PHY back to USB-Serial-JTAG before restarting.
  //
  // On ESP32-S3 one internal PHY and one pin pair carry either USB-Serial-JTAG
  // or USB-OTG, and begin() took them for OTG. The selection lives in
  // RTC_CNTL_USB_CONF_REG, which is in the RTC domain and **survives a software
  // reset** - so without this the ROM's serial loader comes up with the pads
  // still routed to a controller nobody is driving, and the connector goes
  // dark. Measured: an ESP32-S3 whose only cable is its native USB reached the
  // download loader (esptool over UART connected with --before no-reset) while
  // the USB port disappeared from the host entirely.
  //
  // Deliberately not done by rebootToRomDfu(): that one wants the ROM on
  // USB-OTG, which is where the pads already are.
  CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG,
                      RTC_CNTL_SW_HW_USB_PHY_SEL | RTC_CNTL_SW_USB_PHY_SEL |
                          RTC_CNTL_USB_PAD_ENABLE);
  // And not through an external PHY either.
  CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PHY_SEL);
#endif
#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
  // RTC_CNTL_OPTION1_REG holds this one bit and nothing else, so a whole
  // register write is safe here.
  REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
#else
  // LP_SYSTEM_REG_SYS_CTRL_REG also carries LP_SYSTEM_REG_SYS_SW_RST,
  // LP_SYSTEM_REG_DIG_FIB and LP_SYSTEM_REG_IO_MUX_RESET_DISABLE. Set the bit;
  // never write the register.
  REG_SET_BIT(LP_SYSTEM_REG_SYS_CTRL_REG, LP_SYSTEM_REG_FORCE_DOWNLOAD_BOOT);
#endif
  esp_restart();
  return true;
#else
  setLastError(ESP_ERR_NOT_SUPPORTED);
  return false;
#endif
}

bool EspUsbDevice::rebootToRomDfu()
{
#if ESP_USB_DEVICE_HAS_ROM_DFU
#if ESP_USB_DEVICE_HAS_PHY_SEL_EFUSE
  // The ROM's DFU device is on USB-OTG, and on ESP32-S3 the internal PHY
  // reaches USB-OTG only when the USB_PHY_SEL eFuse is burned: the ROM routes
  // the pads from that eFuse at boot, whatever the application left behind.
  //
  // Without it the chip does reach the download loader - esptool over a UART
  // connects with --before no-reset - and the USB connector goes completely
  // dark. Measured on an ESP32-S3 whose only cable is its native USB. On a
  // board like that, restarting anyway is the difference between "this call
  // did nothing" and "this board now needs someone to press BOOT", so it
  // refuses instead.
  //
  // Burning USB_PHY_SEL is one-way and costs the board USB-Serial-JTAG for
  // good, so the library will not do it for you. EspUsbDeviceDfu implements
  // DFU in the application instead, which needs no eFuse and works the same on
  // every target.
  if (!esp_efuse_read_field_bit(ESP_EFUSE_USB_PHY_SEL))
  {
    setLastError(ESP_ERR_NOT_SUPPORTED);
    return false;
  }
#endif
#if ESP_USB_DEVICE_HAS_OTG
  if (usbStarted_)
  {
    espusb_device_disconnect();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
#endif
  // The persist flag is what makes the ROM come up as a DFU device on USB-OTG
  // instead of as a serial loader, without burning the one-way USB_PHY_SEL
  // eFuse that would cost this board USB Serial/JTAG for good.
  chip_usb_set_persist_flags(USBDC_BOOT_DFU);
  REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
  esp_restart();
  return true;
#else
  setLastError(ESP_ERR_NOT_SUPPORTED);
  return false;
#endif
}

// --- MSC firmware disk ------------------------------------------------------

namespace
{
constexpr uint32_t kFirmwareDiskSectorSize = 512;
// FAT12 addresses at most 4084 data clusters (0xFF5 is the first reserved
// value). Beyond that the volume would have to be FAT16, and raising the
// cluster size is both simpler and better here: 4 KiB clusters already line up
// with the flash sector, so a cluster boundary is an erase boundary.
constexpr uint32_t kFirmwareDiskMaxClusters = 4084;
constexpr uint16_t kFirmwareDiskClusterOptions[] = {8, 16, 32, 64, 128};

// UF2, as microsoft/uf2 defines it: 512-byte blocks, three magic words, and a
// 476-byte payload window.
constexpr uint32_t kUf2MagicStart0 = 0x0a324655; // "UF2\n"
constexpr uint32_t kUf2MagicStart1 = 0x9e5d5157;
constexpr uint32_t kUf2MagicEnd = 0x0ab16f30;
constexpr uint32_t kUf2BlockSize = 512;
constexpr uint32_t kUf2MaxPayload = 476;
constexpr uint32_t kUf2FlagNotMainFlash = 0x00000001;
constexpr uint32_t kUf2FlagFamilyIdPresent = 0x00002000;
// A ceiling on what a header may claim, so a corrupt numBlocks cannot ask for
// an absurd bitmap. 65536 blocks is 16 MiB even at the smallest useful payload.
constexpr uint32_t kUf2MaxBlocks = 65536;

void firmwareDiskPut16(uint8_t *dst, uint16_t value)
{
  dst[0] = static_cast<uint8_t>(value & 0xff);
  dst[1] = static_cast<uint8_t>((value >> 8) & 0xff);
}

void firmwareDiskPut32(uint8_t *dst, uint32_t value)
{
  dst[0] = static_cast<uint8_t>(value & 0xff);
  dst[1] = static_cast<uint8_t>((value >> 8) & 0xff);
  dst[2] = static_cast<uint8_t>((value >> 16) & 0xff);
  dst[3] = static_cast<uint8_t>((value >> 24) & 0xff);
}

uint32_t firmwareDiskRead32(const uint8_t *src)
{
  return static_cast<uint32_t>(src[0]) | (static_cast<uint32_t>(src[1]) << 8) |
         (static_cast<uint32_t>(src[2]) << 16) | (static_cast<uint32_t>(src[3]) << 24);
}

uint16_t firmwareDiskRead16(const uint8_t *src)
{
  return static_cast<uint16_t>(static_cast<uint16_t>(src[0]) |
                               (static_cast<uint16_t>(src[1]) << 8));
}

uint32_t firmwareDiskReadLe32(const uint8_t *src)
{
  return static_cast<uint32_t>(src[0]) | (static_cast<uint32_t>(src[1]) << 8) |
         (static_cast<uint32_t>(src[2]) << 16) | (static_cast<uint32_t>(src[3]) << 24);
}

bool firmwareDiskIsUf2Block(const uint8_t *block)
{
  return firmwareDiskReadLe32(block) == kUf2MagicStart0 &&
         firmwareDiskReadLe32(block + 4) == kUf2MagicStart1 &&
         firmwareDiskReadLe32(block + kUf2BlockSize - 4) == kUf2MagicEnd;
}

// "README  TXT" from "README.TXT". False for anything 8.3 cannot hold, which is
// the whole of what this volume needs to name.
bool firmwareDiskNormalizeName(const char *name, char out[11])
{
  if (!name)
  {
    return false;
  }
  memset(out, ' ', 11);
  size_t i = 0;
  for (; name[i] && name[i] != '.'; i++)
  {
    if (i >= 8)
    {
      return false;
    }
    out[i] = static_cast<char>(toupper(static_cast<unsigned char>(name[i])));
  }
  if (i == 0)
  {
    return false;
  }
  if (name[i] == '.')
  {
    const char *extension = name + i + 1;
    for (size_t j = 0; extension[j]; j++)
    {
      if (j >= 3)
      {
        return false;
      }
      out[8 + j] = static_cast<char>(toupper(static_cast<unsigned char>(extension[j])));
    }
  }
  return true;
}
} // namespace

EspUsbDeviceMscFirmwareDisk::EspUsbDeviceMscFirmwareDisk(uint8_t *storage, size_t size)
    : storage_(storage), size_(size)
{
}

uint32_t EspUsbDeviceMscFirmwareDisk::uf2FamilyId()
{
  // The registry in microsoft/uf2 (utils/uf2families.json).
#if defined(CONFIG_IDF_TARGET_ESP32S2)
  return 0xbfdd4eeeu;
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  return 0xc47e5767u;
#elif defined(CONFIG_IDF_TARGET_ESP32P4)
  return 0x3d308e94u;
#else
  return 0;
#endif
}

EspUsbDeviceMscFirmwareDisk::~EspUsbDeviceMscFirmwareDisk()
{
  if (updating_)
  {
    firmware_.abort();
  }
  releaseUf2State();
}

void EspUsbDeviceMscFirmwareDisk::releaseUf2State()
{
  free(uf2SeenBitmap_);
  uf2SeenBitmap_ = nullptr;
  uf2BaseAddress_ = 0;
  uf2TotalBlocks_ = 0;
  uf2BlocksSeen_ = 0;
}

bool EspUsbDeviceMscFirmwareDisk::begin(const char *volumeLabel)
{
  formatted_ = false;
  if (!storage_ || size_ < 8 * 1024)
  {
    return false;
  }
  const size_t capacity = EspUsbDeviceFirmwareUpdate::capacity();
  if (capacity == 0)
  {
    // Single-app partition scheme: there is nothing for this drive to write to,
    // and presenting one anyway would be a lie the host discovers late.
    return false;
  }

  const uint32_t ramSectorsAvailable = static_cast<uint32_t>(size_ / kFirmwareDiskSectorSize);
  bool laidOut = false;
  for (uint16_t candidate : kFirmwareDiskClusterOptions)
  {
    const uint32_t clusterBytes = static_cast<uint32_t>(candidate) * kFirmwareDiskSectorSize;
    const uint32_t firmwareClusters =
        static_cast<uint32_t>((capacity + clusterBytes - 1) / clusterBytes);

    // The FAT has to be big enough for every cluster, and how many clusters
    // there are depends on how much RAM the FAT leaves for the scratch area.
    // Three passes settle it; the loop guards against a layout that oscillates.
    uint16_t fatSectors = 1;
    uint32_t scratchClusters = 0;
    bool converged = false;
    for (int attempt = 0; attempt < 8; attempt++)
    {
      const uint32_t metadataSectors =
          1 + (2 * static_cast<uint32_t>(fatSectors)) + rootDirSectors_;
      if (ramSectorsAvailable <= metadataSectors)
      {
        break;
      }
      scratchClusters = (ramSectorsAvailable - metadataSectors) / candidate;
      if (scratchClusters == 0)
      {
        break;
      }
      const uint32_t totalClusters = scratchClusters + firmwareClusters;
      const uint32_t fatBytes = ((totalClusters + 2) * 3 + 1) / 2;
      const uint16_t required =
          static_cast<uint16_t>((fatBytes + kFirmwareDiskSectorSize - 1) / kFirmwareDiskSectorSize);
      if (required == fatSectors)
      {
        converged = true;
        break;
      }
      fatSectors = required;
    }
    if (!converged || scratchClusters + firmwareClusters > kFirmwareDiskMaxClusters)
    {
      continue;
    }

    sectorsPerCluster_ = candidate;
    sectorsPerFat_ = fatSectors;
    dataStartSector_ = 1 + (2u * fatSectors) + rootDirSectors_;
    scratchSectors_ = scratchClusters * candidate;
    ramSectors_ = dataStartSector_ + scratchSectors_;
    firmwareSectors_ = firmwareClusters * candidate;
    blockCount_ = ramSectors_ + firmwareSectors_;
    laidOut = true;
    break;
  }
  if (!laidOut)
  {
    return false;
  }

  memset(storage_, 0, ramSectors_ * kFirmwareDiskSectorSize);
  nextScratchCluster_ = 2;
  nextRootEntry_ = 0;
  format_ = ImageFormat::Unknown;
  releaseUf2State();

  uint8_t *boot = storage_;
  boot[0] = 0xeb;
  boot[1] = 0x3c;
  boot[2] = 0x90;
  memcpy(boot + 3, "MSDOS5.0", 8);
  firmwareDiskPut16(boot + 11, static_cast<uint16_t>(kFirmwareDiskSectorSize));
  boot[13] = static_cast<uint8_t>(sectorsPerCluster_);
  firmwareDiskPut16(boot + 14, 1); // reserved sectors
  boot[16] = 2;                    // FAT copies
  firmwareDiskPut16(boot + 17, rootEntryCount_);
  // A volume larger than 65535 sectors has to use the 32-bit count instead, and
  // the 16-bit one must then read zero.
  if (blockCount_ < 0x10000)
  {
    firmwareDiskPut16(boot + 19, static_cast<uint16_t>(blockCount_));
  }
  else
  {
    firmwareDiskPut16(boot + 19, 0);
    firmwareDiskPut32(boot + 32, blockCount_);
  }
  boot[21] = 0xf8; // fixed disk
  firmwareDiskPut16(boot + 22, sectorsPerFat_);
  firmwareDiskPut16(boot + 24, 1); // sectors per track
  firmwareDiskPut16(boot + 26, 1); // heads
  boot[36] = 0x80;
  boot[38] = 0x29;
  firmwareDiskPut32(boot + 39, 0x45535055);
  memset(boot + 43, ' ', 11);
  if (volumeLabel)
  {
    for (uint8_t i = 0; i < 11 && volumeLabel[i]; ++i)
    {
      boot[43 + i] = static_cast<uint8_t>(toupper(static_cast<unsigned char>(volumeLabel[i])));
    }
  }
  memcpy(boot + 54, "FAT12   ", 8);
  boot[510] = 0x55;
  boot[511] = 0xaa;

  // Cluster 0 and 1 are the media descriptor and the end-of-chain marker; every
  // other cluster is free, including the ones that map onto the partition. The
  // host allocates them itself when it copies the image.
  uint8_t *fat = storage_ + kFirmwareDiskSectorSize;
  fat[0] = 0xf8;
  fat[1] = 0xff;
  fat[2] = 0xff;
  memcpy(storage_ + (1 + sectorsPerFat_) * kFirmwareDiskSectorSize, fat,
         sectorsPerFat_ * kFirmwareDiskSectorSize);

  formatted_ = true;
  return true;
}

void EspUsbDeviceMscFirmwareDisk::setFatEntry(uint16_t cluster, uint16_t value)
{
  const uint32_t index = (static_cast<uint32_t>(cluster) * 3) / 2;
  uint8_t *fat = storage_ + kFirmwareDiskSectorSize;
  if (index + 1 >= static_cast<uint32_t>(sectorsPerFat_) * kFirmwareDiskSectorSize)
  {
    return;
  }
  if (cluster & 1)
  {
    fat[index] = static_cast<uint8_t>((fat[index] & 0x0f) | ((value & 0x0f) << 4));
    fat[index + 1] = static_cast<uint8_t>((value >> 4) & 0xff);
  }
  else
  {
    fat[index] = static_cast<uint8_t>(value & 0xff);
    fat[index + 1] = static_cast<uint8_t>((fat[index + 1] & 0xf0) | ((value >> 8) & 0x0f));
  }
  memcpy(storage_ + (1 + sectorsPerFat_) * kFirmwareDiskSectorSize, fat,
         sectorsPerFat_ * kFirmwareDiskSectorSize);
}

uint8_t *EspUsbDeviceMscFirmwareDisk::rootEntry(uint16_t index)
{
  const uint32_t rootStart = 1 + (2u * sectorsPerFat_);
  return storage_ + (rootStart * kFirmwareDiskSectorSize) + (index * 32);
}

const uint8_t *EspUsbDeviceMscFirmwareDisk::rootEntry(uint16_t index) const
{
  const uint32_t rootStart = 1 + (2u * sectorsPerFat_);
  return storage_ + (rootStart * kFirmwareDiskSectorSize) + (index * 32);
}

bool EspUsbDeviceMscFirmwareDisk::allocateScratchFile(const char name[11], size_t size,
                                                      uint16_t *firstCluster)
{
  const uint32_t clusterBytes =
      static_cast<uint32_t>(sectorsPerCluster_) * kFirmwareDiskSectorSize;
  const uint32_t needed = static_cast<uint32_t>((size + clusterBytes - 1) / clusterBytes);
  const uint32_t scratchClusters = scratchSectors_ / sectorsPerCluster_;
  if (needed == 0 || nextRootEntry_ >= rootEntryCount_ ||
      nextScratchCluster_ - 2 + needed > scratchClusters)
  {
    return false;
  }

  *firstCluster = nextScratchCluster_;
  for (uint32_t i = 0; i < needed; i++)
  {
    const uint16_t cluster = static_cast<uint16_t>(nextScratchCluster_ + i);
    setFatEntry(cluster, i + 1 == needed ? 0xfff : static_cast<uint16_t>(cluster + 1));
  }
  nextScratchCluster_ = static_cast<uint16_t>(nextScratchCluster_ + needed);

  uint8_t *entry = rootEntry(nextRootEntry_++);
  memcpy(entry, name, 11);
  entry[11] = 0x20; // archive
  firmwareDiskPut16(entry + 26, *firstCluster);
  firmwareDiskPut32(entry + 28, static_cast<uint32_t>(size));
  return true;
}

bool EspUsbDeviceMscFirmwareDisk::addTextFile(const char *name, const char *text)
{
  if (!formatted_ || !text)
  {
    return false;
  }
  char normalized[11];
  if (!firmwareDiskNormalizeName(name, normalized))
  {
    return false;
  }
  const size_t length = strlen(text);
  uint16_t firstCluster = 0;
  if (!allocateScratchFile(normalized, length, &firstCluster))
  {
    return false;
  }
  const uint32_t clusterSector =
      dataStartSector_ + (static_cast<uint32_t>(firstCluster - 2) * sectorsPerCluster_);
  memcpy(storage_ + (clusterSector * kFirmwareDiskSectorSize), text, length);
  return true;
}

bool EspUsbDeviceMscFirmwareDisk::attach(EspUsbDeviceMsc &msc)
{
  if (!formatted_)
  {
    return false;
  }
  msc.onRead([this](uint32_t lba, uint32_t offset, void *buffer, uint32_t size)
             { return read(lba, offset, buffer, size); });
  msc.onWrite([this](uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size)
              { return write(lba, offset, buffer, size); });
  msc.onStartStop([this](uint8_t powerCondition, bool start, bool loadEject)
                  { return handleStartStop(powerCondition, start, loadEject); });
  msc.mediaPresent(true);
  msc.isWritable(true);
  return msc.begin(blockCount_, static_cast<uint16_t>(kFirmwareDiskSectorSize));
}

void EspUsbDeviceMscFirmwareDisk::onProgress(ProgressCallback callback)
{
  progressCallback_ = callback;
}

void EspUsbDeviceMscFirmwareDisk::onComplete(CompleteCallback callback)
{
  completeCallback_ = callback;
}

void EspUsbDeviceMscFirmwareDisk::onError(ErrorCallback callback)
{
  errorCallback_ = callback;
}

void EspUsbDeviceMscFirmwareDisk::onEject(EjectCallback callback)
{
  ejectCallback_ = callback;
}

void EspUsbDeviceMscFirmwareDisk::restartWhenComplete(bool enable)
{
  restartWhenComplete_ = enable;
}

uint32_t EspUsbDeviceMscFirmwareDisk::blockCount() const
{
  return blockCount_;
}

uint16_t EspUsbDeviceMscFirmwareDisk::blockSize() const
{
  return static_cast<uint16_t>(kFirmwareDiskSectorSize);
}

uint32_t EspUsbDeviceMscFirmwareDisk::ramSectorCount() const
{
  return ramSectors_;
}

size_t EspUsbDeviceMscFirmwareDisk::written() const
{
  return firmware_.written();
}

bool EspUsbDeviceMscFirmwareDisk::updating() const
{
  return updating_;
}

bool EspUsbDeviceMscFirmwareDisk::updatingUf2() const
{
  return format_ == ImageFormat::Uf2;
}

EspUsbDeviceFirmwareUpdate &EspUsbDeviceMscFirmwareDisk::firmware()
{
  return firmware_;
}

int32_t EspUsbDeviceMscFirmwareDisk::read(uint32_t lba, uint32_t offset, void *buffer, uint32_t size)
{
  if (!formatted_ || !buffer || lba >= blockCount_)
  {
    return -1;
  }
  const uint64_t address = (static_cast<uint64_t>(lba) * kFirmwareDiskSectorSize) + offset;
  uint8_t *out = static_cast<uint8_t *>(buffer);
  const uint64_t ramBytes = static_cast<uint64_t>(ramSectors_) * kFirmwareDiskSectorSize;

  uint32_t done = 0;
  if (address < ramBytes)
  {
    const uint32_t chunk =
        static_cast<uint32_t>(ramBytes - address < size ? ramBytes - address : size);
    memcpy(out, storage_ + address, chunk);
    done = chunk;
  }
  if (done < size)
  {
#if ESP_USB_DEVICE_HAS_OTA
    // Past the scratch area the volume is the partition itself. Reading it back
    // rather than answering with zeros is what lets a host verify the file it
    // just copied - and an untouched partition reads as erased flash, which is
    // the truth about what is there.
    const esp_partition_t *target = firmwareTargetPartition();
    const uint64_t partitionOffset = (address + done) - ramBytes;
    if (target && partitionOffset < target->size)
    {
      const uint32_t chunk = static_cast<uint32_t>(
          target->size - partitionOffset < (size - done) ? target->size - partitionOffset
                                                         : (size - done));
      if (esp_partition_read(target, static_cast<size_t>(partitionOffset), out + done, chunk) != ESP_OK)
      {
        return -1;
      }
      done += chunk;
    }
#endif
    if (done < size)
    {
      memset(out + done, 0, size - done);
      done = size;
    }
  }
  return static_cast<int32_t>(size);
}

bool EspUsbDeviceMscFirmwareDisk::writeFirmwareRegion(size_t regionOffset,
                                                      const uint8_t *data, uint32_t size)
{
  if (size == 0)
  {
    return true;
  }
  if (format_ == ImageFormat::Unknown)
  {
    // What the host is copying is decided once, from the first bytes to land in
    // this region. Anything that is neither format is left alone - it is a file
    // dropped by mistake, or metadata that spilled out of the scratch area, and
    // the next write gets the same chance.
    if (size >= kUf2BlockSize && regionOffset % kUf2BlockSize == 0 &&
        firmwareDiskIsUf2Block(data))
    {
      format_ = ImageFormat::Uf2;
    }
    else if (data[0] == 0xe9)
    {
      format_ = ImageFormat::Raw;
    }
    else
    {
      return true;
    }
  }

  if (format_ == ImageFormat::Uf2)
  {
    return writeUf2Stream(regionOffset, data, size);
  }
  return writeRawImage(regionOffset, data, size);
}

bool EspUsbDeviceMscFirmwareDisk::writeRawImage(size_t regionOffset, const uint8_t *data,
                                                uint32_t size)
{
  if (!updating_)
  {
    if (!firmware_.begin())
    {
      failUpdate(firmware_.lastError());
      return false;
    }
    updating_ = true;
    imageStartSector_ = static_cast<uint32_t>(regionOffset / kFirmwareDiskSectorSize);
    regionOffset = 0;
  }
  else
  {
    const size_t imageBase =
        static_cast<size_t>(imageStartSector_) * kFirmwareDiskSectorSize;
    if (regionOffset < imageBase)
    {
      failUpdate(ESP_ERR_INVALID_STATE);
      return false;
    }
    regionOffset -= imageBase;
  }

  // Sequential only, and loudly so. A raw image says nothing about where its
  // bytes belong, so the write position is the only thing that can; a host that
  // jumps backwards or leaves a hole would otherwise produce an image that
  // looks written and is not. UF2 is the format that removes this restriction.
  if (regionOffset != firmware_.written())
  {
    failUpdate(ESP_ERR_INVALID_STATE);
    return false;
  }
  if (!firmware_.write(data, size))
  {
    failUpdate(firmware_.lastError());
    return false;
  }
  if (progressCallback_)
  {
    progressCallback_(firmware_.written());
  }
  return true;
}

bool EspUsbDeviceMscFirmwareDisk::writeUf2Stream(size_t regionOffset, const uint8_t *data,
                                                 uint32_t size)
{
  if (regionOffset % kUf2BlockSize != 0)
  {
    // A UF2 file is a whole number of 512-byte blocks and a 512-byte-sector
    // volume delivers it that way. Anything else is not a block boundary and
    // cannot be parsed as one.
    return true;
  }
  while (size >= kUf2BlockSize)
  {
    if (firmwareDiskIsUf2Block(data))
    {
      if (!writeUf2Block(data))
      {
        return false;
      }
    }
    data += kUf2BlockSize;
    size -= kUf2BlockSize;
  }
  return true;
}

bool EspUsbDeviceMscFirmwareDisk::writeUf2Block(const uint8_t *block)
{
  const uint32_t flags = firmwareDiskReadLe32(block + 8);
  const uint32_t targetAddress = firmwareDiskReadLe32(block + 12);
  const uint32_t payloadSize = firmwareDiskReadLe32(block + 16);
  const uint32_t blockNumber = firmwareDiskReadLe32(block + 20);
  const uint32_t totalBlocks = firmwareDiskReadLe32(block + 24);
  const uint32_t fileSizeOrFamily = firmwareDiskReadLe32(block + 28);

  if (payloadSize > kUf2MaxPayload || totalBlocks == 0 || totalBlocks > kUf2MaxBlocks ||
      blockNumber >= totalBlocks)
  {
    failUpdate(ESP_ERR_INVALID_SIZE);
    return false;
  }
  if ((flags & kUf2FlagFamilyIdPresent) != 0 && fileSizeOrFamily != uf2FamilyId())
  {
    // An image for a different chip. Refusing it here is the whole reason the
    // family ID exists, and it is the one mistake a raw `.bin` cannot catch.
    failUpdate(ESP_ERR_INVALID_VERSION);
    return false;
  }

  if (!updating_)
  {
    // Every block says where it goes, so the start of the image is derivable
    // from any block - which is what makes the order the host writes in
    // irrelevant.
    uf2BaseAddress_ = targetAddress - (blockNumber * payloadSize);
    uf2TotalBlocks_ = totalBlocks;
    uf2BlocksSeen_ = 0;

    const size_t imageSize = static_cast<size_t>(totalBlocks) * payloadSize;
    uf2SeenBitmap_ = static_cast<uint8_t *>(calloc((totalBlocks + 7) / 8, 1));
    if (!uf2SeenBitmap_)
    {
      failUpdate(ESP_ERR_NO_MEM);
      return false;
    }
    if (!firmware_.beginRandomAccess(imageSize))
    {
      failUpdate(firmware_.lastError());
      return false;
    }
    updating_ = true;
  }
  else if (totalBlocks != uf2TotalBlocks_)
  {
    // Two different files interleaved into one region.
    failUpdate(ESP_ERR_INVALID_STATE);
    return false;
  }

  const uint32_t byteIndex = blockNumber / 8;
  const uint8_t bit = static_cast<uint8_t>(1u << (blockNumber % 8));
  if ((uf2SeenBitmap_[byteIndex] & bit) != 0)
  {
    // Already had this one. Writing it again would be harmless; counting it
    // again would commit the update before the rest of the image arrived.
    return true;
  }

  if ((flags & kUf2FlagNotMainFlash) == 0)
  {
    if (targetAddress < uf2BaseAddress_)
    {
      failUpdate(ESP_ERR_INVALID_ARG);
      return false;
    }
    if (!firmware_.writeAt(targetAddress - uf2BaseAddress_, block + 32, payloadSize))
    {
      failUpdate(firmware_.lastError());
      return false;
    }
  }

  uf2SeenBitmap_[byteIndex] = static_cast<uint8_t>(uf2SeenBitmap_[byteIndex] | bit);
  uf2BlocksSeen_++;
  if (progressCallback_)
  {
    progressCallback_(firmware_.written());
  }
  if (uf2BlocksSeen_ >= uf2TotalBlocks_)
  {
    // Exact, unlike the raw path: the header said how many blocks there are and
    // every one of them has been seen.
    commit();
  }
  return true;
}

int32_t EspUsbDeviceMscFirmwareDisk::write(uint32_t lba, uint32_t offset, uint8_t *buffer,
                                           uint32_t size)
{
  if (!formatted_ || !buffer || lba >= blockCount_)
  {
    return -1;
  }
  const uint64_t address = (static_cast<uint64_t>(lba) * kFirmwareDiskSectorSize) + offset;
  const uint64_t ramBytes = static_cast<uint64_t>(ramSectors_) * kFirmwareDiskSectorSize;

  uint32_t done = 0;
  if (address < ramBytes)
  {
    const uint32_t chunk =
        static_cast<uint32_t>(ramBytes - address < size ? ramBytes - address : size);
    memcpy(storage_ + address, buffer, chunk);
    done = chunk;
    // The directory entry is how the host says how long the file is, and it may
    // write it before, during or after the data.
    checkForCompletion();
  }
  if (done < size)
  {
    if (!writeFirmwareRegion(static_cast<size_t>((address + done) - ramBytes), buffer + done,
                             size - done))
    {
      return -1;
    }
    checkForCompletion();
  }
  return static_cast<int32_t>(size);
}

size_t EspUsbDeviceMscFirmwareDisk::announcedImageSize() const
{
  const uint32_t scratchClusters = scratchSectors_ / sectorsPerCluster_;
  for (uint16_t i = 0; i < rootEntryCount_; i++)
  {
    const uint8_t *entry = rootEntry(i);
    if (entry[0] == 0x00)
    {
      break; // no entry has ever been used past here
    }
    if (entry[0] == 0xe5 || (entry[11] & 0x08) != 0 || (entry[11] & 0x10) != 0)
    {
      continue; // deleted, volume label, or directory
    }
    const uint16_t firstCluster = firmwareDiskRead16(entry + 26);
    if (firstCluster < 2 || firstCluster - 2 < scratchClusters)
    {
      continue; // lives in the scratch area, so it is not the image
    }
    const uint32_t size = firmwareDiskRead32(entry + 28);
    if (size > 0)
    {
      return size;
    }
  }
  return 0;
}

void EspUsbDeviceMscFirmwareDisk::checkForCompletion()
{
  // UF2 knows exactly when it is finished - the header says how many blocks
  // there are - so the directory-entry heuristic is the raw path's alone.
  if (!updating_ || format_ != ImageFormat::Raw)
  {
    return;
  }
  const size_t announced = announcedImageSize();
  if (announced == 0 || firmware_.written() < announced)
  {
    return;
  }
  commit();
}

void EspUsbDeviceMscFirmwareDisk::failUpdate(esp_err_t error)
{
  firmware_.abort();
  updating_ = false;
  imageStartSector_ = 0;
  format_ = ImageFormat::Unknown;
  releaseUf2State();
  if (errorCallback_)
  {
    errorCallback_(error);
  }
}

bool EspUsbDeviceMscFirmwareDisk::commit()
{
  if (!updating_)
  {
    return false;
  }
  updating_ = false;
  imageStartSector_ = 0;
  format_ = ImageFormat::Unknown;
  releaseUf2State();

  if (!firmware_.end())
  {
    if (errorCallback_)
    {
      errorCallback_(firmware_.lastError());
    }
    return false;
  }
  if (completeCallback_ && !completeCallback_())
  {
    EspUsbDeviceFirmwareUpdate::cancelPendingBoot();
    return false;
  }
#if ESP_USB_DEVICE_HAS_OTG
  if (restartWhenComplete_)
  {
    // Long enough for the host to finish the command that carried the last
    // block, short enough that the new firmware is up before anyone wonders.
    espusb::internal::scheduleDeviceRestart(nullptr, 500, false);
  }
#endif
  return true;
}

bool EspUsbDeviceMscFirmwareDisk::handleStartStop(uint8_t powerCondition, bool start,
                                                  bool loadEject)
{
  (void)powerCondition;
  (void)loadEject;
  if (!start)
  {
    // Ejecting is the second way an update finishes, and the only one when the
    // host never wrote a directory entry this code could read.
    if (updating_)
    {
      commit();
    }
    if (ejectCallback_)
    {
      ejectCallback_();
    }
  }
  return true;
}

EspUsbDeviceHidKeyboard::EspUsbDeviceHidKeyboard(EspUsbDevice &device) : EspUsbHidFunction(device)
{
}

bool EspUsbDeviceHidKeyboard::begin()
{
  nkroState_.clear();
  report_ = EspUsbDeviceBootKeyboardReport();
  ledsRaw_.store(0, std::memory_order_relaxed);
  busChangePending_.store(false, std::memory_order_relaxed);
  return EspUsbHidFunction::begin();
}

bool EspUsbDeviceHidKeyboard::sendReport(const EspUsbDeviceBootKeyboardReport &report, uint32_t timeoutMs)
{
  applyPendingBusChange();
  if (nkroEnabled_)
  {
    // Adopt the supplied 6-key report as the full held-key state, then emit it in
    // whatever format the active protocol needs (NKRO bitmap or boot fallback).
    nkroState_.clear();
    nkroState_.modifiers = report.modifiers;
    for (size_t i = 0; i < sizeof(report.keys); i++)
    {
      if (report.keys[i])
      {
        nkroState_.press(report.keys[i]);
      }
    }
    return sendNkroReport(timeoutMs);
  }
  report_ = report;
  return device_.sendHidReport(device_.classRuntimeInstance(hidInstance_), device_.classReportId(hidInstance_), &report_, sizeof(report_), timeoutMs);
}

bool EspUsbDeviceHidKeyboard::sendReport(const EspUsbDeviceNkroKeyboardReport &report, uint32_t timeoutMs)
{
  applyPendingBusChange();
  // Without enableNkro() the interface only ever declared the 6-key boot report,
  // so this state cannot go out as-is. Fail instead of folding it down: unlike
  // the boot-protocol path below, this is a setup mistake in the sketch, and
  // quietly dropping the seventh key onwards would never be noticed. Callers can
  // check nkroEnabled() beforehand.
  if (!nkroEnabled_)
  {
    return false;
  }
  // Replace the incremental state so a later pressUsage() / releaseUsage() sees
  // what the host was actually told.
  nkroState_ = report;
  return sendNkroReport(timeoutMs);
}

bool EspUsbDeviceHidKeyboard::pressUsage(uint8_t usage, uint8_t modifiers, uint32_t holdMs)
{
  (void)holdMs;
  applyPendingBusChange();
  if (nkroEnabled_)
  {
    // press() rejects usages this report cannot carry (above 0xDF and not a
    // modifier); surface that instead of dropping the key silently.
    if (!nkroState_.press(usage))
    {
      return false;
    }
    nkroState_.modifiers |= modifiers;
    return sendNkroReport();
  }
  report_.modifiers = modifiers;
  report_.keys[0] = usage;
  return sendReport(report_);
}

bool EspUsbDeviceHidKeyboard::pressKey(char key, uint32_t timeoutMs)
{
  uint8_t usage = 0;
  uint8_t modifiers = 0;
  if (!asciiToUsage(key, usage, modifiers))
  {
    return false;
  }
  report_.modifiers = modifiers;
  report_.keys[0] = usage;
  return sendReport(report_, timeoutMs);
}

bool EspUsbDeviceHidKeyboard::tapUsage(uint8_t usage, uint8_t modifiers, uint32_t holdMs)
{
  if (!pressUsage(usage, modifiers))
  {
    return false;
  }
  delay(holdMs);
  return releaseAll();
}

bool EspUsbDeviceHidKeyboard::tapKey(char key, uint32_t holdMs)
{
  uint8_t usage = 0;
  uint8_t modifiers = 0;
  if (!asciiToUsage(key, usage, modifiers))
  {
    return false;
  }
  return tapUsage(usage, modifiers, holdMs);
}

bool EspUsbDeviceHidKeyboard::write(const char *text, uint32_t interKeyDelayMs)
{
  if (!text)
  {
    return false;
  }
  for (const char *p = text; *p; p++)
  {
    if (!tapKey(*p))
    {
      return false;
    }
    if (interKeyDelayMs > 0)
    {
      delay(interKeyDelayMs);
    }
  }
  return true;
}

bool EspUsbDeviceHidKeyboard::releaseUsage(uint8_t usage, uint32_t timeoutMs)
{
  applyPendingBusChange();
  if (nkroEnabled_)
  {
    // release() clears modifier usages from `modifiers` too, so releasing a
    // modifier actually lets go of it instead of leaving it held until
    // releaseAll().
    if (!nkroState_.release(usage))
    {
      return false;
    }
    return sendNkroReport(timeoutMs);
  }
  for (size_t i = 0; i < sizeof(report_.keys); i++)
  {
    if (report_.keys[i] == usage)
    {
      report_.keys[i] = 0;
    }
  }
  return sendReport(report_, timeoutMs);
}

bool EspUsbDeviceHidKeyboard::releaseAll(uint32_t timeoutMs)
{
  applyPendingBusChange();
  if (nkroEnabled_)
  {
    nkroState_.clear();
    return sendNkroReport(timeoutMs);
  }
  report_ = EspUsbDeviceBootKeyboardReport();
  return sendReport(report_, timeoutMs);
}

void EspUsbDeviceHidKeyboard::setLayout(EspUsbDeviceKeyboardLayout layout)
{
  layout_ = layout;
}

EspUsbDeviceKeyboardLayout EspUsbDeviceHidKeyboard::layout() const
{
  return layout_;
}

esp_err_t EspUsbDeviceHidKeyboard::onOutputReport(OutputReportCallback callback)
{
  if (hidStarted()) return ESP_ERR_INVALID_STATE;
  outputCallback_ = std::move(callback);
  return ESP_OK;
}

esp_err_t EspUsbDeviceHidKeyboard::onProtocol(ProtocolCallback callback)
{
  if (hidStarted()) return ESP_ERR_INVALID_STATE;
  protocolCallback_ = std::move(callback);
  return ESP_OK;
}

uint8_t EspUsbDeviceHidKeyboard::protocol() const
{
  return protocol_.load(std::memory_order_relaxed);
}

uint16_t EspUsbDeviceHidKeyboard::configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize)
{
  // NKRO needs a packet big enough for its bitmap report; honour that over the
  // caller-supplied default when set.
  const uint16_t hint = hidInEndpointSize();
  if (hint > endpointSize)
  {
    endpointSize = hint;
  }
  return writeHidConfigurationDescriptor(dst,
                                         interfaceNumber,
                                         endpointNumber,
                                         endpointSize,
                                         USB_SUBCLASS_BOOT,
                                         USB_PROTOCOL_KEYBOARD,
                                         hidReportDescriptorLength(),
                                         true);
}

const uint8_t *EspUsbDeviceHidKeyboard::hidReportDescriptor() const
{
  return nkroEnabled_ ? NKRO_KEYBOARD_REPORT_DESCRIPTOR : KEYBOARD_REPORT_DESCRIPTOR;
}

uint16_t EspUsbDeviceHidKeyboard::hidReportDescriptorLength() const
{
  return nkroEnabled_ ? sizeof(NKRO_KEYBOARD_REPORT_DESCRIPTOR) : sizeof(KEYBOARD_REPORT_DESCRIPTOR);
}

uint16_t EspUsbDeviceHidKeyboard::hidInEndpointSize() const
{
  // 1 modifier byte + 28 bitmap bytes = 29 (+1 report-ID byte in composite mode).
  // Round up to 32 bus bytes; report storage is measured independently.
  return nkroEnabled_ ? 32 : 0;
}

esp_err_t EspUsbDeviceHidKeyboard::enableNkro(bool enable)
{
  if (hidStarted()) return ESP_ERR_INVALID_STATE;
  nkroEnabled_ = enable;
  if (!enable)
  {
    // Keep heldState() honest: the NKRO state means nothing in 6KRO mode, where
    // report_ carries the held keys instead.
    nkroState_.clear();
  }
  return ESP_OK;
}

bool EspUsbDeviceHidKeyboard::nkroEnabled() const
{
  return nkroEnabled_;
}

bool EspUsbDeviceHidKeyboard::sendNkroReport(uint32_t timeoutMs)
{
  const uint8_t runtime = device_.classRuntimeInstance(hidInstance_);
  const uint8_t reportId = device_.classReportId(hidInstance_);
  // Boot protocol (BIOS) ignores the report descriptor and requires the fixed
  // 6-key boot report, so fold the bitmap down to 6 held usages. These are the
  // six lowest usage numbers, not the six pressed most recently, and no
  // ErrorRollOver (0x01) is reported when more are held: identifying a 7-key
  // chord in a BIOS is not a real requirement, and the host asked for boot
  // protocol itself, so sending something valid beats failing.
  if (protocol_ == 0)
  {
    EspUsbDeviceBootKeyboardReport boot;
    boot.modifiers = nkroState_.modifiers;
    size_t slot = 0;
    for (uint16_t usage = 0; usage <= EspUsbDeviceNkroKeyboardReport::MaxBitmapUsage && slot < sizeof(boot.keys); usage++)
    {
      if (nkroState_.isDown(static_cast<uint8_t>(usage)))
      {
        boot.keys[slot++] = static_cast<uint8_t>(usage);
      }
    }
    report_ = boot;
    return device_.sendHidReport(runtime, reportId, &report_, sizeof(report_), timeoutMs);
  }
  uint8_t buffer[1 + EspUsbDeviceNkroKeyboardReport::BitmapSize];
  buffer[0] = nkroState_.modifiers;
  memcpy(&buffer[1], nkroState_.bitmap, sizeof(nkroState_.bitmap));
  return device_.sendHidReport(runtime, reportId, buffer, sizeof(buffer), timeoutMs);
}

const EspUsbDeviceNkroKeyboardReport &EspUsbDeviceHidKeyboard::heldState() const
{
  applyPendingBusChange();
  return nkroState_;
}

// Expand a raw LED byte into the public report. Both the callback path and
// ledState() go through EspUsbDeviceHidKeyboardOutputReport::setLeds(), so the
// two can never disagree about what a bit means.
static EspUsbDeviceHidKeyboardOutputReport makeKeyboardOutputReport(uint8_t leds)
{
  EspUsbDeviceHidKeyboardOutputReport report;
  report.setLeds(leds);
  return report;
}

void EspUsbDeviceHidKeyboard::onHidSetReport(uint8_t reportId, uint8_t reportType, const uint8_t *data, uint16_t length)
{
  (void)reportId;
  if (reportType != ESP_USB_DEVICE_HID_REPORT_TYPE_OUTPUT || !data || length < 1)
  {
    return;
  }
  // Record the raw byte before dispatching, so ledState() is readable even when
  // nothing took the callback slot - or when the callback belongs to an
  // integration layer and the sketch also wants to light an external LED. One
  // atomic store, so a reader on another task cannot see it half-applied.
  ledsRaw_.store(data[0], std::memory_order_relaxed);
  if (!outputCallback_)
  {
    return;
  }
  outputCallback_(makeKeyboardOutputReport(data[0]));
}

EspUsbDeviceHidKeyboardOutputReport EspUsbDeviceHidKeyboard::ledState() const
{
  // Built from a single load, so the flags always describe one host report.
  return makeKeyboardOutputReport(ledsRaw_.load(std::memory_order_relaxed));
}

// Both hooks run on the TinyUSB device task.
void EspUsbDeviceHidKeyboard::onBusAttached()
{
  ledsRaw_.store(0, std::memory_order_relaxed);
  busChangePending_.store(true, std::memory_order_relaxed);
}

void EspUsbDeviceHidKeyboard::onBusDetached()
{
  ledsRaw_.store(0, std::memory_order_relaxed);
  busChangePending_.store(true, std::memory_order_relaxed);
}

void EspUsbDeviceHidKeyboard::applyPendingBusChange() const
{
  if (!busChangePending_.exchange(false, std::memory_order_relaxed))
  {
    return;
  }
  // The host knows nothing is held, so the previous session's keys must not be
  // read back. This is not cosmetic: a state-based caller is told to suppress
  // duplicate sends by comparing against heldState() (see the NKRO section of
  // docs/DESIGN_NOTES.ja.md). If a chord survived a replug, that comparison would
  // say "same as last time, skip the send" while the host holds nothing - a stuck
  // key that never resolves. EspBle clears its equivalent state on disconnect for
  // the same reason.
  nkroState_.clear();
}

void EspUsbDeviceHidKeyboard::onHidSetProtocol(uint8_t protocol)
{
  protocol_ = protocol;
  if (!protocolCallback_)
  {
    return;
  }
  EspUsbDeviceHidProtocolEvent event;
  event.instance = hidInstance_;
  event.protocol = protocol;
  protocolCallback_(event);
}

namespace
{
// Reverse lookup from an 8-bit character to a HID usage + modifier for the given
// layout. Pure and layout-parameterized (rather than a member using layout_) so
// the host unit test in tests/unit/keymap can exercise the real code without an
// Arduino build. Kept in sync, by construction, with EspUsbHost's forward
// keymap tables (src/keymap/*.h are byte-identical between the two libraries).
//
// The tables are uint16_t[N][4]: columns [0]=unshifted, [1]=Shift, [2]=AltGr
// (Right Alt), [3]=AltGr+Shift, holding Unicode code points. Latin-1 characters
// occupy the first 256 code points, so an 8-bit input can match columns whose
// value is < 0x100; higher code points (e.g. EUR = U+20AC) are simply never
// matched by a single-byte input (see KEYMAP_FIX_REQUEST.ja.md §5).
bool espUsbDeviceAsciiToUsage(char key, EspUsbDeviceKeyboardLayout layout, uint8_t &usage, uint8_t &modifiers)
{
  const uint16_t(*table)[4] = KEYCODE_TO_UNICODE_EN_US;
  size_t tableSize = 128;
  switch (layout)
  {
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_DA_DK:
    table = KEYCODE_TO_UNICODE_DA_DK;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_DE_DE:
    table = KEYCODE_TO_UNICODE_DE_DE;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_GB:
    table = KEYCODE_TO_UNICODE_EN_GB;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_ES_ES:
    table = KEYCODE_TO_UNICODE_ES_ES;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_FI_FI:
    table = KEYCODE_TO_UNICODE_FI_FI;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_FR_CH:
    table = KEYCODE_TO_UNICODE_FR_CH;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_FR_FR:
    table = KEYCODE_TO_UNICODE_FR_FR;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_HU_HU:
    table = KEYCODE_TO_UNICODE_HU_HU;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_IT_IT:
    table = KEYCODE_TO_UNICODE_IT_IT;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP:
    table = KEYCODE_TO_UNICODE_JA_JP;
    tableSize = 0x90;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_NB_NO:
    table = KEYCODE_TO_UNICODE_NB_NO;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_NL_NL:
    table = KEYCODE_TO_UNICODE_NL_NL;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_PT_BR:
    table = KEYCODE_TO_UNICODE_PT_BR;
    // pt_BR carries International1 (/ and ?) at 0x87 and the numpad comma at
    // 0x85, so the reverse lookup must scan the extended 0x90-entry table.
    tableSize = 0x90;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_PT_PT:
    table = KEYCODE_TO_UNICODE_PT_PT;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_SV_SE:
    table = KEYCODE_TO_UNICODE_SV_SE;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_KO_KR:
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_ZH_CN:
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_ZH_TW:
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US:
  default:
    break;
  }

  usage = 0;
  modifiers = 0;
  const uint16_t c = static_cast<uint8_t>(key);

  // Pass 1: base / Shift. This preserves the pre-AltGr behaviour exactly (same
  // per-keycode base-then-Shift precedence), so existing single-level layouts
  // are unaffected.
  for (size_t keycode = 0; keycode < tableSize; keycode++)
  {
    if (table[keycode][0] == c)
    {
      usage = static_cast<uint8_t>(keycode);
      return true;
    }
    if (table[keycode][1] == c)
    {
      usage = static_cast<uint8_t>(keycode);
      modifiers = ESP_USB_DEVICE_MOD_LEFT_SHIFT;
      return true;
    }
  }

  // Pass 2: AltGr (Right Alt) fallback, only for characters not reachable on
  // the base / Shift levels. This is what makes e.g. '@' typeable on de_DE
  // (AltGr+Q) or '/' on pt_BR without breaking any base-level mapping.
  for (size_t keycode = 0; keycode < tableSize; keycode++)
  {
    if (table[keycode][2] == c)
    {
      usage = static_cast<uint8_t>(keycode);
      modifiers = ESP_USB_DEVICE_MOD_RIGHT_ALT;
      return true;
    }
    if (table[keycode][3] == c)
    {
      usage = static_cast<uint8_t>(keycode);
      modifiers = ESP_USB_DEVICE_MOD_RIGHT_ALT | ESP_USB_DEVICE_MOD_LEFT_SHIFT;
      return true;
    }
  }
  return false;
}
} // namespace

bool EspUsbDeviceHidKeyboard::asciiToUsage(char key, uint8_t &usage, uint8_t &modifiers) const
{
  return espUsbDeviceAsciiToUsage(key, layout_, usage, modifiers);
}

EspUsbDeviceHidMouse::EspUsbDeviceHidMouse(EspUsbDevice &device) : EspUsbHidFunction(device)
{
}

bool EspUsbDeviceHidMouse::begin()
{
  return EspUsbHidFunction::begin();
}

bool EspUsbDeviceHidMouse::sendReport(const EspUsbDeviceBootMouseReport &report, uint32_t timeoutMs)
{
  return device_.sendHidReport(device_.classRuntimeInstance(hidInstance_), device_.classReportId(hidInstance_), &report, sizeof(report), timeoutMs);
}

bool EspUsbDeviceHidMouse::sendState(uint32_t timeoutMs)
{
  EspUsbDeviceBootMouseReport report;
  report.buttons = buttons_;
  return sendReport(report, timeoutMs);
}

bool EspUsbDeviceHidMouse::move(int8_t x, int8_t y, int8_t wheel, uint8_t buttons, uint32_t timeoutMs)
{
  buttons_ = buttons;
  EspUsbDeviceBootMouseReport report;
  report.buttons = buttons_;
  report.x = x;
  report.y = y;
  report.wheel = wheel;
  return sendReport(report, timeoutMs);
}

bool EspUsbDeviceHidMouse::wheel(int8_t wheel, uint32_t timeoutMs)
{
  return move(0, 0, wheel, buttons_, timeoutMs);
}

bool EspUsbDeviceHidMouse::press(uint8_t buttons, uint32_t timeoutMs)
{
  buttons_ |= buttons;
  return sendState(timeoutMs);
}

bool EspUsbDeviceHidMouse::release(uint8_t buttons, uint32_t timeoutMs)
{
  buttons_ &= static_cast<uint8_t>(~buttons);
  return sendState(timeoutMs);
}

bool EspUsbDeviceHidMouse::releaseAll(uint32_t timeoutMs)
{
  buttons_ = 0;
  return sendState(timeoutMs);
}

bool EspUsbDeviceHidMouse::click(uint8_t button, uint32_t holdMs)
{
  if (!press(button))
  {
    return false;
  }
  delay(holdMs);
  return release(button);
}

uint8_t EspUsbDeviceHidMouse::buttons() const
{
  return buttons_;
}

uint16_t EspUsbDeviceHidMouse::configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize)
{
  return writeHidConfigurationDescriptor(dst,
                                         interfaceNumber,
                                         endpointNumber,
                                         endpointSize,
                                         USB_SUBCLASS_BOOT,
                                         USB_PROTOCOL_MOUSE,
                                         hidReportDescriptorLength(),
                                         false);
}

const uint8_t *EspUsbDeviceHidMouse::hidReportDescriptor() const
{
  return MOUSE_REPORT_DESCRIPTOR;
}

uint16_t EspUsbDeviceHidMouse::hidReportDescriptorLength() const
{
  return sizeof(MOUSE_REPORT_DESCRIPTOR);
}

EspUsbDeviceHidCustom::EspUsbDeviceHidCustom(EspUsbDevice &device, const uint8_t *reportDescriptor, uint16_t reportDescriptorLength, uint16_t inputReportSize)
    : EspUsbHidFunction(device), reportDescriptor_(reportDescriptor), reportDescriptorLength_(reportDescriptorLength), inputReportSize_(inputReportSize)
{
}

bool EspUsbDeviceHidCustom::begin()
{
  if (!reportDescriptor_ || !reportDescriptorLength_ || !inputReportSize_)
    return failHidBegin(ESP_ERR_INVALID_ARG);
  return EspUsbHidFunction::begin();
}

bool EspUsbDeviceHidCustom::sendReport(const void *data, size_t length, uint8_t reportId, uint32_t timeoutMs)
{
  if (device_.compositeHid()) {
    const uint8_t assigned = device_.classReportId(hidInstance_);
    if (reportId && reportId != assigned) return false;
    reportId = assigned;
  }
  if (!data || length == 0)
  {
    return false;
  }
  return device_.sendHidReport(device_.classRuntimeInstance(hidInstance_), reportId, data, length, timeoutMs);
}

uint16_t EspUsbDeviceHidCustom::configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize)
{
  const uint16_t mps = inputReportSize_ < endpointSize ? inputReportSize_ : endpointSize;
  return writeHidConfigurationDescriptor(dst,
                                         interfaceNumber,
                                         endpointNumber,
                                         mps,
                                         0,
                                         0,
                                         reportDescriptorLength_,
                                         false);
}

const uint8_t *EspUsbDeviceHidCustom::hidReportDescriptor() const
{
  return reportDescriptor_;
}

uint16_t EspUsbDeviceHidCustom::hidReportDescriptorLength() const
{
  return reportDescriptorLength_;
}

EspUsbDeviceHidVendor::EspUsbDeviceHidVendor(EspUsbDevice &device, uint16_t reportSize)
    : EspUsbHidFunction(device), reportSize_(reportSize)
{
  buildReportDescriptor();
}

uint16_t EspUsbDeviceHidVendor::maxReportSize()
{
  // The interim core carries a uint16_t complete transfer length, including
  // the report ID. Packet size and available runtime memory are separate.
  return UINT16_MAX - 1;
}

void EspUsbDeviceHidVendor::buildReportDescriptor()
{
  uint8_t *dst = reportDescriptor_;
  const uint8_t head[] = {
      0x06, 0x00, 0xff, // Usage Page (Vendor Defined 0xff00)
      0x09, 0x01,       // Usage (1)
      0xa1, 0x01,       // Collection (Application)
      0x85, ESP_USB_DEVICE_HID_REPORT_ID_VENDOR, //   Report ID
      0x15, 0x00,       //   Logical Minimum (0)
      0x26, 0xff, 0x00, //   Logical Maximum (255)
      0x75, 0x08,       //   Report Size (8 bits)
  };
  memcpy(dst, head, sizeof(head));
  size_t offset = sizeof(head);
  if (reportSize_ > 0xff)
  {
    // Report Count with a two-byte payload. Anything above 255 needs it, and a
    // high-speed interrupt endpoint makes those sizes reachable.
    dst[offset++] = 0x96;
    dst[offset++] = static_cast<uint8_t>(reportSize_ & 0xff);
    dst[offset++] = static_cast<uint8_t>((reportSize_ >> 8) & 0xff);
  }
  else
  {
    dst[offset++] = 0x95;
    dst[offset++] = static_cast<uint8_t>(reportSize_ & 0xff);
  }
  const uint8_t tail[] = {
      0x09, 0x01, //   Usage (1)
      0x81, 0x02, //   Input (Data,Var,Abs)
      0x09, 0x01, //   Usage (1)
      0x91, 0x02, //   Output (Data,Var,Abs)
      0x09, 0x01, //   Usage (1)
      0xb1, 0x02, //   Feature (Data,Var,Abs)
      0xc0,       // End Collection
  };
  memcpy(dst + offset, tail, sizeof(tail));
  offset += sizeof(tail);
  reportDescriptorLength_ = static_cast<uint8_t>(offset);
}

bool EspUsbDeviceHidVendor::begin()
{
  if (!reportSize_) return failHidBegin(ESP_ERR_INVALID_ARG);
  if (reportSize_ > maxReportSize()) return failHidBegin(ESP_ERR_INVALID_SIZE);
  return EspUsbHidFunction::begin();
}

bool EspUsbDeviceHidVendor::sendInput(const void *data, size_t length, uint32_t timeoutMs)
{
  if (!data || length == 0)
  {
    return false;
  }
  if (length > reportSize_) return false;
  return device_.sendHidReport(device_.classRuntimeInstance(hidInstance_), device_.compositeHid() ? device_.classReportId(hidInstance_) : hidReportId(), data, length, timeoutMs);
}

esp_err_t EspUsbDeviceHidVendor::onOutputReport(ReportCallback callback)
{
  if (hidStarted()) return ESP_ERR_INVALID_STATE;
  outputCallback_ = std::move(callback);
  return ESP_OK;
}

esp_err_t EspUsbDeviceHidVendor::onFeatureReport(ReportCallback callback)
{
  if (hidStarted()) return ESP_ERR_INVALID_STATE;
  featureCallback_ = std::move(callback);
  return ESP_OK;
}

uint16_t EspUsbDeviceHidVendor::configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize)
{
  // This legacy primitive uses the full-speed ceiling. The capacity-aware
  // speed wrapper applies hidHighSpeedEndpointSize() for HS. A larger report
  // at FS is still split over several packets of the same transfer.
  uint16_t mps = static_cast<uint16_t>(reportSize_ + 1);
  if (mps < endpointSize)
  {
    mps = endpointSize;
  }
  if (mps > ESP_USB_DEVICE_FS_INTERRUPT_MAX_PACKET)
  {
    mps = ESP_USB_DEVICE_FS_INTERRUPT_MAX_PACKET;
  }
  return writeHidConfigurationDescriptor(dst,
                                         interfaceNumber,
                                         endpointNumber,
                                         mps,
                                         0,
                                         0,
                                         hidReportDescriptorLength(),
                                         true);
}

const uint8_t *EspUsbDeviceHidVendor::hidReportDescriptor() const
{
  return reportDescriptor_;
}

uint16_t EspUsbDeviceHidVendor::hidReportDescriptorLength() const
{
  return reportDescriptorLength_;
}

uint16_t EspUsbDeviceHidVendor::hidHighSpeedEndpointSize() const
{
  const uint16_t wanted = static_cast<uint16_t>(reportSize_ + 1);
  if (wanted <= ESP_USB_DEVICE_FS_INTERRUPT_MAX_PACKET)
  {
    return 0; // one full-speed packet is already enough; both speeds agree
  }
  return std::min(uint16_t(1024), wanted);
}

void EspUsbDeviceHidVendor::onHidSetReport(uint8_t reportId, uint8_t reportType, const uint8_t *data, uint16_t length)
{
  EspUsbDeviceHidReport report;
  report.reportId = reportId;
  report.reportType = reportType;
  report.data = data;
  report.length = length;
  if (reportType == ESP_USB_DEVICE_HID_REPORT_TYPE_OUTPUT && outputCallback_)
  {
    outputCallback_(report);
  }
  else if (reportType == ESP_USB_DEVICE_HID_REPORT_TYPE_FEATURE && featureCallback_)
  {
    featureCallback_(report);
  }
}

EspUsbDeviceHidGamepad::EspUsbDeviceHidGamepad(EspUsbDevice &device) : EspUsbHidFunction(device)
{
}

bool EspUsbDeviceHidGamepad::begin()
{
  return EspUsbHidFunction::begin();
}

bool EspUsbDeviceHidGamepad::sendReport(const EspUsbDeviceGamepadReport &report, uint32_t timeoutMs)
{
  report_ = report;
  uint8_t data[11] = {
      static_cast<uint8_t>(report_.x),
      static_cast<uint8_t>(report_.y),
      static_cast<uint8_t>(report_.z),
      static_cast<uint8_t>(report_.rz),
      static_cast<uint8_t>(report_.rx),
      static_cast<uint8_t>(report_.ry),
      report_.hat,
      static_cast<uint8_t>(report_.buttons & 0xff),
      static_cast<uint8_t>((report_.buttons >> 8) & 0xff),
      static_cast<uint8_t>((report_.buttons >> 16) & 0xff),
      static_cast<uint8_t>((report_.buttons >> 24) & 0xff),
  };
  return device_.sendHidReport(device_.classRuntimeInstance(hidInstance_), device_.compositeHid() ? device_.classReportId(hidInstance_) : hidReportId(), data, sizeof(data), timeoutMs);
}

bool EspUsbDeviceHidGamepad::send(int8_t x,
                                  int8_t y,
                                  int8_t z,
                                  int8_t rz,
                                  int8_t rx,
                                  int8_t ry,
                                  uint8_t hat,
                                  uint32_t buttons,
                                  uint32_t timeoutMs)
{
  EspUsbDeviceGamepadReport report;
  report.x = x;
  report.y = y;
  report.z = z;
  report.rz = rz;
  report.rx = rx;
  report.ry = ry;
  report.hat = hat;
  report.buttons = buttons;
  return sendReport(report, timeoutMs);
}

bool EspUsbDeviceHidGamepad::releaseAll(uint32_t timeoutMs)
{
  return sendReport(EspUsbDeviceGamepadReport(), timeoutMs);
}

uint16_t EspUsbDeviceHidGamepad::configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize)
{
  const uint16_t mps = endpointSize < 12 ? 12 : endpointSize;
  return writeHidConfigurationDescriptor(dst,
                                         interfaceNumber,
                                         endpointNumber,
                                         mps,
                                         0,
                                         0,
                                         hidReportDescriptorLength(),
                                         false);
}

const uint8_t *EspUsbDeviceHidGamepad::hidReportDescriptor() const
{
  return GAMEPAD_REPORT_DESCRIPTOR;
}

uint16_t EspUsbDeviceHidGamepad::hidReportDescriptorLength() const
{
  return sizeof(GAMEPAD_REPORT_DESCRIPTOR);
}

EspUsbDeviceHidConsumerControl::EspUsbDeviceHidConsumerControl(EspUsbDevice &device) : EspUsbHidFunction(device)
{
}

bool EspUsbDeviceHidConsumerControl::begin()
{
  return EspUsbHidFunction::begin();
}

bool EspUsbDeviceHidConsumerControl::sendUsage(uint16_t usage, uint32_t timeoutMs)
{
  usage_ = usage;
  uint8_t report[2] = {
      static_cast<uint8_t>(usage & 0xff),
      static_cast<uint8_t>((usage >> 8) & 0xff),
  };
  return device_.sendHidReport(device_.classRuntimeInstance(hidInstance_), device_.compositeHid() ? device_.classReportId(hidInstance_) : hidReportId(), report, sizeof(report), timeoutMs);
}

bool EspUsbDeviceHidConsumerControl::press(uint16_t usage, uint32_t timeoutMs)
{
  return sendUsage(usage, timeoutMs);
}

bool EspUsbDeviceHidConsumerControl::release(uint32_t timeoutMs)
{
  return sendUsage(0, timeoutMs);
}

bool EspUsbDeviceHidConsumerControl::click(uint16_t usage, uint32_t holdMs)
{
  if (!press(usage))
  {
    return false;
  }
  delay(holdMs);
  return release();
}

uint16_t EspUsbDeviceHidConsumerControl::usage() const
{
  return usage_;
}

uint16_t EspUsbDeviceHidConsumerControl::configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize)
{
  return writeHidConfigurationDescriptor(dst,
                                         interfaceNumber,
                                         endpointNumber,
                                         endpointSize,
                                         0,
                                         0,
                                         hidReportDescriptorLength(),
                                         false);
}

const uint8_t *EspUsbDeviceHidConsumerControl::hidReportDescriptor() const
{
  return CONSUMER_CONTROL_REPORT_DESCRIPTOR;
}

uint16_t EspUsbDeviceHidConsumerControl::hidReportDescriptorLength() const
{
  return sizeof(CONSUMER_CONTROL_REPORT_DESCRIPTOR);
}

EspUsbDeviceHidSystemControl::EspUsbDeviceHidSystemControl(EspUsbDevice &device) : EspUsbHidFunction(device)
{
}

bool EspUsbDeviceHidSystemControl::begin()
{
  return EspUsbHidFunction::begin();
}

bool EspUsbDeviceHidSystemControl::sendUsage(uint8_t usage, uint32_t timeoutMs)
{
  if (usage && (usage < ESP_USB_DEVICE_SYSTEM_CONTROL_POWER_OFF || usage > ESP_USB_DEVICE_SYSTEM_CONTROL_WAKE_HOST)) return false;
  usage_ = usage;
  // Public values are standard usages, while the descriptor's array selects
  // its three entries with 1..3. Zero is the out-of-range null/release value.
  const uint8_t selector = usage ? uint8_t(usage - ESP_USB_DEVICE_SYSTEM_CONTROL_POWER_OFF + 1) : 0;
  return device_.sendHidReport(device_.classRuntimeInstance(hidInstance_), device_.compositeHid() ? device_.classReportId(hidInstance_) : hidReportId(), &selector, sizeof(selector), timeoutMs);
}

bool EspUsbDeviceHidSystemControl::press(uint8_t usage, uint32_t timeoutMs)
{
  return sendUsage(usage, timeoutMs);
}

bool EspUsbDeviceHidSystemControl::release(uint32_t timeoutMs)
{
  return sendUsage(0, timeoutMs);
}

bool EspUsbDeviceHidSystemControl::click(uint8_t usage, uint32_t holdMs)
{
  if (!press(usage))
  {
    return false;
  }
  delay(holdMs);
  return release();
}

uint8_t EspUsbDeviceHidSystemControl::usage() const
{
  return usage_;
}

uint16_t EspUsbDeviceHidSystemControl::configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize)
{
  return writeHidConfigurationDescriptor(dst,
                                         interfaceNumber,
                                         endpointNumber,
                                         endpointSize,
                                         0,
                                         0,
                                         hidReportDescriptorLength(),
                                         false);
}

const uint8_t *EspUsbDeviceHidSystemControl::hidReportDescriptor() const
{
  return SYSTEM_CONTROL_REPORT_DESCRIPTOR;
}

uint16_t EspUsbDeviceHidSystemControl::hidReportDescriptorLength() const
{
  return sizeof(SYSTEM_CONTROL_REPORT_DESCRIPTOR);
}
