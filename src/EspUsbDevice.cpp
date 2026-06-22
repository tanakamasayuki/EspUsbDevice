#include "EspUsbDevice.h"

#include <string.h>

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
static constexpr uint8_t HID_REPORT_TYPE_OUTPUT = 0x02;

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
  running_ = true;
  ready_ = false;
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
  (void)instance;
  (void)reportId;
  (void)data;
  (void)length;
  (void)timeoutMs;
  setLastError(ESP_ERR_NOT_SUPPORTED);
  return false;
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
  if (instance >= classCount_ || !classes_[instance])
  {
    return nullptr;
  }
  return classes_[instance]->hidReportDescriptor();
}

void EspUsbDevice::handleHidSetReport(uint8_t instance, uint8_t reportId, uint8_t reportType, const uint8_t *data, uint16_t length)
{
  if (instance < classCount_ && classes_[instance])
  {
    classes_[instance]->onHidSetReport(reportId, reportType, data, length);
  }
}

bool EspUsbDevice::buildDescriptors()
{
  uint8_t interfaceCount = 0;
  uint8_t endpointCount = 0;
  for (size_t i = 0; i < classCount_; i++)
  {
    interfaceCount += classes_[i]->interfaceCount();
    endpointCount += classes_[i]->endpointCount();
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
  const uint16_t endpointSize = hidEndpointSize();
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
  configDescriptorLength_ = offset;
  put16(&configDescriptor_[2], configDescriptorLength_);
  setLastError(ESP_OK);
  return true;
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
  return device_.sendHidReport(hidInstance_, 0, &report_, sizeof(report_), timeoutMs);
}

bool EspUsbDeviceHidKeyboard::pressUsage(uint8_t usage, uint8_t modifiers, uint32_t holdMs)
{
  (void)holdMs;
  report_.modifiers = modifiers;
  report_.keys[0] = usage;
  return sendReport(report_);
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
  if (reportType != HID_REPORT_TYPE_OUTPUT || !data || length < 1 || !outputCallback_)
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

EspUsbDeviceHidMouse::EspUsbDeviceHidMouse(EspUsbDevice &device) : EspUsbDeviceClass(device)
{
}

bool EspUsbDeviceHidMouse::begin()
{
  return true;
}

bool EspUsbDeviceHidMouse::sendReport(const EspUsbDeviceBootMouseReport &report, uint32_t timeoutMs)
{
  return device_.sendHidReport(hidInstance_, 0, &report, sizeof(report), timeoutMs);
}

bool EspUsbDeviceHidMouse::move(int8_t x, int8_t y, int8_t wheel, uint8_t buttons, uint32_t timeoutMs)
{
  EspUsbDeviceBootMouseReport report;
  report.buttons = buttons;
  report.x = x;
  report.y = y;
  report.wheel = wheel;
  return sendReport(report, timeoutMs);
}

bool EspUsbDeviceHidMouse::click(uint8_t button, uint32_t holdMs)
{
  (void)holdMs;
  EspUsbDeviceBootMouseReport press;
  press.buttons = button;
  if (!sendReport(press))
  {
    return false;
  }
  EspUsbDeviceBootMouseReport release;
  return sendReport(release);
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
