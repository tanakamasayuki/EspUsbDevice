#include "EspUsbDevice.h"

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

#if defined(SOC_USB_OTG_SUPPORTED) && SOC_USB_OTG_SUPPORTED && __has_include("esp32-hal-tinyusb.h")
#include "esp32-hal-tinyusb.h"
#include "class/cdc/cdc_device.h"
#include "class/midi/midi_device.h"
#include "class/msc/msc_device.h"
#define ESP_USB_DEVICE_HAS_ARDUINO_TINYUSB 1
#else
#define ESP_USB_DEVICE_HAS_ARDUINO_TINYUSB 0
#endif

static constexpr uint8_t USB_DESC_DEVICE = 0x01;
static constexpr uint8_t USB_DESC_CONFIGURATION = 0x02;
static constexpr uint8_t USB_DESC_STRING = 0x03;
static constexpr uint8_t USB_DESC_INTERFACE = 0x04;
static constexpr uint8_t USB_DESC_ENDPOINT = 0x05;
static constexpr uint8_t USB_DESC_HID = 0x21;

static constexpr uint8_t USB_CLASS_HID = 0x03;
static constexpr uint8_t USB_SUBCLASS_BOOT = 0x01;
static constexpr uint8_t USB_PROTOCOL_KEYBOARD = 0x01;
static constexpr uint8_t USB_PROTOCOL_MOUSE = 0x02;

static constexpr uint8_t USB_ENDPOINT_ATTR_INTERRUPT = 0x03;
static constexpr uint8_t USB_SCSI_CMD_SYNCHRONIZE_CACHE_10 = 0x35;

static EspUsbDevice *g_activeDevice = nullptr;
static EspUsbDeviceCdcSerial *g_activeCdcSerial = nullptr;
static EspUsbDeviceMidi *g_activeMidi = nullptr;
static EspUsbDeviceMsc *g_activeMsc = nullptr;

static void put16(uint8_t *dst, uint16_t value)
{
  dst[0] = static_cast<uint8_t>(value & 0xff);
  dst[1] = static_cast<uint8_t>((value >> 8) & 0xff);
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

#if ESP_USB_DEVICE_HAS_ARDUINO_TINYUSB
static uint16_t espUsbDeviceLoadHidDescriptor(uint8_t *dst, uint8_t *itf)
{
  if (!g_activeDevice)
  {
    return 0;
  }
  const uint8_t *config = g_activeDevice->configurationDescriptor(0);
  if (!config)
  {
    return 0;
  }
  const uint16_t totalLength = static_cast<uint16_t>(config[2]) | (static_cast<uint16_t>(config[3]) << 8);
  if (totalLength < 9)
  {
    return 0;
  }
  const uint16_t interfaceLength = totalLength - 9;
  memcpy(dst, config + 9, interfaceLength);
  *itf = static_cast<uint8_t>(*itf + config[4]);
  return interfaceLength;
}

static uint16_t espUsbDeviceLoadCdcDescriptor(uint8_t *dst, uint8_t *itf)
{
  const uint8_t strIndex = tinyusb_add_string_descriptor("EspUsbDevice CDC");
  static constexpr uint16_t CDC_NOTIFICATION_ENDPOINT_SIZE = 8;
  static constexpr uint16_t CDC_DATA_ENDPOINT_SIZE = 64;
  uint8_t descriptor[] = {
      TUD_CDC_DESCRIPTOR(*itf, strIndex, 0x85, CDC_NOTIFICATION_ENDPOINT_SIZE, 0x03, 0x84, CDC_DATA_ENDPOINT_SIZE),
  };
  memcpy(dst, descriptor, sizeof(descriptor));
  *itf = static_cast<uint8_t>(*itf + 2);
  return sizeof(descriptor);
}

static uint16_t espUsbDeviceLoadMidiDescriptor(uint8_t *dst, uint8_t *itf)
{
  const uint8_t strIndex = tinyusb_add_string_descriptor("EspUsbDevice MIDI");
  const uint8_t epIn = tinyusb_get_free_in_endpoint();
  const uint8_t epOut = tinyusb_get_free_out_endpoint();
  if (epIn == 0 || epOut == 0)
  {
    return 0;
  }
  static constexpr uint16_t MIDI_ENDPOINT_SIZE = 64;
  uint8_t descriptor[] = {
      TUD_MIDI_DESCRIPTOR(*itf, strIndex, epOut, static_cast<uint8_t>(0x80 | epIn), MIDI_ENDPOINT_SIZE),
  };
  memcpy(dst, descriptor, sizeof(descriptor));
  *itf = static_cast<uint8_t>(*itf + 2);
  return sizeof(descriptor);
}

static uint16_t espUsbDeviceLoadMscDescriptor(uint8_t *dst, uint8_t *itf)
{
  const uint8_t strIndex = tinyusb_add_string_descriptor("EspUsbDevice MSC");
  const uint8_t epNum = tinyusb_get_free_duplex_endpoint();
  if (epNum == 0)
  {
    return 0;
  }
  static constexpr uint16_t MSC_ENDPOINT_SIZE = 64;
  uint8_t descriptor[] = {
      TUD_MSC_DESCRIPTOR(*itf, strIndex, epNum, static_cast<uint8_t>(0x80 | epNum), MSC_ENDPOINT_SIZE),
  };
  memcpy(dst, descriptor, sizeof(descriptor));
  *itf = static_cast<uint8_t>(*itf + 1);
  return sizeof(descriptor);
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
  for (size_t i = 0; i < classCount_; i++)
  {
    if (classes_[i] && !classes_[i]->begin())
    {
      setLastError(ESP_FAIL);
      return false;
    }
  }
  if (config_.startTinyUsb && classCount_ > 0)
  {
#if ESP_USB_DEVICE_HAS_ARDUINO_TINYUSB
    if (g_activeDevice && g_activeDevice != this)
    {
      setLastError(ESP_ERR_INVALID_STATE);
      return false;
    }
    g_activeDevice = this;

    esp_err_t err = ESP_OK;
    if (hasHidClass())
    {
      const uint8_t *configDescriptor = configurationDescriptor(0);
      const uint16_t totalLength = static_cast<uint16_t>(configDescriptor[2]) | (static_cast<uint16_t>(configDescriptor[3]) << 8);
      const uint16_t interfaceLength = totalLength >= 9 ? totalLength - 9 : 0;
      err = tinyusb_enable_interface2(USB_INTERFACE_HID, interfaceLength, espUsbDeviceLoadHidDescriptor, false);
      if (err != ESP_OK)
      {
        setLastError(err);
        return false;
      }
    }
    if (hasCdcClass())
    {
      err = tinyusb_enable_interface(USB_INTERFACE_CDC, TUD_CDC_DESC_LEN, espUsbDeviceLoadCdcDescriptor);
      if (err != ESP_OK)
      {
        setLastError(err);
        return false;
      }
    }
    if (hasMidiClass())
    {
      err = tinyusb_enable_interface(USB_INTERFACE_MIDI, TUD_MIDI_DESC_LEN, espUsbDeviceLoadMidiDescriptor);
      if (err != ESP_OK)
      {
        setLastError(err);
        return false;
      }
    }
    if (hasMscClass())
    {
      err = tinyusb_enable_interface(USB_INTERFACE_MSC, TUD_MSC_DESC_LEN, espUsbDeviceLoadMscDescriptor);
      if (err != ESP_OK)
      {
        setLastError(err);
        return false;
      }
    }

    tinyusb_device_config_t tinyusbConfig = {
        .vid = config_.vid,
        .pid = config_.pid,
        .product_name = config_.product,
        .manufacturer_name = config_.manufacturer,
        .serial_number = config_.serialNumber,
        .fw_version = 0x0100,
        .usb_version = 0x0200,
        .usb_class = 0x00,
        .usb_subclass = 0x00,
        .usb_protocol = 0x00,
        .usb_attributes = static_cast<uint8_t>(0x80 | (config_.selfPowered ? 0x40 : 0x00)),
        .usb_power_ma = config_.maxPowerMilliamps,
        .webusb_enabled = false,
        .webusb_url = nullptr,
    };

    err = tinyusb_init(&tinyusbConfig);
    if (err != ESP_OK)
    {
      setLastError(err);
      return false;
    }
    tinyusbStarted_ = true;
#else
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
  running_ = false;
  ready_ = false;
}

void EspUsbDevice::task()
{
}

bool EspUsbDevice::ready() const
{
#if ESP_USB_DEVICE_HAS_ARDUINO_TINYUSB
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

EspUsbDeviceSpeed EspUsbDevice::requestedSpeed() const
{
  return config_.speed;
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

bool EspUsbDevice::sendHidReport(uint8_t instance, uint8_t reportId, const void *data, size_t length, uint32_t timeoutMs)
{
  (void)timeoutMs;
#if ESP_USB_DEVICE_HAS_ARDUINO_TINYUSB
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
  if (index != 0)
  {
    return nullptr;
  }
  buildDescriptors();
  return configDescriptor_;
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

  memset(deviceDescriptor_, 0, sizeof(deviceDescriptor_));
  deviceDescriptor_[0] = 18;
  deviceDescriptor_[1] = USB_DESC_DEVICE;
  put16(&deviceDescriptor_[2], 0x0200);
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
  const uint16_t endpointSize = composite ? 16 : hidEndpointSize();
  if (composite)
  {
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

    const uint8_t epOut = endpointNumber;
    const uint8_t epIn = static_cast<uint8_t>(0x80 | (endpointNumber + 1));
    uint8_t descriptor[] = {
        9, USB_DESC_INTERFACE, interfaceNumber, 0, 2, USB_CLASS_HID, 0x00, 0x00, 0,
        9, USB_DESC_HID, 0x11, 0x01, 0x00, 1, 0x22, static_cast<uint8_t>(hidReportDescriptorLength_ & 0xff), static_cast<uint8_t>((hidReportDescriptorLength_ >> 8) & 0xff),
        7, USB_DESC_ENDPOINT, epOut, USB_ENDPOINT_ATTR_INTERRUPT, static_cast<uint8_t>(endpointSize & 0xff), static_cast<uint8_t>((endpointSize >> 8) & 0xff), 1,
        7, USB_DESC_ENDPOINT, epIn, USB_ENDPOINT_ATTR_INTERRUPT, static_cast<uint8_t>(endpointSize & 0xff), static_cast<uint8_t>((endpointSize >> 8) & 0xff), 1,
    };
    memcpy(&configDescriptor_[offset], descriptor, sizeof(descriptor));
    offset += sizeof(descriptor);
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
  configDescriptorLength_ = offset;
  put16(&configDescriptor_[2], configDescriptorLength_);
  setLastError(ESP_OK);
  return true;
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

EspUsbDeviceCdcSerial::EspUsbDeviceCdcSerial(EspUsbDevice &device) : EspUsbDeviceClass(device)
{
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

uint16_t EspUsbDeviceCdcSerial::configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize)
{
  (void)dst;
  (void)interfaceNumber;
  (void)endpointNumber;
  (void)endpointSize;
  return 0;
}

int EspUsbDeviceCdcSerial::available()
{
#if ESP_USB_DEVICE_HAS_ARDUINO_TINYUSB
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
#if ESP_USB_DEVICE_HAS_ARDUINO_TINYUSB
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
#if ESP_USB_DEVICE_HAS_ARDUINO_TINYUSB
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
#if ESP_USB_DEVICE_HAS_ARDUINO_TINYUSB
  tud_cdc_n_write_flush(0);
#endif
}

bool EspUsbDeviceCdcSerial::connected() const
{
#if ESP_USB_DEVICE_HAS_ARDUINO_TINYUSB
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

EspUsbDeviceMidi::EspUsbDeviceMidi(EspUsbDevice &device) : EspUsbDeviceClass(device)
{
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

uint16_t EspUsbDeviceMidi::configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize)
{
  (void)dst;
  (void)interfaceNumber;
  (void)endpointNumber;
  (void)endpointSize;
  return 0;
}

bool EspUsbDeviceMidi::readPacket(EspUsbDeviceMidiPacket &packet)
{
#if ESP_USB_DEVICE_HAS_ARDUINO_TINYUSB
  return tud_midi_packet_read(reinterpret_cast<uint8_t *>(&packet));
#else
  (void)packet;
  return false;
#endif
}

bool EspUsbDeviceMidi::writePacket(const EspUsbDeviceMidiPacket &packet)
{
#if ESP_USB_DEVICE_HAS_ARDUINO_TINYUSB
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

bool EspUsbDeviceMsc::begin()
{
  if (g_activeMsc && g_activeMsc != this)
  {
    return false;
  }
  g_activeMsc = this;
  return blockCount_ > 0 && blockSize_ > 0 && readCallback_ && writeCallback_;
}

bool EspUsbDeviceMsc::begin(uint32_t blockCount, uint16_t blockSize)
{
  blockCount_ = blockCount;
  blockSize_ = blockSize;
  return blockCount_ > 0 && blockSize_ > 0 && readCallback_ && writeCallback_;
}

uint16_t EspUsbDeviceMsc::configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize)
{
  (void)dst;
  (void)interfaceNumber;
  (void)endpointNumber;
  (void)endpointSize;
  return 0;
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
  if ((!start || loadEject) && ejectCallback_)
  {
    ejectCallback_();
  }
  return true;
}

EspUsbDeviceHidKeyboard::EspUsbDeviceHidKeyboard(EspUsbDevice &device) : EspUsbDeviceClass(device)
{
}

bool EspUsbDeviceHidKeyboard::begin()
{
  return true;
}

bool EspUsbDeviceHidKeyboard::sendReport(const EspUsbDeviceBootKeyboardReport &report, uint32_t timeoutMs)
{
  report_ = report;
  return device_.sendHidReport(device_.classRuntimeInstance(hidInstance_), device_.classReportId(hidInstance_), &report_, sizeof(report_), timeoutMs);
}

bool EspUsbDeviceHidKeyboard::pressUsage(uint8_t usage, uint8_t modifiers, uint32_t holdMs)
{
  (void)holdMs;
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
  const uint8_t epOut = endpointNumber;
  const uint8_t epIn = static_cast<uint8_t>(0x80 | (endpointNumber + 1));
  const uint16_t reportLen = hidReportDescriptorLength();
  uint8_t descriptor[] = {
      9, USB_DESC_INTERFACE, interfaceNumber, 0, 2, USB_CLASS_HID, USB_SUBCLASS_BOOT, USB_PROTOCOL_KEYBOARD, 0,
      9, USB_DESC_HID, 0x11, 0x01, 0x00, 1, 0x22, static_cast<uint8_t>(reportLen & 0xff), static_cast<uint8_t>((reportLen >> 8) & 0xff),
      7, USB_DESC_ENDPOINT, epOut, USB_ENDPOINT_ATTR_INTERRUPT, static_cast<uint8_t>(endpointSize & 0xff), static_cast<uint8_t>((endpointSize >> 8) & 0xff), 1,
      7, USB_DESC_ENDPOINT, epIn, USB_ENDPOINT_ATTR_INTERRUPT, static_cast<uint8_t>(endpointSize & 0xff), static_cast<uint8_t>((endpointSize >> 8) & 0xff), 1,
  };
  memcpy(dst, descriptor, sizeof(descriptor));
  return sizeof(descriptor);
}

const uint8_t *EspUsbDeviceHidKeyboard::hidReportDescriptor() const
{
  return KEYBOARD_REPORT_DESCRIPTOR;
}

uint16_t EspUsbDeviceHidKeyboard::hidReportDescriptorLength() const
{
  return sizeof(KEYBOARD_REPORT_DESCRIPTOR);
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

bool EspUsbDeviceHidKeyboard::asciiToUsage(char key, uint8_t &usage, uint8_t &modifiers) const
{
  const uint8_t(*table)[2] = KEYCODE_TO_ASCII_EN_US;
  size_t tableSize = 128;
  switch (layout_)
  {
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_DA_DK:
    table = KEYCODE_TO_ASCII_DA_DK;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_DE_DE:
    table = KEYCODE_TO_ASCII_DE_DE;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_GB:
    table = KEYCODE_TO_ASCII_EN_GB;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_ES_ES:
    table = KEYCODE_TO_ASCII_ES_ES;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_FI_FI:
    table = KEYCODE_TO_ASCII_FI_FI;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_FR_CH:
    table = KEYCODE_TO_ASCII_FR_CH;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_FR_FR:
    table = KEYCODE_TO_ASCII_FR_FR;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_HU_HU:
    table = KEYCODE_TO_ASCII_HU_HU;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_IT_IT:
    table = KEYCODE_TO_ASCII_IT_IT;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP:
    table = KEYCODE_TO_ASCII_JA_JP;
    tableSize = 0x90;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_NB_NO:
    table = KEYCODE_TO_ASCII_NB_NO;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_NL_NL:
    table = KEYCODE_TO_ASCII_NL_NL;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_PT_BR:
    table = KEYCODE_TO_ASCII_PT_BR;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_PT_PT:
    table = KEYCODE_TO_ASCII_PT_PT;
    break;
  case ESP_USB_DEVICE_KEYBOARD_LAYOUT_SV_SE:
    table = KEYCODE_TO_ASCII_SV_SE;
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
  for (size_t keycode = 0; keycode < tableSize; keycode++)
  {
    if (table[keycode][0] == static_cast<uint8_t>(key))
    {
      usage = static_cast<uint8_t>(keycode);
      return true;
    }
    if (table[keycode][1] == static_cast<uint8_t>(key))
    {
      usage = static_cast<uint8_t>(keycode);
      modifiers = ESP_USB_DEVICE_MOD_LEFT_SHIFT;
      return true;
    }
  }
  return false;
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
  const uint8_t epIn = static_cast<uint8_t>(0x80 | endpointNumber);
  const uint16_t reportLen = hidReportDescriptorLength();
  uint8_t descriptor[] = {
      9, USB_DESC_INTERFACE, interfaceNumber, 0, 1, USB_CLASS_HID, USB_SUBCLASS_BOOT, USB_PROTOCOL_MOUSE, 0,
      9, USB_DESC_HID, 0x11, 0x01, 0x00, 1, 0x22, static_cast<uint8_t>(reportLen & 0xff), static_cast<uint8_t>((reportLen >> 8) & 0xff),
      7, USB_DESC_ENDPOINT, epIn, USB_ENDPOINT_ATTR_INTERRUPT, static_cast<uint8_t>(endpointSize & 0xff), static_cast<uint8_t>((endpointSize >> 8) & 0xff), 1,
  };
  memcpy(dst, descriptor, sizeof(descriptor));
  return sizeof(descriptor);
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
  const uint8_t epIn = static_cast<uint8_t>(0x80 | endpointNumber);
  const uint16_t mps = inputReportSize_ < endpointSize ? inputReportSize_ : endpointSize;
  uint8_t descriptor[] = {
      9, USB_DESC_INTERFACE, interfaceNumber, 0, 1, USB_CLASS_HID, 0x00, 0x00, 0,
      9, USB_DESC_HID, 0x11, 0x01, 0x00, 1, 0x22, static_cast<uint8_t>(reportDescriptorLength_ & 0xff), static_cast<uint8_t>((reportDescriptorLength_ >> 8) & 0xff),
      7, USB_DESC_ENDPOINT, epIn, USB_ENDPOINT_ATTR_INTERRUPT, static_cast<uint8_t>(mps & 0xff), static_cast<uint8_t>((mps >> 8) & 0xff), 1,
  };
  memcpy(dst, descriptor, sizeof(descriptor));
  return sizeof(descriptor);
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
  const uint8_t epOut = endpointNumber;
  const uint8_t epIn = static_cast<uint8_t>(0x80 | (endpointNumber + 1));
  uint16_t mps = static_cast<uint16_t>(reportSize_ + 1);
  if (mps < endpointSize)
  {
    mps = endpointSize;
  }
  if (mps > 64)
  {
    mps = 64;
  }
  const uint16_t reportLen = hidReportDescriptorLength();
  uint8_t descriptor[] = {
      9, USB_DESC_INTERFACE, interfaceNumber, 0, 2, USB_CLASS_HID, 0x00, 0x00, 0,
      9, USB_DESC_HID, 0x11, 0x01, 0x00, 1, 0x22, static_cast<uint8_t>(reportLen & 0xff), static_cast<uint8_t>((reportLen >> 8) & 0xff),
      7, USB_DESC_ENDPOINT, epOut, USB_ENDPOINT_ATTR_INTERRUPT, static_cast<uint8_t>(mps & 0xff), static_cast<uint8_t>((mps >> 8) & 0xff), 1,
      7, USB_DESC_ENDPOINT, epIn, USB_ENDPOINT_ATTR_INTERRUPT, static_cast<uint8_t>(mps & 0xff), static_cast<uint8_t>((mps >> 8) & 0xff), 1,
  };
  memcpy(dst, descriptor, sizeof(descriptor));
  return sizeof(descriptor);
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
  const uint8_t epIn = static_cast<uint8_t>(0x80 | endpointNumber);
  const uint16_t mps = endpointSize < 12 ? 12 : endpointSize;
  const uint16_t reportLen = hidReportDescriptorLength();
  uint8_t descriptor[] = {
      9, USB_DESC_INTERFACE, interfaceNumber, 0, 1, USB_CLASS_HID, 0x00, 0x00, 0,
      9, USB_DESC_HID, 0x11, 0x01, 0x00, 1, 0x22, static_cast<uint8_t>(reportLen & 0xff), static_cast<uint8_t>((reportLen >> 8) & 0xff),
      7, USB_DESC_ENDPOINT, epIn, USB_ENDPOINT_ATTR_INTERRUPT, static_cast<uint8_t>(mps & 0xff), static_cast<uint8_t>((mps >> 8) & 0xff), 1,
  };
  memcpy(dst, descriptor, sizeof(descriptor));
  return sizeof(descriptor);
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
  const uint8_t epIn = static_cast<uint8_t>(0x80 | endpointNumber);
  const uint16_t reportLen = hidReportDescriptorLength();
  uint8_t descriptor[] = {
      9, USB_DESC_INTERFACE, interfaceNumber, 0, 1, USB_CLASS_HID, 0x00, 0x00, 0,
      9, USB_DESC_HID, 0x11, 0x01, 0x00, 1, 0x22, static_cast<uint8_t>(reportLen & 0xff), static_cast<uint8_t>((reportLen >> 8) & 0xff),
      7, USB_DESC_ENDPOINT, epIn, USB_ENDPOINT_ATTR_INTERRUPT, static_cast<uint8_t>(endpointSize & 0xff), static_cast<uint8_t>((endpointSize >> 8) & 0xff), 1,
  };
  memcpy(dst, descriptor, sizeof(descriptor));
  return sizeof(descriptor);
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
  const uint8_t epIn = static_cast<uint8_t>(0x80 | endpointNumber);
  const uint16_t reportLen = hidReportDescriptorLength();
  uint8_t descriptor[] = {
      9, USB_DESC_INTERFACE, interfaceNumber, 0, 1, USB_CLASS_HID, 0x00, 0x00, 0,
      9, USB_DESC_HID, 0x11, 0x01, 0x00, 1, 0x22, static_cast<uint8_t>(reportLen & 0xff), static_cast<uint8_t>((reportLen >> 8) & 0xff),
      7, USB_DESC_ENDPOINT, epIn, USB_ENDPOINT_ATTR_INTERRUPT, static_cast<uint8_t>(endpointSize & 0xff), static_cast<uint8_t>((endpointSize >> 8) & 0xff), 1,
  };
  memcpy(dst, descriptor, sizeof(descriptor));
  return sizeof(descriptor);
}

const uint8_t *EspUsbDeviceHidSystemControl::hidReportDescriptor() const
{
  return SYSTEM_CONTROL_REPORT_DESCRIPTOR;
}

uint16_t EspUsbDeviceHidSystemControl::hidReportDescriptorLength() const
{
  return sizeof(SYSTEM_CONTROL_REPORT_DESCRIPTOR);
}
