#include "EspUsbDevice.h"
#include "internal/EspUsbHidDescriptor.h"
#include "internal/EspUsbTinyUsbRuntime.h"

#include <ctype.h>
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
#include "tusb.h"
#include "class/cdc/cdc_device.h"
#include "class/midi/midi_device.h"
#include "class/msc/msc_device.h"
#include "class/vendor/vendor_device.h"
#include "class/net/net_device.h"
#define ESP_USB_DEVICE_HAS_TINYUSB 1
#if __has_include("esp_mac.h")
#include "esp_mac.h"
#define ESP_USB_DEVICE_HAS_ESP_MAC 1
#else
#define ESP_USB_DEVICE_HAS_ESP_MAC 0
#endif
#else
#define ESP_USB_DEVICE_HAS_TINYUSB 0
#define ESP_USB_DEVICE_HAS_ESP_MAC 0
#endif

// esp_netif / lwIP integration for the NCM class (EspUsbDeviceNet::beginNetwork()).
// Optional: only referenced when the sketch actually calls beginNetwork().
#if ESP_USB_DEVICE_HAS_TINYUSB && __has_include("esp_netif.h")
#include "esp_netif.h"
#include "esp_netif_defaults.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#define ESP_USB_DEVICE_HAS_ESP_NETIF 1
#else
#define ESP_USB_DEVICE_HAS_ESP_NETIF 0
#endif

static constexpr uint8_t USB_DESC_DEVICE = 0x01;
static constexpr uint8_t USB_DESC_CONFIGURATION = 0x02;
static constexpr uint8_t USB_DESC_STRING = 0x03;
static constexpr uint8_t USB_DESC_INTERFACE = 0x04;
static constexpr uint8_t USB_DESC_ENDPOINT = 0x05;
static constexpr uint8_t USB_DESC_BOS = 0x0f;
static constexpr uint8_t USB_DESC_DEVICE_CAPABILITY = 0x10;
static constexpr uint8_t USB_DESC_HID = 0x21;

static constexpr uint8_t USB_CLASS_HID = 0x03;
static constexpr uint8_t USB_CLASS_VENDOR_SPECIFIC = 0xff;
static constexpr uint8_t USB_SUBCLASS_BOOT = 0x01;
static constexpr uint8_t USB_PROTOCOL_KEYBOARD = 0x01;
static constexpr uint8_t USB_PROTOCOL_MOUSE = 0x02;

static constexpr uint8_t USB_ENDPOINT_ATTR_BULK = 0x02;
static constexpr uint8_t USB_ENDPOINT_ATTR_INTERRUPT = 0x03;
static constexpr uint8_t USB_SCSI_CMD_SYNCHRONIZE_CACHE_10 = 0x35;

static EspUsbDevice *g_activeDevice = nullptr;
static EspUsbDeviceCdcSerial *g_activeCdcSerial = nullptr;
static EspUsbDeviceMidi *g_activeMidi = nullptr;
static EspUsbDeviceMsc *g_activeMsc = nullptr;
static EspUsbDeviceVendor *g_activeVendor = nullptr;
static EspUsbDeviceNet *g_activeNet = nullptr;
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

  if (dst == nullptr || endpointNumber == 0 || endpointNumber > 15 ||
      endpointSize == 0 || reportDescriptorLength == 0)
  {
    return 0;
  }

  const size_t capacity = hasOutEndpoint ? 32U : 25U;
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
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x08,       //   Logical Maximum (8)
    0x35, 0x00,       //   Physical Minimum (0)
    0x46, 0x3b, 0x01, //   Physical Maximum (315)
    0x65, 0x14,       //   Unit (Eng Rot:Angular Pos)
    0x09, 0x39,       //   Usage (Hat switch)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x02,       //   Input (Data,Var,Abs)
    0x65, 0x00,       //   Unit (None)
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

static constexpr uint8_t VENDOR_REPORT_DESCRIPTOR[] = {
    0x06, 0x00, 0xff, // Usage Page (Vendor Defined 0xff00)
    0x09, 0x01,       // Usage (1)
    0xa1, 0x01,       // Collection (Application)
    0x85, 0x06,       //   Report ID (6)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xff, 0x00, //   Logical Maximum (255)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x3f,       //   Report Count (63)
    0x09, 0x01,       //   Usage (1)
    0x81, 0x02,       //   Input (Data,Var,Abs)
    0x09, 0x01,       //   Usage (1)
    0x91, 0x02,       //   Output (Data,Var,Abs)
    0x09, 0x01,       //   Usage (1)
    0xb1, 0x02,       //   Feature (Data,Var,Abs)
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
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x03,       //   Logical Maximum (3)
    0x19, 0x00,       //   Usage Minimum (Unassigned)
    0x29, 0x03,       //   Usage Maximum (3)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x00,       //   Input (Data,Array,Abs)
    0xc0,             // End Collection
};

uint8_t tud_network_mac_address[6] = {0x02, 0x02, 0x84, 0x6a, 0x96, 0x00};
static bool g_netMacUserSet = false;
static char g_netMacString[13] = {};
static uint8_t g_webUsbUrlDescriptor[128] = {};

static constexpr uint8_t WEBUSB_VENDOR_CODE = 0x01;
static constexpr uint8_t MICROSOFT_OS_20_VENDOR_CODE = 0x02;
static constexpr uint16_t MICROSOFT_OS_20_DESCRIPTOR_INDEX = 0x0007;

#if ESP_USB_DEVICE_HAS_TINYUSB
extern "C" uint8_t const *tud_descriptor_device_cb(void)
{
  return g_activeDevice ? g_activeDevice->deviceDescriptor() : nullptr;
}

extern "C" uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
  return g_activeDevice
             ? g_activeDevice->configurationDescriptorForSpeed(
                   index, tud_speed_get() == TUSB_SPEED_HIGH)
             : nullptr;
}

extern "C" uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
  return g_activeDevice ? g_activeDevice->stringDescriptor(index, langid) : nullptr;
}

extern "C" uint8_t const *tud_descriptor_bos_cb(void)
{
  return g_activeDevice ? g_activeDevice->bosDescriptor() : nullptr;
}

extern "C" uint8_t const *tud_descriptor_device_qualifier_cb(void)
{
  return g_activeDevice ? g_activeDevice->deviceQualifierDescriptor() : nullptr;
}

extern "C" uint8_t const *tud_descriptor_other_speed_configuration_cb(uint8_t index)
{
  return g_activeDevice
             ? g_activeDevice->otherSpeedConfigurationDescriptor(
                   index, tud_speed_get() == TUSB_SPEED_HIGH)
             : nullptr;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
  return g_activeDevice ? g_activeDevice->hidReportDescriptor(instance) : nullptr;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t reportId, hid_report_type_t reportType, uint8_t *buffer, uint16_t reqlen)
{
  (void)instance;
  (void)reportId;
  (void)reportType;
  (void)buffer;
  (void)reqlen;
  return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t reportId, hid_report_type_t reportType, const uint8_t *buffer, uint16_t bufsize)
{
  if (g_activeDevice)
  {
    g_activeDevice->handleHidSetReport(instance, reportId, static_cast<uint8_t>(reportType), buffer, bufsize);
  }
}

void tud_hid_set_protocol_cb(uint8_t instance, uint8_t protocol)
{
  if (g_activeDevice)
  {
    g_activeDevice->handleHidSetProtocol(instance, protocol);
  }
}

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
  if (itf == 0 && g_activeCdcSerial)
  {
    g_activeCdcSerial->handleLineState(dtr, rts);
  }
}

void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *lineCoding)
{
  if (itf == 0 && g_activeCdcSerial && lineCoding)
  {
    g_activeCdcSerial->handleLineCoding(lineCoding->bit_rate, lineCoding->stop_bits, lineCoding->parity, lineCoding->data_bits);
  }
}

void tud_cdc_rx_cb(uint8_t itf)
{
  if (itf == 0 && g_activeCdcSerial)
  {
    g_activeCdcSerial->handleRx();
  }
}

uint8_t tud_msc_get_maxlun_cb(void)
{
  return g_activeMsc ? g_activeMsc->maxLun() : 0;
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendorId[8], uint8_t productId[16], uint8_t productRev[4])
{
  (void)lun;
  if (g_activeMsc)
  {
    g_activeMsc->inquiry(vendorId, productId, productRev);
  }
}

bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
  (void)lun;
  return g_activeMsc ? g_activeMsc->testUnitReady() : false;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *blockCount, uint16_t *blockSize)
{
  (void)lun;
  if (g_activeMsc)
  {
    g_activeMsc->capacity(blockCount, blockSize);
  }
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t powerCondition, bool start, bool loadEject)
{
  (void)lun;
  return g_activeMsc ? g_activeMsc->startStop(powerCondition, start, loadEject) : true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
  (void)lun;
  return g_activeMsc ? g_activeMsc->read10(lba, offset, buffer, bufsize) : -1;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
  (void)lun;
  return g_activeMsc ? g_activeMsc->write10(lba, offset, buffer, bufsize) : -1;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsiCmd[16], void *buffer, uint16_t bufsize)
{
  (void)buffer;
  (void)bufsize;
  if (scsiCmd[0] == SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL)
  {
    return 0;
  }
  if (scsiCmd[0] == USB_SCSI_CMD_SYNCHRONIZE_CACHE_10)
  {
    return 0;
  }
  tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
  return -1;
}

bool tud_msc_is_writable_cb(uint8_t lun)
{
  (void)lun;
  return g_activeMsc ? g_activeMsc->writable() : false;
}

// The signature MUST match the arduino-esp32 TinyUSB prototype exactly
// (class/vendor/vendor_device.h): `void tud_vendor_rx_cb(uint8_t idx,
// const uint8_t *buffer, uint32_t bufsize)`. That declaration is inside the
// header's `extern "C"` block, so defining a matching signature here gives this
// function C linkage and overrides TinyUSB's weak default. Using a mismatched
// signature (e.g. a single `uint8_t` argument) does NOT conflict at compile time
// — instead the compiler emits a *C++-mangled* symbol that silently fails to
// override the weak C default, so vendord_xfer_cb keeps calling the empty stub
// and onRx() never fires (data still reaches the RX FIFO, which is why polling
// "works" — but the callback is dead). In buffered mode (CFG_TUD_VENDOR_TXRX_BUFFERED,
// the default) buffer/bufsize are NULL/0 and the payload is in the RX FIFO, which
// handleRx() drains via available()/read().
void tud_vendor_rx_cb(uint8_t idx, const uint8_t *buffer, uint32_t bufsize)
{
  (void)idx;
  (void)buffer;
  (void)bufsize;
  if (g_activeVendor)
  {
    g_activeVendor->handleRx();
  }
}

extern "C" bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
  if (g_activeDevice && request &&
      request->bRequest == WEBUSB_VENDOR_CODE &&
      g_activeDevice->config().webusbEnabled)
  {
    if (stage != CONTROL_STAGE_SETUP)
    {
      return true;
    }
    const char *url = g_activeDevice->config().webusbUrl;
    if (!url)
    {
      return false;
    }
    uint8_t scheme = 0xff;
    if (strncmp(url, "https://", 8) == 0)
    {
      scheme = 1;
      url += 8;
    }
    else if (strncmp(url, "http://", 7) == 0)
    {
      scheme = 0;
      url += 7;
    }
    else
    {
      scheme = 1;
    }
    size_t length = strlen(url);
    if (length > sizeof(g_webUsbUrlDescriptor) - 3)
    {
      length = sizeof(g_webUsbUrlDescriptor) - 3;
    }
    g_webUsbUrlDescriptor[0] = static_cast<uint8_t>(length + 3);
    g_webUsbUrlDescriptor[1] = 0x03;
    g_webUsbUrlDescriptor[2] = scheme;
    memcpy(&g_webUsbUrlDescriptor[3], url, length);
    return tud_control_xfer(rhport, request, g_webUsbUrlDescriptor,
                            g_webUsbUrlDescriptor[0]);
  }
  if (g_activeDevice && request &&
      request->bRequest == MICROSOFT_OS_20_VENDOR_CODE &&
      request->wIndex == MICROSOFT_OS_20_DESCRIPTOR_INDEX &&
      g_activeDevice->microsoftOs20Descriptor())
  {
    if (stage != CONTROL_STAGE_SETUP)
    {
      return true;
    }
    return tud_control_xfer(
        rhport, request,
        const_cast<uint8_t *>(g_activeDevice->microsoftOs20Descriptor()),
        g_activeDevice->microsoftOs20DescriptorLength());
  }
  return g_activeVendor ? g_activeVendor->handleControlRequest(rhport, stage, request) : false;
}

// TinyUSB net (CDC-NCM) callbacks. Signatures must match class/net/net_device.h
// (included above) so they get C linkage and override TinyUSB's weak defaults —
// see the tud_vendor_rx_cb note above for why a mismatch silently fails.
bool tud_network_recv_cb(const uint8_t *src, uint16_t size)
{
  // Return true = we took ownership of this frame and TinyUSB may reuse its
  // buffer. We copy out synchronously in handleRecv, so always accept.
  if (g_activeNet)
  {
    return g_activeNet->handleRecv(src, size);
  }
  tud_network_recv_renew();
  return true;
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg)
{
  return g_activeNet ? g_activeNet->handleXmit(dst, ref, arg) : 0;
}

void tud_network_init_cb(void)
{
  if (g_activeNet)
  {
    g_activeNet->handleInit();
  }
}
#endif


EspUsbDevice::EspUsbDevice()
{
}

EspUsbDevice::~EspUsbDevice()
{
  end();
}

bool EspUsbDevice::begin()
{
  return begin(EspUsbDeviceConfig());
}

bool EspUsbDevice::begin(const EspUsbDeviceConfig &config)
{
  if (running_)
  {
    return true;
  }
  config_ = config;
  if (!buildDescriptors())
  {
    return false;
  }
  size_t begunClassCount = 0;
  for (size_t i = 0; i < classCount_; i++)
  {
    if (classes_[i] && !classes_[i]->begin())
    {
      classes_[i]->end();
      while (begunClassCount > 0)
      {
        --begunClassCount;
        classes_[begunClassCount]->end();
      }
      setLastError(ESP_FAIL);
      return false;
    }
    ++begunClassCount;
  }
  if (config_.startTinyUsb && classCount_ > 0)
  {
#if ESP_USB_DEVICE_HAS_TINYUSB
    if (g_activeDevice && g_activeDevice != this)
    {
      while (begunClassCount > 0)
      {
        --begunClassCount;
        classes_[begunClassCount]->end();
      }
      setLastError(ESP_ERR_INVALID_STATE);
      return false;
    }
    g_activeDevice = this;

    espusb::internal::TinyUsbRuntimeOptions runtimeOptions;
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

    const esp_err_t err = espusb::internal::startTinyUsbRuntime(runtimeOptions);
    if (err != ESP_OK)
    {
      g_activeDevice = nullptr;
      while (begunClassCount > 0)
      {
        --begunClassCount;
        classes_[begunClassCount]->end();
      }
      setLastError(err);
      return false;
    }
    tinyusbStarted_ = true;
    for (size_t i = 0; i < classCount_; i++)
    {
      if (classes_[i] && !classes_[i]->afterDeviceStarted())
      {
        espusb::internal::stopTinyUsbRuntime();
        tinyusbStarted_ = false;
        g_activeDevice = nullptr;
        while (begunClassCount > 0)
        {
          --begunClassCount;
          classes_[begunClassCount]->end();
        }
        setLastError(ESP_FAIL);
        return false;
      }
    }
#else
    while (begunClassCount > 0)
    {
      --begunClassCount;
      classes_[begunClassCount]->end();
    }
    setLastError(ESP_ERR_NOT_SUPPORTED);
    return false;
#endif
  }

  running_ = true;
  ready_ = tinyusbStarted_;
  setLastError(ESP_OK);
  return true;
}

void EspUsbDevice::end()
{
#if ESP_USB_DEVICE_HAS_TINYUSB
  if (tinyusbStarted_)
  {
    espusb::internal::stopTinyUsbRuntime();
    tinyusbStarted_ = false;
  }
#endif
  if (running_)
  {
    for (size_t i = classCount_; i > 0; --i)
    {
      if (classes_[i - 1])
      {
        classes_[i - 1]->end();
      }
    }
  }
  if (g_activeDevice == this)
  {
    g_activeDevice = nullptr;
  }
  running_ = false;
  ready_ = false;
}

void EspUsbDevice::task()
{
}

bool EspUsbDevice::ready() const
{
#if ESP_USB_DEVICE_HAS_TINYUSB
  if (tinyusbStarted_)
  {
    return tud_mounted();
  }
#endif
  return ready_;
}

const EspUsbDeviceConfig &EspUsbDevice::config() const
{
  return config_;
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
  case ESP_ERR_INVALID_SIZE:
    return "ESP_ERR_INVALID_SIZE";
  case ESP_ERR_NOT_SUPPORTED:
    return "ESP_ERR_NOT_SUPPORTED";
  default:
    return "ESP_ERR_UNKNOWN";
  }
}

bool EspUsbDevice::addClass(EspUsbDeviceClass *deviceClass)
{
  if (!deviceClass || running_ || classCount_ >= MAX_CLASSES)
  {
    setLastError(running_ ? ESP_ERR_INVALID_STATE : ESP_FAIL);
    return false;
  }
  classes_[classCount_] = deviceClass;
  deviceClass->hidInstance_ = static_cast<uint8_t>(classCount_);
  classCount_++;
  setLastError(ESP_OK);
  return true;
}

void EspUsbDevice::removeClass(EspUsbDeviceClass *deviceClass)
{
  for (size_t i = 0; i < classCount_; ++i)
  {
    if (classes_[i] != deviceClass)
    {
      continue;
    }
    for (size_t j = i + 1; j < classCount_; ++j)
    {
      classes_[j - 1] = classes_[j];
      if (classes_[j - 1])
      {
        classes_[j - 1]->hidInstance_ = static_cast<uint8_t>(j - 1);
      }
    }
    classes_[--classCount_] = nullptr;
    return;
  }
}

bool EspUsbDevice::sendHidReport(uint8_t instance, uint8_t reportId, const void *data, size_t length, uint32_t timeoutMs)
{
  (void)timeoutMs;
#if ESP_USB_DEVICE_HAS_TINYUSB
  if (!tinyusbStarted_)
  {
    setLastError(ESP_ERR_INVALID_STATE);
    return false;
  }
  if (!data || length == 0)
  {
    setLastError(ESP_FAIL);
    return false;
  }
  if (!tud_hid_n_ready(instance))
  {
    setLastError(ESP_ERR_INVALID_STATE);
    return false;
  }
  const bool ok = tud_hid_n_report(instance, reportId, data, length);
  setLastError(ok ? ESP_OK : ESP_FAIL);
  return ok;
#else
  (void)instance;
  (void)reportId;
  (void)data;
  (void)length;
  setLastError(ESP_ERR_NOT_SUPPORTED);
  return false;
#endif
}

const uint8_t *EspUsbDevice::deviceDescriptor()
{
  buildDescriptors();
  return deviceDescriptor_;
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
  return highSpeed ? configDescriptorHighSpeed_ : configDescriptor_;
}

const uint8_t *EspUsbDevice::deviceQualifierDescriptor()
{
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (config_.controller == EspUsbController::FullSpeed)
  {
    return nullptr;
  }
  buildDescriptors();
  return deviceQualifierDescriptor_;
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
  memcpy(otherSpeedDescriptor_, source, configDescriptorLength_);
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

uint16_t EspUsbDevice::hidInterfacesLength() const
{
  return hidInterfacesLength_;
}

uint8_t EspUsbDevice::hidInterfaceCount() const
{
  return hidInterfaceCount_;
}

const uint16_t *EspUsbDevice::stringDescriptor(uint8_t index, uint16_t langid)
{
  (void)langid;
  memset(stringDescriptor_, 0, sizeof(stringDescriptor_));
  if (index == 0)
  {
    stringDescriptor_[0] = (USB_DESC_STRING << 8) | 4;
    stringDescriptor_[1] = 0x0409;
    return stringDescriptor_;
  }

  const char *value = nullptr;
  if (index == 1)
  {
    value = config_.manufacturer;
  }
  else if (index == 2)
  {
    value = config_.product;
  }
  else if (index == 3)
  {
    value = config_.serialNumber;
  }
  else if (index == 4 && g_activeNet)
  {
    value = g_netMacString;
  }
  if (!value)
  {
    return nullptr;
  }

  size_t length = strlen(value);
  if (length > MAX_STRING_DESCRIPTOR - 1)
  {
    length = MAX_STRING_DESCRIPTOR - 1;
  }
  stringDescriptor_[0] = static_cast<uint16_t>((USB_DESC_STRING << 8) | (2 + length * 2));
  for (size_t i = 0; i < length; i++)
  {
    stringDescriptor_[1 + i] = static_cast<uint8_t>(value[i]);
  }
  return stringDescriptor_;
}

const uint8_t *EspUsbDevice::hidReportDescriptor(uint8_t instance)
{
  if (compositeHid())
  {
    return instance == 0 ? hidReportDescriptor_ : nullptr;
  }
  if (instance >= classCount_ || !classes_[instance])
  {
    return nullptr;
  }
  if (!classes_[instance]->isHid())
  {
    return nullptr;
  }
  return classes_[instance]->hidReportDescriptor();
}

void EspUsbDevice::handleHidSetReport(uint8_t instance, uint8_t reportId, uint8_t reportType, const uint8_t *data, uint16_t length)
{
  if (compositeHid())
  {
    for (size_t i = 0; i < classCount_; i++)
    {
      if (!classes_[i] || !classes_[i]->isHid())
      {
        continue;
      }
      if (classReportId(static_cast<uint8_t>(i)) == reportId && classes_[i])
      {
        classes_[i]->onHidSetReport(reportId, reportType, data, length);
        return;
      }
    }
    return;
  }
  if (instance < classCount_ && classes_[instance])
  {
    classes_[instance]->onHidSetReport(reportId, reportType, data, length);
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
      if (classes_[i])
      {
        classes_[i]->onHidSetProtocol(protocol);
      }
    }
    return;
  }
  if (instance < classCount_ && classes_[instance])
  {
    classes_[instance]->onHidSetProtocol(protocol);
  }
}

bool EspUsbDevice::buildDescriptors()
{
  vendorInterfaceNumber_ = 0xff;
  const bool composite = compositeHid();
  uint8_t interfaceCount = composite ? 1 : 0;
  if (!composite)
  {
    for (size_t i = 0; i < classCount_; i++)
    {
      if (classes_[i] && classes_[i]->isHid())
      {
        interfaceCount += classes_[i]->interfaceCount();
      }
    }
  }
  for (size_t i = 0; i < classCount_; i++)
  {
    if (classes_[i] && !classes_[i]->isHid())
    {
      interfaceCount += classes_[i]->interfaceCount();
    }
  }

  memset(deviceDescriptor_, 0, sizeof(deviceDescriptor_));
  deviceDescriptor_[0] = 18;
  deviceDescriptor_[1] = USB_DESC_DEVICE;
  put16(&deviceDescriptor_[2], config_.webusbEnabled ? 0x0201 : 0x0200);
  deviceDescriptor_[4] = 0x00;
  deviceDescriptor_[5] = 0x00;
  deviceDescriptor_[6] = 0x00;
  deviceDescriptor_[7] = 64;
  put16(&deviceDescriptor_[8], config_.vid);
  put16(&deviceDescriptor_[10], config_.pid);
  put16(&deviceDescriptor_[12], 0x0100);
  deviceDescriptor_[14] = config_.manufacturer ? 1 : 0;
  deviceDescriptor_[15] = config_.product ? 2 : 0;
  deviceDescriptor_[16] = config_.serialNumber ? 3 : 0;
  deviceDescriptor_[17] = 1;

  memset(configDescriptor_, 0, sizeof(configDescriptor_));
  memset(hidReportDescriptor_, 0, sizeof(hidReportDescriptor_));
  hidReportDescriptorLength_ = 0;
  configDescriptor_[0] = 9;
  configDescriptor_[1] = USB_DESC_CONFIGURATION;
  configDescriptor_[4] = interfaceCount;
  configDescriptor_[5] = 1;
  configDescriptor_[6] = 0;
  configDescriptor_[7] = static_cast<uint8_t>(0x80 | (config_.selfPowered ? 0x40 : 0x00));
  configDescriptor_[8] = powerToDescriptor(config_.maxPowerMilliamps);

  uint16_t offset = 9;
  uint8_t interfaceNumber = 0;
  uint8_t endpointNumber = 1;
  uint16_t endpointSize = composite ? 16 : hidEndpointSize();
  if (composite)
  {
    // Raise the shared HID endpoint if any merged class needs more room (e.g. an
    // NKRO keyboard's bitmap report). Bounded by CFG_TUD_HID_EP_BUFSIZE (64).
    for (size_t i = 0; i < classCount_; i++)
    {
      if (classes_[i] && classes_[i]->isHid())
      {
        const uint16_t hint = classes_[i]->hidInEndpointSize();
        if (hint > endpointSize)
        {
          endpointSize = hint;
        }
      }
    }
    for (size_t i = 0; i < classCount_; i++)
    {
      if (!classes_[i] || !classes_[i]->isHid())
      {
        continue;
      }
      const uint8_t *src = classes_[i]->hidReportDescriptor();
      const uint16_t srcLen = classes_[i]->hidReportDescriptorLength();
      if (!src || srcLen < 6 || hidReportDescriptorLength_ + srcLen + 2 > MAX_HID_REPORT_DESCRIPTOR)
      {
        setLastError(ESP_FAIL);
        return false;
      }
      memcpy(&hidReportDescriptor_[hidReportDescriptorLength_], src, 6);
      hidReportDescriptorLength_ += 6;
      hidReportDescriptor_[hidReportDescriptorLength_++] = 0x85;
      hidReportDescriptor_[hidReportDescriptorLength_++] = classReportId(static_cast<uint8_t>(i));
      memcpy(&hidReportDescriptor_[hidReportDescriptorLength_], src + 6, srcLen - 6);
      hidReportDescriptorLength_ += srcLen - 6;
    }

    // Single duplex endpoint on EP1 (OUT=0x01 / IN=0x81).
    const uint16_t written = writeHidConfigurationDescriptor(
        &configDescriptor_[offset],
        interfaceNumber,
        endpointNumber,
        endpointSize,
        0,
        0,
        hidReportDescriptorLength_,
        true);
    if (written == 0)
    {
      setLastError(ESP_FAIL);
      return false;
    }
    offset += written;
    interfaceNumber += 1;
    endpointNumber += 1;
  }
  else
  {
    for (size_t i = 0; i < classCount_; i++)
    {
      if (!classes_[i] || !classes_[i]->isHid())
      {
        continue;
      }
      uint16_t written = classes_[i]->configurationDescriptor(&configDescriptor_[offset], interfaceNumber, endpointNumber, endpointSize);
      offset += written;
      interfaceNumber += classes_[i]->interfaceCount();
      endpointNumber += classes_[i]->endpointCount();
      if (offset > MAX_CONFIG_DESCRIPTOR)
      {
        setLastError(ESP_FAIL);
        return false;
      }
    }
  }
  // Everything written so far is HID; keep the extent for descriptor tests and
  // report-instance bookkeeping.
  hidInterfacesLength_ = static_cast<uint16_t>(offset - 9);
  hidInterfaceCount_ = interfaceNumber;
  EspUsbDeviceClass *audioClass = nullptr;
  uint16_t audioOffset = 0;
  uint16_t audioLength = 0;
  uint8_t audioInterfaceNumber = 0;
  uint8_t audioEndpointNumber = 0;
  for (size_t i = 0; i < classCount_; i++)
  {
    if (!classes_[i] || classes_[i]->isHid())
    {
      continue;
    }
    const uint16_t classOffset = offset;
    const uint8_t classInterface = interfaceNumber;
    const uint8_t classEndpoint = endpointNumber;
    uint16_t written = classes_[i]->configurationDescriptorForSpeed(
        &configDescriptor_[offset], MAX_CONFIG_DESCRIPTOR - offset,
        interfaceNumber, endpointNumber, false);
    if (written == 0)
    {
      setLastError(ESP_FAIL);
      return false;
    }
    offset += written;
    if (classes_[i]->isAudio())
    {
      audioClass = classes_[i];
      audioOffset = classOffset;
      audioLength = written;
      audioInterfaceNumber = classInterface;
      audioEndpointNumber = classEndpoint;
    }
    if (classes_[i]->isVendor())
    {
      vendorInterfaceNumber_ = classInterface;
    }
    interfaceNumber += classes_[i]->interfaceCount();
    endpointNumber += classes_[i]->endpointCount();
    if (offset > MAX_CONFIG_DESCRIPTOR)
    {
      setLastError(ESP_FAIL);
      return false;
    }
  }
  configDescriptorLength_ = offset;
  put16(&configDescriptor_[2], configDescriptorLength_);
  if (!validateControllerEndpoints(configDescriptor_,
                                   configDescriptorLength_))
  {
    setLastError(ESP_ERR_INVALID_SIZE);
    return false;
  }

  memcpy(configDescriptorHighSpeed_, configDescriptor_, configDescriptorLength_);
  for (uint16_t descriptorOffset = 0;
       descriptorOffset + 1 < configDescriptorLength_;)
  {
    const uint8_t descriptorLength =
        configDescriptorHighSpeed_[descriptorOffset];
    if (descriptorLength < 2 ||
        descriptorOffset + descriptorLength > configDescriptorLength_)
    {
      setLastError(ESP_FAIL);
      return false;
    }
    if (configDescriptorHighSpeed_[descriptorOffset + 1] == USB_DESC_ENDPOINT &&
        descriptorLength >= 7 &&
        (configDescriptorHighSpeed_[descriptorOffset + 3] & 0x03) ==
            USB_ENDPOINT_ATTR_BULK)
    {
      put16(&configDescriptorHighSpeed_[descriptorOffset + 4], 512);
    }
    descriptorOffset = static_cast<uint16_t>(descriptorOffset + descriptorLength);
  }
  if (audioClass)
  {
    const uint16_t highSpeedAudioLength =
        audioClass->configurationDescriptorForSpeed(
            &configDescriptorHighSpeed_[audioOffset],
            MAX_CONFIG_DESCRIPTOR - audioOffset, audioInterfaceNumber,
            audioEndpointNumber, true);
    if (highSpeedAudioLength != audioLength)
    {
      setLastError(ESP_FAIL);
      return false;
    }
  }

  memset(deviceQualifierDescriptor_, 0, sizeof(deviceQualifierDescriptor_));
  deviceQualifierDescriptor_[0] = sizeof(deviceQualifierDescriptor_);
  deviceQualifierDescriptor_[1] = 0x06;
  put16(&deviceQualifierDescriptor_[2], config_.webusbEnabled ? 0x0201 : 0x0200);
  deviceQualifierDescriptor_[4] = deviceDescriptor_[4];
  deviceQualifierDescriptor_[5] = deviceDescriptor_[5];
  deviceQualifierDescriptor_[6] = deviceDescriptor_[6];
  deviceQualifierDescriptor_[7] = deviceDescriptor_[7];
  deviceQualifierDescriptor_[8] = 1;
  buildWebUsbDescriptors();
  setLastError(ESP_OK);
  return true;
}

void EspUsbDevice::buildWebUsbDescriptors()
{
  memset(bosDescriptor_, 0, sizeof(bosDescriptor_));
  memset(microsoftOs20Descriptor_, 0, sizeof(microsoftOs20Descriptor_));
  bosDescriptorLength_ = 0;
  microsoftOs20DescriptorLength_ = 0;
  if (!config_.webusbEnabled)
  {
    return;
  }

  static constexpr uint8_t webUsbUuid[] = {
      0x38, 0xb6, 0x08, 0x34, 0xa9, 0x09, 0xa0, 0x47,
      0x8b, 0xfd, 0xa0, 0x76, 0x88, 0x15, 0xb6, 0x65,
  };
  static constexpr uint8_t microsoftOs20Uuid[] = {
      0xdf, 0x60, 0xdd, 0xd8, 0x89, 0x45, 0xc7, 0x4c,
      0x9c, 0xd2, 0x65, 0x9d, 0x9e, 0x64, 0x8a, 0x9f,
  };

  size_t bosOffset = 5;
  bosDescriptor_[bosOffset++] = 24;
  bosDescriptor_[bosOffset++] = USB_DESC_DEVICE_CAPABILITY;
  bosDescriptor_[bosOffset++] = 0x05;
  bosDescriptor_[bosOffset++] = 0x00;
  memcpy(&bosDescriptor_[bosOffset], webUsbUuid, sizeof(webUsbUuid));
  bosOffset += sizeof(webUsbUuid);
  put16(&bosDescriptor_[bosOffset], 0x0100);
  bosOffset += 2;
  bosDescriptor_[bosOffset++] = WEBUSB_VENDOR_CODE;
  bosDescriptor_[bosOffset++] = 1;

  if (vendorInterfaceNumber_ != 0xff)
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
    put16(&bosDescriptor_[bosOffset], MS_OS_20_DESCRIPTOR_SIZE);
    bosOffset += 2;
    bosDescriptor_[bosOffset++] = MICROSOFT_OS_20_VENDOR_CODE;
    bosDescriptor_[bosOffset++] = 0;

    size_t offset = 0;
    put16(&microsoftOs20Descriptor_[offset], 10);
    offset += 2;
    put16(&microsoftOs20Descriptor_[offset], 0);
    offset += 2;
    put32(&microsoftOs20Descriptor_[offset], 0x06030000);
    offset += 4;
    put16(&microsoftOs20Descriptor_[offset], MS_OS_20_DESCRIPTOR_SIZE);
    offset += 2;

    put16(&microsoftOs20Descriptor_[offset], 8);
    offset += 2;
    put16(&microsoftOs20Descriptor_[offset], 1);
    offset += 2;
    microsoftOs20Descriptor_[offset++] = 0;
    microsoftOs20Descriptor_[offset++] = 0;
    put16(&microsoftOs20Descriptor_[offset], MS_OS_20_DESCRIPTOR_SIZE - 10);
    offset += 2;

    put16(&microsoftOs20Descriptor_[offset], 8);
    offset += 2;
    put16(&microsoftOs20Descriptor_[offset], 2);
    offset += 2;
    microsoftOs20Descriptor_[offset++] = vendorInterfaceNumber_;
    microsoftOs20Descriptor_[offset++] = 0;
    put16(&microsoftOs20Descriptor_[offset],
          MS_OS_20_DESCRIPTOR_SIZE - 10 - 8);
    offset += 2;

    put16(&microsoftOs20Descriptor_[offset], 20);
    offset += 2;
    put16(&microsoftOs20Descriptor_[offset], 3);
    offset += 2;
    static constexpr uint8_t winUsbId[16] = {
        'W', 'I', 'N', 'U', 'S', 'B', 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
    };
    memcpy(&microsoftOs20Descriptor_[offset], winUsbId, sizeof(winUsbId));
    offset += sizeof(winUsbId);

    put16(&microsoftOs20Descriptor_[offset], 132);
    offset += 2;
    put16(&microsoftOs20Descriptor_[offset], 4);
    offset += 2;
    put16(&microsoftOs20Descriptor_[offset], 7);
    offset += 2;
    put16(&microsoftOs20Descriptor_[offset], 42);
    offset += 2;
    offset += putUtf16Le(&microsoftOs20Descriptor_[offset],
                         "DeviceInterfaceGUIDs", false);
    put16(&microsoftOs20Descriptor_[offset], 80);
    offset += 2;
    offset += putUtf16Le(
        &microsoftOs20Descriptor_[offset],
        "{975F44D9-0D08-43FD-8B3E-127CA8AFFF9D}", true);
    if (offset == sizeof(microsoftOs20Descriptor_))
    {
      microsoftOs20DescriptorLength_ = static_cast<uint16_t>(offset);
    }
    else
    {
      // Keep WebUSB usable without advertising a malformed Microsoft
      // capability if this fixed descriptor is edited inconsistently.
      memset(microsoftOs20Descriptor_, 0,
             sizeof(microsoftOs20Descriptor_));
      bosOffset = 29;
    }
  }

  bosDescriptor_[0] = 5;
  bosDescriptor_[1] = USB_DESC_BOS;
  put16(&bosDescriptor_[2], static_cast<uint16_t>(bosOffset));
  bosDescriptor_[4] = microsoftOs20DescriptorLength_ ? 2 : 1;
  bosDescriptorLength_ = static_cast<uint16_t>(bosOffset);
}

bool EspUsbDevice::validateControllerEndpoints(const uint8_t *descriptor,
                                               uint16_t length)
{
  if (!descriptor || length < 9)
  {
    return false;
  }
#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
  constexpr uint8_t maxEndpointNumber = 5;
  constexpr uint8_t maxNonControlInEndpoints = 4;
  constexpr uint8_t maxNonControlOutEndpoints = 5;
#elif defined(CONFIG_IDF_TARGET_ESP32P4)
  // P4 rhport 0 is the FS controller (7 endpoint numbers total, 5 IN
  // endpoints including EP0). rhport 1 is the HS controller (16 endpoint
  // numbers total, 8 IN endpoints including EP0). Auto selects HS on P4.
  const bool fullSpeed = config_.controller == EspUsbController::FullSpeed;
  const uint8_t maxEndpointNumber = fullSpeed ? 6 : 15;
  const uint8_t maxNonControlInEndpoints = fullSpeed ? 4 : 7;
  const uint8_t maxNonControlOutEndpoints = fullSpeed ? 6 : 15;
#else
  // Unknown future targets keep USB's protocol maximum until their controller
  // capabilities are represented here.
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
    if (classes_[i] && classes_[i]->isHid())
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
    if (classes_[i] && classes_[i]->isHid())
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
    if (classes_[i] && classes_[i]->isCdc())
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
    if (classes_[i] && classes_[i]->isMidi())
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
    if (classes_[i] && classes_[i]->isMsc())
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
    if (classes_[i] && classes_[i]->isVendor())
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
    if (classes_[i] && classes_[i]->isAudio())
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
    if (classes_[i] && classes_[i]->isNet())
    {
      return true;
    }
  }
  return false;
}

uint8_t EspUsbDevice::classReportId(uint8_t classInstance) const
{
  if (!compositeHid())
  {
    return 0;
  }
  if (classInstance < classCount_ && classes_[classInstance])
  {
    const uint8_t reportId = classes_[classInstance]->hidReportId();
    if (reportId)
    {
      return reportId;
    }
  }
  return ESP_USB_DEVICE_HID_REPORT_ID_KEYBOARD;
}

uint8_t EspUsbDevice::classRuntimeInstance(uint8_t classInstance) const
{
  return compositeHid() ? 0 : classInstance;
}

void EspUsbDevice::setLastError(esp_err_t error)
{
  lastError_ = error;
}

EspUsbDeviceClass::EspUsbDeviceClass(EspUsbDevice &device) : device_(device)
{
  device_.addClass(this);
}

EspUsbDeviceClass::~EspUsbDeviceClass()
{
  device_.removeClass(this);
}

EspUsbDeviceCdcSerial::EspUsbDeviceCdcSerial(EspUsbDevice &device) : EspUsbDeviceClass(device)
{
}

EspUsbDeviceCdcSerial::~EspUsbDeviceCdcSerial()
{
  end();
}

bool EspUsbDeviceCdcSerial::begin()
{
  if (g_activeCdcSerial && g_activeCdcSerial != this)
  {
    return false;
  }
  g_activeCdcSerial = this;
  return true;
}

bool EspUsbDeviceCdcSerial::afterDeviceStarted()
{
  return true;
}

void EspUsbDeviceCdcSerial::end()
{
  if (g_activeCdcSerial == this)
  {
    g_activeCdcSerial = nullptr;
  }
}

uint16_t EspUsbDeviceCdcSerial::configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize)
{
  if (!dst || endpointNumber == 0 || endpointNumber > 14)
  {
    return 0;
  }
  const uint8_t epNotification = static_cast<uint8_t>(0x80 | endpointNumber);
  const uint8_t epOut = static_cast<uint8_t>(endpointNumber + 1);
  const uint8_t epIn = static_cast<uint8_t>(0x80 | (endpointNumber + 1));
  const uint8_t descriptor[] = {
      TUD_CDC_DESCRIPTOR(interfaceNumber, 0, epNotification, 8,
                         epOut, epIn, endpointSize),
  };
  memcpy(dst, descriptor, sizeof(descriptor));
  return sizeof(descriptor);
}

int EspUsbDeviceCdcSerial::available()
{
#if ESP_USB_DEVICE_HAS_TINYUSB
  return static_cast<int>(tud_cdc_n_available(0));
#else
  return 0;
#endif
}

int EspUsbDeviceCdcSerial::read()
{
  uint8_t data = 0;
  return read(&data, 1) == 1 ? data : -1;
}

size_t EspUsbDeviceCdcSerial::read(uint8_t *buffer, size_t size)
{
  if (!buffer || size == 0)
  {
    return 0;
  }
#if ESP_USB_DEVICE_HAS_TINYUSB
  return tud_cdc_n_read(0, buffer, static_cast<uint32_t>(size));
#else
  return 0;
#endif
}

size_t EspUsbDeviceCdcSerial::write(uint8_t data)
{
  return write(&data, 1);
}

size_t EspUsbDeviceCdcSerial::write(const uint8_t *buffer, size_t size)
{
  if (!buffer || size == 0)
  {
    return 0;
  }
#if ESP_USB_DEVICE_HAS_TINYUSB
  if (!tud_cdc_n_ready(0))
  {
    return 0;
  }
  const uint32_t written = tud_cdc_n_write(0, buffer, static_cast<uint32_t>(size));
  tud_cdc_n_write_flush(0);
  return written;
#else
  return 0;
#endif
}

void EspUsbDeviceCdcSerial::flush()
{
#if ESP_USB_DEVICE_HAS_TINYUSB
  tud_cdc_n_write_flush(0);
#endif
}

bool EspUsbDeviceCdcSerial::connected() const
{
#if ESP_USB_DEVICE_HAS_TINYUSB
  return tud_cdc_n_connected(0);
#else
  return false;
#endif
}

const EspUsbDeviceCdcLineCoding &EspUsbDeviceCdcSerial::lineCoding() const
{
  return lineCoding_;
}

const EspUsbDeviceCdcLineState &EspUsbDeviceCdcSerial::lineState() const
{
  return lineState_;
}

void EspUsbDeviceCdcSerial::onLineCoding(LineCodingCallback callback)
{
  lineCodingCallback_ = callback;
}

void EspUsbDeviceCdcSerial::onLineState(LineStateCallback callback)
{
  lineStateCallback_ = callback;
}

void EspUsbDeviceCdcSerial::onRx(RxCallback callback)
{
  rxCallback_ = callback;
}

void EspUsbDeviceCdcSerial::handleLineCoding(uint32_t baud, uint8_t stopBits, uint8_t parity, uint8_t dataBits)
{
  lineCoding_.baud = baud;
  lineCoding_.stopBits = stopBits;
  lineCoding_.parity = parity;
  lineCoding_.dataBits = dataBits;
  if (lineCodingCallback_)
  {
    lineCodingCallback_(lineCoding_);
  }
}

void EspUsbDeviceCdcSerial::handleLineState(bool dtr, bool rts)
{
  lineState_.dtr = dtr;
  lineState_.rts = rts;
  if (lineStateCallback_)
  {
    lineStateCallback_(lineState_);
  }
}

void EspUsbDeviceCdcSerial::handleRx()
{
  if (rxCallback_)
  {
    rxCallback_(available());
  }
}

EspUsbDeviceVendor::EspUsbDeviceVendor(EspUsbDevice &device, uint16_t endpointSize) : EspUsbDeviceClass(device)
{
  endpointSize_ = endpointSize == 0 ? 64 : endpointSize;
}

EspUsbDeviceVendor::~EspUsbDeviceVendor()
{
  end();
}

void EspUsbDeviceVendor::end()
{
  if (g_activeVendor == this)
  {
    g_activeVendor = nullptr;
  }
}

bool EspUsbDeviceVendor::begin()
{
  if (g_activeVendor && g_activeVendor != this)
  {
    return false;
  }
  g_activeVendor = this;
  return true;
}

uint16_t EspUsbDeviceVendor::configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize)
{
  (void)endpointSize;
  const uint8_t epOut = endpointNumber;
  const uint8_t epIn = static_cast<uint8_t>(0x80 | endpointNumber);
  uint8_t descriptor[] = {
      9, USB_DESC_INTERFACE, interfaceNumber, 0, 2, USB_CLASS_VENDOR_SPECIFIC, 0x00, 0x00, 0,
      7, USB_DESC_ENDPOINT, epOut, USB_ENDPOINT_ATTR_BULK, static_cast<uint8_t>(endpointSize_ & 0xff), static_cast<uint8_t>((endpointSize_ >> 8) & 0xff), 0,
      7, USB_DESC_ENDPOINT, epIn, USB_ENDPOINT_ATTR_BULK, static_cast<uint8_t>(endpointSize_ & 0xff), static_cast<uint8_t>((endpointSize_ >> 8) & 0xff), 0,
  };
  memcpy(dst, descriptor, sizeof(descriptor));
  return sizeof(descriptor);
}

bool EspUsbDeviceVendor::mounted() const
{
#if ESP_USB_DEVICE_HAS_TINYUSB
  return tud_vendor_n_mounted(0);
#else
  return false;
#endif
}

int EspUsbDeviceVendor::available()
{
#if ESP_USB_DEVICE_HAS_TINYUSB
  return static_cast<int>(tud_vendor_n_available(0));
#else
  return 0;
#endif
}

int EspUsbDeviceVendor::read()
{
  uint8_t data = 0;
  return read(&data, 1) == 1 ? data : -1;
}

size_t EspUsbDeviceVendor::read(uint8_t *buffer, size_t size)
{
  if (!buffer || size == 0)
  {
    return 0;
  }
#if ESP_USB_DEVICE_HAS_TINYUSB
  return tud_vendor_n_read(0, buffer, static_cast<uint32_t>(size));
#else
  return 0;
#endif
}

size_t EspUsbDeviceVendor::write(uint8_t data)
{
  return write(&data, 1);
}

size_t EspUsbDeviceVendor::write(const uint8_t *buffer, size_t size)
{
  if (!buffer || size == 0)
  {
    return 0;
  }
#if ESP_USB_DEVICE_HAS_TINYUSB
  if (!mounted())
  {
    return 0;
  }
  const uint32_t available = tud_vendor_n_write_available(0);
  if (size > available)
  {
    size = available;
  }
  return size ? tud_vendor_n_write(0, buffer, static_cast<uint32_t>(size)) : 0;
#else
  return 0;
#endif
}

void EspUsbDeviceVendor::flush()
{
#if ESP_USB_DEVICE_HAS_TINYUSB
  tud_vendor_n_write_flush(0);
#endif
}

void EspUsbDeviceVendor::onRx(RxCallback callback)
{
  rxCallback_ = callback;
}

void EspUsbDeviceVendor::onControlRequest(ControlRequestCallback callback)
{
  controlRequestCallback_ = callback;
}

bool EspUsbDeviceVendor::sendControlResponse(const EspUsbDeviceVendorControlRequest &request, const void *data, size_t length)
{
#if ESP_USB_DEVICE_HAS_TINYUSB
  const tusb_control_request_t *raw = static_cast<const tusb_control_request_t *>(request.rawRequest);
  if (!raw)
  {
    return false;
  }
  if (!data || length == 0)
  {
    return tud_control_status(request.rhport, raw);
  }
  return tud_control_xfer(request.rhport, raw, const_cast<void *>(data), static_cast<uint16_t>(length));
#else
  (void)request;
  (void)data;
  (void)length;
  return false;
#endif
}

uint16_t EspUsbDeviceVendor::endpointSize() const
{
  return endpointSize_;
}

void EspUsbDeviceVendor::handleRx()
{
  if (rxCallback_)
  {
    rxCallback_(available());
  }
}

bool EspUsbDeviceVendor::handleControlRequest(uint8_t rhport, uint8_t stage, const void *request)
{
#if ESP_USB_DEVICE_HAS_TINYUSB
  const tusb_control_request_t *raw = static_cast<const tusb_control_request_t *>(request);
  if (!raw || !controlRequestCallback_)
  {
    return false;
  }
  EspUsbDeviceVendorControlRequest event;
  event.rhport = rhport;
  event.stage = stage;
  event.bmRequestType = raw->bmRequestType;
  event.bRequest = raw->bRequest;
  event.wValue = raw->wValue;
  event.wIndex = raw->wIndex;
  event.wLength = raw->wLength;
  event.rawRequest = raw;
  return controlRequestCallback_(event);
#else
  (void)rhport;
  (void)stage;
  (void)request;
  return false;
#endif
}

#if ESP_USB_DEVICE_HAS_TINYUSB
// Shared TX path used by both the sendFrame() raw API and the esp_netif transmit
// callback. `tud_network_xmit` only *records* the ref/len; the actual copy runs
// later in the usbd task via handleXmit() (tud_network_xmit_cb). So we:
//  (1) serialize with a mutex (TinyUSB net is not reentrant),
//  (2) copy into a persistent internal buffer that stays valid until handleXmit
//      runs — the caller's buffer (a stack buffer, or an lwIP pbuf freed the
//      moment we return / time out) must NOT be handed to tud_network_xmit, and
//  (3) wait (bounded) for handleXmit to signal completion.
static SemaphoreHandle_t g_netTxMutex = nullptr;
static SemaphoreHandle_t g_netTxDone = nullptr;
static uint8_t g_netTxBuf[1600];

static bool espUsbDeviceNetTxFrame(const uint8_t *data, uint16_t len)
{
  if (!data || len == 0 || len > sizeof(g_netTxBuf))
  {
    return false;
  }
  if (!g_netTxMutex)
  {
    g_netTxMutex = xSemaphoreCreateMutex();
  }
  if (!g_netTxDone)
  {
    g_netTxDone = xSemaphoreCreateBinary();
  }
  if (!g_netTxMutex || !g_netTxDone)
  {
    return false;
  }
  xSemaphoreTake(g_netTxMutex, portMAX_DELAY);
  bool ok = false;
  // Wait (bounded) for a free transmit slot. can_xmit stays false while a prior
  // (possibly timed-out) xmit is still pending, so g_netTxBuf is not overwritten
  // until that xmit's handleXmit has copied it out — no corruption, no UAF.
  for (int i = 0; i < 100 && !tud_network_can_xmit(len); i++)
  {
    vTaskDelay(1);
  }
  if (tud_network_can_xmit(len))
  {
    memcpy(g_netTxBuf, data, len);
    xSemaphoreTake(g_netTxDone, 0); // drop any stale completion
    tud_network_xmit(g_netTxBuf, len);
    // handleXmit() (usbd task) copies g_netTxBuf out, then signals g_netTxDone.
    ok = (xSemaphoreTake(g_netTxDone, pdMS_TO_TICKS(100)) == pdTRUE);
  }
  xSemaphoreGive(g_netTxMutex);
  return ok;
}
#endif

#if ESP_USB_DEVICE_HAS_ESP_NETIF
static esp_netif_driver_base_t g_netDriverBase = {};
static esp_netif_ip_info_t g_netIpInfo = {};

static esp_err_t espUsbDeviceNetPostAttach(esp_netif_t *netif, esp_netif_iodriver_handle h)
{
  esp_netif_driver_base_t *base = static_cast<esp_netif_driver_base_t *>(h);
  base->netif = netif;
  return ESP_OK;
}

static void espUsbDeviceNetFreeRx(void *h, void *buffer)
{
  (void)h;
  free(buffer);
}

// esp_netif calls this from the tcpip task to send a frame to the USB host.
static esp_err_t espUsbDeviceNetTransmit(void *h, void *buffer, size_t len)
{
  (void)h;
  if (!g_activeNet || len == 0 || len > 0xffff)
  {
    return ESP_ERR_INVALID_STATE;
  }
  return espUsbDeviceNetTxFrame(static_cast<const uint8_t *>(buffer), static_cast<uint16_t>(len)) ? ESP_OK : ESP_FAIL;
}
#endif

EspUsbDeviceNet::EspUsbDeviceNet(EspUsbDevice &device) : EspUsbDeviceClass(device)
{
}

EspUsbDeviceNet::~EspUsbDeviceNet()
{
  end();
#if ESP_USB_DEVICE_HAS_ESP_NETIF
  if (netif_)
  {
    esp_netif_destroy(static_cast<esp_netif_t *>(netif_));
    netif_ = nullptr;
    g_netDriverBase.netif = nullptr;
  }
#endif
  netStarted_ = false;
}

bool EspUsbDeviceNet::begin()
{
  if (g_activeNet && g_activeNet != this)
  {
    return false;
  }
  g_activeNet = this;
  return true;
}

bool EspUsbDeviceNet::afterDeviceStarted()
{
#if ESP_USB_DEVICE_HAS_TINYUSB
  // Bring the network link up so the host starts exchanging frames.
  tud_network_link_state(0, true);
#endif
  return true;
}

void EspUsbDeviceNet::end()
{
#if ESP_USB_DEVICE_HAS_TINYUSB
  if (g_activeNet == this)
  {
    tud_network_link_state(0, false);
  }
#endif
  if (g_activeNet == this)
  {
    g_activeNet = nullptr;
  }
}

uint16_t EspUsbDeviceNet::configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize)
{
  if (!dst || endpointNumber == 0 || endpointNumber > 14)
  {
    return 0;
  }
#if ESP_USB_DEVICE_HAS_ESP_MAC
  if (!g_netMacUserSet)
  {
    esp_read_mac(tud_network_mac_address, ESP_MAC_ETH);
  }
#endif
  static const char hex[] = "0123456789ABCDEF";
  for (size_t i = 0; i < 6; ++i)
  {
    g_netMacString[i * 2] = hex[tud_network_mac_address[i] >> 4];
    g_netMacString[i * 2 + 1] = hex[tud_network_mac_address[i] & 0x0f];
  }
  g_netMacString[12] = '\0';

  const uint8_t epNotification = static_cast<uint8_t>(0x80 | endpointNumber);
  const uint8_t epOut = static_cast<uint8_t>(endpointNumber + 1);
  const uint8_t epIn = static_cast<uint8_t>(0x80 | (endpointNumber + 1));
  const uint8_t descriptor[] = {
      TUD_CDC_NCM_DESCRIPTOR(interfaceNumber, 0, 4,
                             epNotification, 64, epOut, epIn, endpointSize,
                             1514, 16, 0),
  };
  memcpy(dst, descriptor, sizeof(descriptor));
  return sizeof(descriptor);
}

void EspUsbDeviceNet::macAddress(const uint8_t mac[6])
{
#if ESP_USB_DEVICE_HAS_TINYUSB
  memcpy(tud_network_mac_address, mac, 6);
  g_netMacUserSet = true;
#else
  (void)mac;
#endif
}

const uint8_t *EspUsbDeviceNet::macAddress() const
{
#if ESP_USB_DEVICE_HAS_TINYUSB
  return tud_network_mac_address;
#else
  return nullptr;
#endif
}

void EspUsbDeviceNet::onFrame(FrameCallback callback)
{
  frameCallback_ = callback;
}

bool EspUsbDeviceNet::linkUp() const
{
#if ESP_USB_DEVICE_HAS_TINYUSB
  // Reflect the actual USB attach/detach state rather than latching true after
  // the first configuration.
  return tud_mounted();
#else
  return false;
#endif
}

bool EspUsbDeviceNet::sendFrame(const uint8_t *data, size_t length)
{
#if ESP_USB_DEVICE_HAS_TINYUSB
  if (length == 0 || length > 0xffff)
  {
    return false;
  }
  // Copies `data` into an internal buffer and serializes with the same mutex as
  // the esp_netif TX path, so the caller may pass a stack/temporary buffer and
  // this is safe to mix with beginNetwork(). tud_network_xmit copies the frame
  // out asynchronously in the usbd task (see espUsbDeviceNetTxFrame), so passing
  // the caller's buffer directly would be a use-after-free.
  return espUsbDeviceNetTxFrame(data, static_cast<uint16_t>(length));
#else
  (void)data;
  (void)length;
  return false;
#endif
}

bool EspUsbDeviceNet::handleRecv(const uint8_t *src, uint16_t size)
{
#if ESP_USB_DEVICE_HAS_TINYUSB
#if ESP_USB_DEVICE_HAS_ESP_NETIF
  if (netStarted_ && netif_ && src && size)
  {
    // Copy out of TinyUSB's buffer into one esp_netif owns; it frees it via
    // espUsbDeviceNetFreeRx once lwIP is done, so we can renew immediately.
    uint8_t *buf = static_cast<uint8_t *>(malloc(size));
    if (buf)
    {
      memcpy(buf, src, size);
      if (esp_netif_receive(static_cast<esp_netif_t *>(netif_), buf, size, buf) != ESP_OK)
      {
        free(buf);
      }
    }
  }
  else
#endif
      if (frameCallback_ && src && size)
  {
    frameCallback_(src, size);
  }
  // We consumed the frame synchronously; allow TinyUSB to receive the next one.
  tud_network_recv_renew();
  return true;
#else
  (void)src;
  (void)size;
  return true;
#endif
}

uint16_t EspUsbDeviceNet::handleXmit(uint8_t *dst, void *ref, uint16_t arg)
{
  if (!dst || !ref || arg == 0)
  {
    return 0;
  }
  memcpy(dst, ref, arg);
#if ESP_USB_DEVICE_HAS_TINYUSB
  // Unblock espUsbDeviceNetTxFrame(): the frame has been copied out.
  if (g_netTxDone)
  {
    xSemaphoreGive(g_netTxDone);
  }
#endif
  return arg;
}

void EspUsbDeviceNet::handleInit()
{
  // tud_network init hook; link state is derived from tud_mounted() in linkUp().
}

void EspUsbDeviceNet::ipConfig(IPAddress local, IPAddress gateway, IPAddress subnet)
{
  cfgIp_ = static_cast<uint32_t>(local);
  cfgGateway_ = static_cast<uint32_t>(gateway);
  cfgNetmask_ = static_cast<uint32_t>(subnet);
}

void EspUsbDeviceNet::dhcpServer(bool enable)
{
  dhcpServer_ = enable;
  if (enable)
  {
    dhcpClient_ = false;
  }
}

void EspUsbDeviceNet::dhcpClient(bool enable)
{
  dhcpClient_ = enable;
  if (enable)
  {
    dhcpServer_ = false;
  }
}

void EspUsbDeviceNet::dhcpAdvertiseGateway(bool enable)
{
  dhcpAdvertiseGateway_ = enable;
}

void EspUsbDeviceNet::dhcpDns(IPAddress dns)
{
  dhcpDns_ = static_cast<uint32_t>(dns);
}

void EspUsbDeviceNet::defaultRoute(bool enable)
{
  defaultRoute_ = enable;
}

bool EspUsbDeviceNet::networkUp() const
{
  return netStarted_;
}

IPAddress EspUsbDeviceNet::localIP() const
{
#if ESP_USB_DEVICE_HAS_ESP_NETIF
  if (netif_)
  {
    esp_netif_ip_info_t info = {};
    if (esp_netif_get_ip_info(static_cast<esp_netif_t *>(netif_), &info) == ESP_OK)
    {
      return IPAddress(info.ip.addr);
    }
  }
#endif
  return IPAddress(cfgIp_);
}

bool EspUsbDeviceNet::beginNetwork()
{
#if ESP_USB_DEVICE_HAS_ESP_NETIF
  if (netStarted_)
  {
    return true;
  }

  // Ensure the TCP/IP stack and default event loop exist. Both are idempotent —
  // ESP_ERR_INVALID_STATE just means something already initialized them.
  esp_netif_init();
  esp_err_t evt = esp_event_loop_create_default();
  if (evt != ESP_OK && evt != ESP_ERR_INVALID_STATE)
  {
    return false;
  }

  if (!g_netTxMutex)
  {
    g_netTxMutex = xSemaphoreCreateMutex();
  }
  if (!g_netTxDone)
  {
    g_netTxDone = xSemaphoreCreateBinary();
  }
  if (!g_netTxMutex || !g_netTxDone)
  {
    return false;
  }

  // Defaults: 192.168.7.1 / gw 192.168.7.1 / mask 255.255.255.0 (network order).
  const uint32_t defIp = static_cast<uint32_t>(IPAddress(192, 168, 7, 1));
  const uint32_t defMask = static_cast<uint32_t>(IPAddress(255, 255, 255, 0));
  g_netIpInfo.ip.addr = cfgIp_ ? cfgIp_ : defIp;
  g_netIpInfo.gw.addr = cfgGateway_ ? cfgGateway_ : g_netIpInfo.ip.addr;
  g_netIpInfo.netmask.addr = cfgNetmask_ ? cfgNetmask_ : defMask;

  // Build a custom inherent config (avoids depending on esp_eth's event symbols
  // that ESP_NETIF_INHERENT_DEFAULT_ETH would pull in). Our frames are standard
  // Ethernet, so we reuse the ETH lwIP netstack glue.
  esp_netif_inherent_config_t base = {};
  uint32_t flags = ESP_NETIF_FLAG_AUTOUP;
  if (dhcpServer_)
  {
    flags |= ESP_NETIF_DHCP_SERVER;
  }
  else if (dhcpClient_)
  {
    flags |= ESP_NETIF_DHCP_CLIENT;
  }
  base.flags = static_cast<esp_netif_flags_t>(flags);
  base.ip_info = &g_netIpInfo;
  base.if_key = "USB_NCM";
  base.if_desc = "usbncm";
  // Low priority by default so a coexisting Wi-Fi STA (100) stays the ESP's
  // default netif; raise above it only when the USB host is meant to be our
  // uplink (defaultRoute(true)). esp_netif auto-selects the default by priority.
  base.route_prio = defaultRoute_ ? 255 : 10;

  esp_netif_driver_ifconfig_t driver = {};
  driver.handle = &g_netDriverBase;
  driver.transmit = espUsbDeviceNetTransmit;
  driver.driver_free_rx_buffer = espUsbDeviceNetFreeRx;

  esp_netif_config_t cfg = {};
  cfg.base = &base;
  cfg.driver = &driver;
  cfg.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;

  esp_netif_t *netif = esp_netif_new(&cfg);
  if (!netif)
  {
    return false;
  }
  netif_ = netif;

  g_netDriverBase.post_attach = espUsbDeviceNetPostAttach;
  if (esp_netif_attach(netif, &g_netDriverBase) != ESP_OK)
  {
    // Destroy the netif so a retry does not fail permanently on the duplicate
    // if_key "USB_NCM", and so the netif is not leaked.
    esp_netif_destroy(netif);
    netif_ = nullptr;
    return false;
  }
  // The iMACAddress descriptor (tud_network_mac_address) is the MAC assigned to
  // the *host* end of the USB link, so our own netif must use a *different* MAC
  // on this point-to-point segment — otherwise both ends share one MAC and ARP
  // resolves the peer to the host's own address. Toggle the low bit of the last
  // byte, exactly like TinyUSB's net_lwip_webserver. The USB link is an isolated
  // segment, so this derived address never leaks onto a real network.
  uint8_t devMac[6];
  memcpy(devMac, tud_network_mac_address, 6);
  devMac[5] ^= 0x01;
  esp_netif_set_mac(netif, devMac);

  // Start the interface, then apply addressing.
  esp_netif_action_start(netif, nullptr, 0, nullptr);
  if (dhcpClient_)
  {
    esp_netif_dhcpc_start(netif);
  }
  else
  {
    esp_netif_dhcps_stop(netif);
    esp_netif_set_ip_info(netif, &g_netIpInfo);
    if (dhcpServer_)
    {
      // By default advertise neither a router (option 3) nor DNS (option 6): the
      // device is a local endpoint, not a forwarding gateway, so advertising
      // itself as the host's default route would black-hole its off-link traffic.
      // The host still gets the on-link route and reaches the device by IP; its
      // real internet path is untouched. dhcpAdvertiseGateway()/dhcpDns() opt in
      // when the device really does forward or has a reachable DNS.
      const uint8_t routerOffer = dhcpAdvertiseGateway_ ? 1 : 0;
      const uint8_t dnsOffer = dhcpDns_ ? 1 : 0;
      if (dhcpDns_)
      {
        esp_netif_dns_info_t dns = {};
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        dns.ip.u_addr.ip4.addr = dhcpDns_;
        esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
      }
      esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET, ESP_NETIF_ROUTER_SOLICITATION_ADDRESS,
                             const_cast<uint8_t *>(&routerOffer), sizeof(routerOffer));
      esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
                             const_cast<uint8_t *>(&dnsOffer), sizeof(dnsOffer));
      esp_netif_dhcps_start(netif);
    }
  }
  // Mark the link connected so lwIP brings the interface up.
  esp_netif_action_connected(netif, nullptr, 0, nullptr);

  netStarted_ = true;
  return true;
#else
  return false;
#endif
}

EspUsbDeviceMidi::EspUsbDeviceMidi(EspUsbDevice &device) : EspUsbDeviceClass(device)
{
}

EspUsbDeviceMidi::~EspUsbDeviceMidi()
{
  end();
}

bool EspUsbDeviceMidi::begin()
{
  if (g_activeMidi && g_activeMidi != this)
  {
    return false;
  }
  g_activeMidi = this;
  return true;
}

void EspUsbDeviceMidi::end()
{
  if (g_activeMidi == this)
  {
    g_activeMidi = nullptr;
  }
}

uint16_t EspUsbDeviceMidi::configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize)
{
  if (!dst || endpointNumber == 0)
  {
    return 0;
  }
  const uint8_t epOut = endpointNumber;
  const uint8_t epIn = static_cast<uint8_t>(0x80 | endpointNumber);
  const uint8_t descriptor[] = {
      TUD_MIDI_DESCRIPTOR(interfaceNumber, 0, epOut, epIn, endpointSize),
  };
  memcpy(dst, descriptor, sizeof(descriptor));
  return sizeof(descriptor);
}

bool EspUsbDeviceMidi::readPacket(EspUsbDeviceMidiPacket &packet)
{
#if ESP_USB_DEVICE_HAS_TINYUSB
  return tud_midi_packet_read(reinterpret_cast<uint8_t *>(&packet));
#else
  (void)packet;
  return false;
#endif
}

bool EspUsbDeviceMidi::writePacket(const EspUsbDeviceMidiPacket &packet)
{
#if ESP_USB_DEVICE_HAS_TINYUSB
  return tud_midi_packet_write(reinterpret_cast<const uint8_t *>(&packet));
#else
  (void)packet;
  return false;
#endif
}

bool EspUsbDeviceMidi::noteOn(uint8_t channel, uint8_t note, uint8_t velocity)
{
  EspUsbDeviceMidiPacket packet = {ESP_USB_DEVICE_MIDI_CIN_NOTE_ON, status(ESP_USB_DEVICE_MIDI_CIN_NOTE_ON, channel), clamp7(note), clamp7(velocity)};
  return writePacket(packet);
}

bool EspUsbDeviceMidi::noteOff(uint8_t channel, uint8_t note, uint8_t velocity)
{
  EspUsbDeviceMidiPacket packet = {ESP_USB_DEVICE_MIDI_CIN_NOTE_OFF, status(ESP_USB_DEVICE_MIDI_CIN_NOTE_OFF, channel), clamp7(note), clamp7(velocity)};
  return writePacket(packet);
}

bool EspUsbDeviceMidi::controlChange(uint8_t channel, uint8_t control, uint8_t value)
{
  EspUsbDeviceMidiPacket packet = {ESP_USB_DEVICE_MIDI_CIN_CONTROL_CHANGE, status(ESP_USB_DEVICE_MIDI_CIN_CONTROL_CHANGE, channel), clamp7(control), clamp7(value)};
  return writePacket(packet);
}

bool EspUsbDeviceMidi::programChange(uint8_t channel, uint8_t program)
{
  EspUsbDeviceMidiPacket packet = {ESP_USB_DEVICE_MIDI_CIN_PROGRAM_CHANGE, status(ESP_USB_DEVICE_MIDI_CIN_PROGRAM_CHANGE, channel), clamp7(program), 0};
  return writePacket(packet);
}

bool EspUsbDeviceMidi::polyPressure(uint8_t channel, uint8_t note, uint8_t pressure)
{
  EspUsbDeviceMidiPacket packet = {ESP_USB_DEVICE_MIDI_CIN_POLY_KEYPRESS, status(ESP_USB_DEVICE_MIDI_CIN_POLY_KEYPRESS, channel), clamp7(note), clamp7(pressure)};
  return writePacket(packet);
}

bool EspUsbDeviceMidi::channelPressure(uint8_t channel, uint8_t pressure)
{
  EspUsbDeviceMidiPacket packet = {ESP_USB_DEVICE_MIDI_CIN_CHANNEL_PRESSURE, status(ESP_USB_DEVICE_MIDI_CIN_CHANNEL_PRESSURE, channel), clamp7(pressure), 0};
  return writePacket(packet);
}

bool EspUsbDeviceMidi::pitchBend(uint8_t channel, uint16_t value)
{
  if (value > 16383)
  {
    value = 16383;
  }
  EspUsbDeviceMidiPacket packet = {
      ESP_USB_DEVICE_MIDI_CIN_PITCH_BEND_CHANGE,
      status(ESP_USB_DEVICE_MIDI_CIN_PITCH_BEND_CHANGE, channel),
      static_cast<uint8_t>(value & 0x7f),
      static_cast<uint8_t>((value >> 7) & 0x7f),
  };
  return writePacket(packet);
}

uint8_t EspUsbDeviceMidi::status(uint8_t codeIndex, uint8_t channel)
{
  if (channel > 15)
  {
    channel = 15;
  }
  return static_cast<uint8_t>((codeIndex << 4) | channel);
}

uint8_t EspUsbDeviceMidi::clamp7(uint8_t value)
{
  return value > 127 ? 127 : value;
}

EspUsbDeviceMsc::EspUsbDeviceMsc(EspUsbDevice &device) : EspUsbDeviceClass(device)
{
}

EspUsbDeviceMsc::~EspUsbDeviceMsc()
{
  end();
}

bool EspUsbDeviceMsc::begin()
{
  if (blockCount_ == 0 || blockSize_ == 0 || !readCallback_ || !writeCallback_)
  {
    return false;
  }
  if (g_activeMsc && g_activeMsc != this)
  {
    return false;
  }
  g_activeMsc = this;
  return true;
}

bool EspUsbDeviceMsc::begin(uint32_t blockCount, uint16_t blockSize)
{
  blockCount_ = blockCount;
  blockSize_ = blockSize;
  return blockCount_ > 0 && blockSize_ > 0 && readCallback_ && writeCallback_;
}

void EspUsbDeviceMsc::end()
{
  if (g_activeMsc == this)
  {
    g_activeMsc = nullptr;
  }
}

uint16_t EspUsbDeviceMsc::configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize)
{
  if (!dst || endpointNumber == 0)
  {
    return 0;
  }
  const uint8_t epOut = endpointNumber;
  const uint8_t epIn = static_cast<uint8_t>(0x80 | endpointNumber);
  const uint8_t descriptor[] = {
      TUD_MSC_DESCRIPTOR(interfaceNumber, 0, epOut, epIn, endpointSize),
  };
  memcpy(dst, descriptor, sizeof(descriptor));
  return sizeof(descriptor);
}

void EspUsbDeviceMsc::vendorID(const char *value)
{
  copyPadded(reinterpret_cast<uint8_t *>(vendor_), sizeof(vendor_) - 1, value);
  vendor_[sizeof(vendor_) - 1] = '\0';
}

void EspUsbDeviceMsc::productID(const char *value)
{
  copyPadded(reinterpret_cast<uint8_t *>(product_), sizeof(product_) - 1, value);
  product_[sizeof(product_) - 1] = '\0';
}

void EspUsbDeviceMsc::productRevision(const char *value)
{
  copyPadded(reinterpret_cast<uint8_t *>(revision_), sizeof(revision_) - 1, value);
  revision_[sizeof(revision_) - 1] = '\0';
}

void EspUsbDeviceMsc::mediaPresent(bool value)
{
  mediaPresent_ = value;
}

void EspUsbDeviceMsc::isWritable(bool value)
{
  writable_ = value;
}

void EspUsbDeviceMsc::onRead(EspUsbDeviceMscReadCallback callback)
{
  readCallback_ = callback;
}

void EspUsbDeviceMsc::onWrite(EspUsbDeviceMscWriteCallback callback)
{
  writeCallback_ = callback;
}

void EspUsbDeviceMsc::onStartStop(EspUsbDeviceMscStartStopCallback callback)
{
  startStopCallback_ = callback;
}

uint8_t EspUsbDeviceMsc::maxLun() const
{
  return 0;
}

void EspUsbDeviceMsc::inquiry(uint8_t vendor[8], uint8_t product[16], uint8_t revision[4]) const
{
  copyPadded(vendor, 8, vendor_);
  copyPadded(product, 16, product_);
  copyPadded(revision, 4, revision_);
}

bool EspUsbDeviceMsc::testUnitReady() const
{
  return mediaPresent_;
}

void EspUsbDeviceMsc::capacity(uint32_t *blockCount, uint16_t *blockSize) const
{
  if (!mediaPresent_)
  {
    *blockCount = 0;
    *blockSize = 0;
    return;
  }
  *blockCount = blockCount_;
  *blockSize = blockSize_;
}

bool EspUsbDeviceMsc::startStop(uint8_t powerCondition, bool start, bool loadEject)
{
  return startStopCallback_ ? startStopCallback_(powerCondition, start, loadEject) : true;
}

int32_t EspUsbDeviceMsc::read10(uint32_t lba, uint32_t offset, void *buffer, uint32_t size)
{
  if (!mediaPresent_ || !readCallback_)
  {
    return -1;
  }
  return readCallback_(lba, offset, buffer, size);
}

int32_t EspUsbDeviceMsc::write10(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size)
{
  if (!mediaPresent_ || !writable_ || !writeCallback_)
  {
    return -1;
  }
  return writeCallback_(lba, offset, buffer, size);
}

bool EspUsbDeviceMsc::writable() const
{
  return writable_;
}

void EspUsbDeviceMsc::copyPadded(uint8_t *dst, size_t size, const char *value)
{
  memset(dst, 0, size);
  if (!value)
  {
    return;
  }
  size_t length = strlen(value);
  if (length > size)
  {
    length = size;
  }
  memcpy(dst, value, length);
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

EspUsbDeviceHidKeyboard::EspUsbDeviceHidKeyboard(EspUsbDevice &device) : EspUsbDeviceClass(device)
{
}

bool EspUsbDeviceHidKeyboard::begin()
{
  return true;
}

bool EspUsbDeviceHidKeyboard::sendReport(const EspUsbDeviceBootKeyboardReport &report, uint32_t timeoutMs)
{
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

void EspUsbDeviceHidKeyboard::onOutputReport(OutputReportCallback callback)
{
  outputCallback_ = callback;
}

void EspUsbDeviceHidKeyboard::onProtocol(ProtocolCallback callback)
{
  protocolCallback_ = callback;
}

uint8_t EspUsbDeviceHidKeyboard::protocol() const
{
  return protocol_;
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
  // Round up to 32; well within CFG_TUD_HID_EP_BUFSIZE (64).
  return nkroEnabled_ ? 32 : 0;
}

void EspUsbDeviceHidKeyboard::enableNkro(bool enable)
{
  nkroEnabled_ = enable;
  if (!enable)
  {
    // Keep heldState() honest: the NKRO state means nothing in 6KRO mode, where
    // report_ carries the held keys instead.
    nkroState_.clear();
  }
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
  return nkroState_;
}

void EspUsbDeviceHidKeyboard::onHidSetReport(uint8_t reportId, uint8_t reportType, const uint8_t *data, uint16_t length)
{
  (void)reportId;
  if (reportType != ESP_USB_DEVICE_HID_REPORT_TYPE_OUTPUT || !data || length < 1 || !outputCallback_)
  {
    return;
  }
  EspUsbDeviceHidKeyboardOutputReport report;
  report.leds = data[0];
  report.numLock = report.leds & ESP_USB_DEVICE_KEYBOARD_LED_NUM_LOCK;
  report.capsLock = report.leds & ESP_USB_DEVICE_KEYBOARD_LED_CAPS_LOCK;
  report.scrollLock = report.leds & ESP_USB_DEVICE_KEYBOARD_LED_SCROLL_LOCK;
  report.compose = report.leds & ESP_USB_DEVICE_KEYBOARD_LED_COMPOSE;
  report.kana = report.leds & ESP_USB_DEVICE_KEYBOARD_LED_KANA;
  outputCallback_(report);
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
  event.protocol = protocol_;
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

EspUsbDeviceHidMouse::EspUsbDeviceHidMouse(EspUsbDevice &device) : EspUsbDeviceClass(device)
{
}

bool EspUsbDeviceHidMouse::begin()
{
  return true;
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
    : EspUsbDeviceClass(device), reportDescriptor_(reportDescriptor), reportDescriptorLength_(reportDescriptorLength), inputReportSize_(inputReportSize)
{
}

bool EspUsbDeviceHidCustom::begin()
{
  return reportDescriptor_ && reportDescriptorLength_ > 0 && inputReportSize_ > 0;
}

bool EspUsbDeviceHidCustom::sendReport(const void *data, size_t length, uint8_t reportId, uint32_t timeoutMs)
{
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
    : EspUsbDeviceClass(device), reportSize_(reportSize)
{
}

bool EspUsbDeviceHidVendor::begin()
{
  return reportSize_ > 0 && reportSize_ <= 63;
}

bool EspUsbDeviceHidVendor::sendInput(const void *data, size_t length, uint32_t timeoutMs)
{
  if (!data || length == 0)
  {
    return false;
  }
  if (length > reportSize_)
  {
    length = reportSize_;
  }
  return device_.sendHidReport(device_.classRuntimeInstance(hidInstance_), ESP_USB_DEVICE_HID_REPORT_ID_VENDOR, data, length, timeoutMs);
}

void EspUsbDeviceHidVendor::onOutputReport(ReportCallback callback)
{
  outputCallback_ = callback;
}

void EspUsbDeviceHidVendor::onFeatureReport(ReportCallback callback)
{
  featureCallback_ = callback;
}

uint16_t EspUsbDeviceHidVendor::configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize)
{
  uint16_t mps = static_cast<uint16_t>(reportSize_ + 1);
  if (mps < endpointSize)
  {
    mps = endpointSize;
  }
  if (mps > 64)
  {
    mps = 64;
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
  return VENDOR_REPORT_DESCRIPTOR;
}

uint16_t EspUsbDeviceHidVendor::hidReportDescriptorLength() const
{
  return sizeof(VENDOR_REPORT_DESCRIPTOR);
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

EspUsbDeviceHidGamepad::EspUsbDeviceHidGamepad(EspUsbDevice &device) : EspUsbDeviceClass(device)
{
}

bool EspUsbDeviceHidGamepad::begin()
{
  return true;
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
  return device_.sendHidReport(device_.classRuntimeInstance(hidInstance_), hidReportId(), data, sizeof(data), timeoutMs);
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

EspUsbDeviceHidConsumerControl::EspUsbDeviceHidConsumerControl(EspUsbDevice &device) : EspUsbDeviceClass(device)
{
}

bool EspUsbDeviceHidConsumerControl::begin()
{
  return true;
}

bool EspUsbDeviceHidConsumerControl::sendUsage(uint16_t usage, uint32_t timeoutMs)
{
  usage_ = usage;
  uint8_t report[2] = {
      static_cast<uint8_t>(usage & 0xff),
      static_cast<uint8_t>((usage >> 8) & 0xff),
  };
  return device_.sendHidReport(device_.classRuntimeInstance(hidInstance_), hidReportId(), report, sizeof(report), timeoutMs);
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

EspUsbDeviceHidSystemControl::EspUsbDeviceHidSystemControl(EspUsbDevice &device) : EspUsbDeviceClass(device)
{
}

bool EspUsbDeviceHidSystemControl::begin()
{
  return true;
}

bool EspUsbDeviceHidSystemControl::sendUsage(uint8_t usage, uint32_t timeoutMs)
{
  usage_ = usage;
  return device_.sendHidReport(device_.classRuntimeInstance(hidInstance_), hidReportId(), &usage_, sizeof(usage_), timeoutMs);
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
