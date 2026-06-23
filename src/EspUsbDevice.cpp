#include "EspUsbDevice.h"

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
static constexpr uint8_t ESP_USB_DEVICE_HID_REPORT_ID_KEYBOARD = 0x01;
static constexpr uint8_t ESP_USB_DEVICE_HID_REPORT_ID_MOUSE = 0x02;
static constexpr uint8_t ESP_USB_DEVICE_HID_REPORT_ID_VENDOR = 0x06;

static EspUsbDevice *g_activeDevice = nullptr;

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

    const uint8_t *configDescriptor = configurationDescriptor(0);
    const uint16_t totalLength = static_cast<uint16_t>(configDescriptor[2]) | (static_cast<uint16_t>(configDescriptor[3]) << 8);
    const uint16_t interfaceLength = totalLength >= 9 ? totalLength - 9 : 0;
    esp_err_t err = tinyusb_enable_interface2(USB_INTERFACE_HID, interfaceLength, espUsbDeviceLoadHidDescriptor, false);
    if (err != ESP_OK)
    {
      setLastError(err);
      return false;
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
  return classes_[instance]->hidReportDescriptor();
}

void EspUsbDevice::handleHidSetReport(uint8_t instance, uint8_t reportId, uint8_t reportType, const uint8_t *data, uint16_t length)
{
  if (compositeHid())
  {
    for (size_t i = 0; i < classCount_; i++)
    {
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

bool EspUsbDevice::buildDescriptors()
{
  const bool composite = compositeHid();
  uint8_t interfaceCount = composite ? 1 : 0;
  if (!composite)
  {
    for (size_t i = 0; i < classCount_; i++)
    {
      interfaceCount += classes_[i]->interfaceCount();
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
  return classCount_ > 1;
}

uint8_t EspUsbDevice::classReportId(uint8_t classInstance) const
{
  if (!compositeHid())
  {
    return 0;
  }
  if (classInstance < classCount_ && classes_[classInstance])
  {
    if (classes_[classInstance]->interfaceCount() == 1 && classes_[classInstance]->endpointCount() == 1)
    {
      return ESP_USB_DEVICE_HID_REPORT_ID_MOUSE;
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
