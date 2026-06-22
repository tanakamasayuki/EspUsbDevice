#ifndef ESP_USB_DEVICE_H
#define ESP_USB_DEVICE_H

#include <Arduino.h>
#include <functional>
#include "espusbdevice_version.h"

#if __has_include(<esp_err.h>)
#include <esp_err.h>
#else
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_SUPPORTED 0x106
#endif

enum EspUsbDevicePort
{
  ESP_USB_DEVICE_PORT_DEFAULT = 0,
  ESP_USB_DEVICE_PORT_HIGH_SPEED,
  ESP_USB_DEVICE_PORT_FULL_SPEED,
};

enum EspUsbDeviceSpeed
{
  ESP_USB_DEVICE_SPEED_DEFAULT = 0,
  ESP_USB_DEVICE_SPEED_FULL,
  ESP_USB_DEVICE_SPEED_HIGH,
};

struct EspUsbDeviceConfig
{
  EspUsbDevicePort port = ESP_USB_DEVICE_PORT_DEFAULT;
  EspUsbDeviceSpeed speed = ESP_USB_DEVICE_SPEED_DEFAULT;
  const char *manufacturer = "EspUsbDevice";
  const char *product = "EspUsbDevice";
  const char *serialNumber = nullptr;
  uint16_t vid = 0x303a;
  uint16_t pid = 0x4000;
  bool selfPowered = false;
  uint16_t maxPowerMilliamps = 100;
};

static constexpr uint8_t ESP_USB_DEVICE_KEYBOARD_LED_NUM_LOCK = 0x01;
static constexpr uint8_t ESP_USB_DEVICE_KEYBOARD_LED_CAPS_LOCK = 0x02;
static constexpr uint8_t ESP_USB_DEVICE_KEYBOARD_LED_SCROLL_LOCK = 0x04;
static constexpr uint8_t ESP_USB_DEVICE_KEYBOARD_LED_COMPOSE = 0x08;
static constexpr uint8_t ESP_USB_DEVICE_KEYBOARD_LED_KANA = 0x10;

static constexpr uint8_t ESP_USB_HID_KEY_A = 0x04;
static constexpr uint8_t ESP_USB_HID_KEY_B = 0x05;
static constexpr uint8_t ESP_USB_HID_KEY_C = 0x06;
static constexpr uint8_t ESP_USB_HID_KEY_D = 0x07;
static constexpr uint8_t ESP_USB_HID_KEY_E = 0x08;
static constexpr uint8_t ESP_USB_HID_KEY_F = 0x09;
static constexpr uint8_t ESP_USB_HID_KEY_G = 0x0a;
static constexpr uint8_t ESP_USB_HID_KEY_H = 0x0b;
static constexpr uint8_t ESP_USB_HID_KEY_I = 0x0c;
static constexpr uint8_t ESP_USB_HID_KEY_J = 0x0d;
static constexpr uint8_t ESP_USB_HID_KEY_K = 0x0e;
static constexpr uint8_t ESP_USB_HID_KEY_L = 0x0f;
static constexpr uint8_t ESP_USB_HID_KEY_M = 0x10;
static constexpr uint8_t ESP_USB_HID_KEY_N = 0x11;
static constexpr uint8_t ESP_USB_HID_KEY_O = 0x12;
static constexpr uint8_t ESP_USB_HID_KEY_P = 0x13;
static constexpr uint8_t ESP_USB_HID_KEY_Q = 0x14;
static constexpr uint8_t ESP_USB_HID_KEY_R = 0x15;
static constexpr uint8_t ESP_USB_HID_KEY_S = 0x16;
static constexpr uint8_t ESP_USB_HID_KEY_T = 0x17;
static constexpr uint8_t ESP_USB_HID_KEY_U = 0x18;
static constexpr uint8_t ESP_USB_HID_KEY_V = 0x19;
static constexpr uint8_t ESP_USB_HID_KEY_W = 0x1a;
static constexpr uint8_t ESP_USB_HID_KEY_X = 0x1b;
static constexpr uint8_t ESP_USB_HID_KEY_Y = 0x1c;
static constexpr uint8_t ESP_USB_HID_KEY_Z = 0x1d;
static constexpr uint8_t ESP_USB_HID_KEY_LANG1 = 0x90;
static constexpr uint8_t ESP_USB_HID_KEY_LANG2 = 0x91;
static constexpr uint8_t ESP_USB_HID_KEY_INTERNATIONAL1 = 0x87;
static constexpr uint8_t ESP_USB_HID_KEY_INTERNATIONAL3 = 0x89;
static constexpr uint8_t ESP_USB_HID_KEY_HENKAN = ESP_USB_HID_KEY_INTERNATIONAL3;
static constexpr uint8_t ESP_USB_HID_KEY_MUHENKAN = ESP_USB_HID_KEY_INTERNATIONAL1;

static constexpr uint8_t ESP_USB_DEVICE_MOD_LEFT_CTRL = 0x01;
static constexpr uint8_t ESP_USB_DEVICE_MOD_LEFT_SHIFT = 0x02;
static constexpr uint8_t ESP_USB_DEVICE_MOD_LEFT_ALT = 0x04;
static constexpr uint8_t ESP_USB_DEVICE_MOD_LEFT_GUI = 0x08;
static constexpr uint8_t ESP_USB_DEVICE_MOD_RIGHT_CTRL = 0x10;
static constexpr uint8_t ESP_USB_DEVICE_MOD_RIGHT_SHIFT = 0x20;
static constexpr uint8_t ESP_USB_DEVICE_MOD_RIGHT_ALT = 0x40;
static constexpr uint8_t ESP_USB_DEVICE_MOD_RIGHT_GUI = 0x80;

static constexpr uint8_t ESP_USB_DEVICE_MOUSE_LEFT = 0x01;
static constexpr uint8_t ESP_USB_DEVICE_MOUSE_RIGHT = 0x02;
static constexpr uint8_t ESP_USB_DEVICE_MOUSE_MIDDLE = 0x04;
static constexpr uint8_t ESP_USB_DEVICE_MOUSE_BACK = 0x08;
static constexpr uint8_t ESP_USB_DEVICE_MOUSE_FORWARD = 0x10;

struct EspUsbDeviceBootKeyboardReport
{
  uint8_t modifiers = 0;
  uint8_t reserved = 0;
  uint8_t keys[6] = {};
};

struct EspUsbDeviceHidKeyboardOutputReport
{
  uint8_t leds = 0;
  bool numLock = false;
  bool capsLock = false;
  bool scrollLock = false;
  bool compose = false;
  bool kana = false;
};

struct EspUsbDeviceBootMouseReport
{
  uint8_t buttons = 0;
  int8_t x = 0;
  int8_t y = 0;
  int8_t wheel = 0;
};

class EspUsbDeviceClass;

class EspUsbDevice
{
public:
  EspUsbDevice();
  ~EspUsbDevice();

  bool begin();
  bool begin(const EspUsbDeviceConfig &config);
  void end();
  void task();
  bool ready() const;

  const EspUsbDeviceConfig &config() const;
  EspUsbDeviceSpeed requestedSpeed() const;
  uint16_t hidEndpointSize() const;
  esp_err_t lastError() const;
  const char *lastErrorName() const;

  bool addClass(EspUsbDeviceClass *deviceClass);
  bool sendHidReport(uint8_t instance, uint8_t reportId, const void *data, size_t length, uint32_t timeoutMs = 100);

  const uint8_t *deviceDescriptor();
  const uint8_t *configurationDescriptor(uint8_t index);
  const uint16_t *stringDescriptor(uint8_t index, uint16_t langid);
  const uint8_t *hidReportDescriptor(uint8_t instance);
  void handleHidSetReport(uint8_t instance, uint8_t reportId, uint8_t reportType, const uint8_t *data, uint16_t length);

private:
  friend class EspUsbDeviceClass;
  static constexpr size_t MAX_CLASSES = 4;
  static constexpr size_t MAX_CONFIG_DESCRIPTOR = 256;
  static constexpr size_t MAX_STRING_DESCRIPTOR = 64;

  bool buildDescriptors();
  void setLastError(esp_err_t error);

  EspUsbDeviceConfig config_;
  EspUsbDeviceClass *classes_[MAX_CLASSES] = {};
  size_t classCount_ = 0;
  bool running_ = false;
  bool ready_ = false;
  esp_err_t lastError_ = ESP_OK;
  uint8_t deviceDescriptor_[18] = {};
  uint8_t configDescriptor_[MAX_CONFIG_DESCRIPTOR] = {};
  uint16_t configDescriptorLength_ = 0;
  uint16_t stringDescriptor_[MAX_STRING_DESCRIPTOR] = {};
};

class EspUsbDeviceClass
{
public:
  virtual ~EspUsbDeviceClass() = default;
  virtual bool begin() { return true; }
  virtual uint16_t configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize) = 0;
  virtual uint8_t interfaceCount() const = 0;
  virtual uint8_t endpointCount() const = 0;
  virtual const uint8_t *hidReportDescriptor() const { return nullptr; }
  virtual uint16_t hidReportDescriptorLength() const { return 0; }
  virtual void onHidSetReport(uint8_t reportId, uint8_t reportType, const uint8_t *data, uint16_t length) {}

protected:
  explicit EspUsbDeviceClass(EspUsbDevice &device);
  EspUsbDevice &device_;
  uint8_t hidInstance_ = 0;
};

class EspUsbDeviceHidKeyboard : public EspUsbDeviceClass
{
public:
  using OutputReportCallback = std::function<void(const EspUsbDeviceHidKeyboardOutputReport &)>;

  explicit EspUsbDeviceHidKeyboard(EspUsbDevice &device);

  bool begin() override;
  bool sendReport(const EspUsbDeviceBootKeyboardReport &report, uint32_t timeoutMs = 100);
  bool pressUsage(uint8_t usage, uint8_t modifiers = 0, uint32_t holdMs = 10);
  bool releaseUsage(uint8_t usage, uint32_t timeoutMs = 100);
  bool releaseAll(uint32_t timeoutMs = 100);
  void onOutputReport(OutputReportCallback callback);

  uint16_t configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize) override;
  uint8_t interfaceCount() const override { return 1; }
  uint8_t endpointCount() const override { return 2; }
  const uint8_t *hidReportDescriptor() const override;
  uint16_t hidReportDescriptorLength() const override;
  void onHidSetReport(uint8_t reportId, uint8_t reportType, const uint8_t *data, uint16_t length) override;

private:
  EspUsbDeviceBootKeyboardReport report_;
  OutputReportCallback outputCallback_;
};

class EspUsbDeviceHidMouse : public EspUsbDeviceClass
{
public:
  explicit EspUsbDeviceHidMouse(EspUsbDevice &device);

  bool begin() override;
  bool sendReport(const EspUsbDeviceBootMouseReport &report, uint32_t timeoutMs = 100);
  bool move(int8_t x, int8_t y, int8_t wheel = 0, uint8_t buttons = 0, uint32_t timeoutMs = 100);
  bool click(uint8_t button, uint32_t holdMs = 10);

  uint16_t configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize) override;
  uint8_t interfaceCount() const override { return 1; }
  uint8_t endpointCount() const override { return 1; }
  const uint8_t *hidReportDescriptor() const override;
  uint16_t hidReportDescriptorLength() const override;
};

#endif
