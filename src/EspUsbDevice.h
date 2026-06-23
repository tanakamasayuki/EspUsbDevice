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

enum EspUsbDeviceKeyboardLayout : uint16_t
{
  ESP_USB_DEVICE_KEYBOARD_LAYOUT_ZH_TW = 0x0404,
  ESP_USB_DEVICE_KEYBOARD_LAYOUT_DA_DK = 0x0406,
  ESP_USB_DEVICE_KEYBOARD_LAYOUT_DE_DE = 0x0407,
  ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US = 0x0409,
  ESP_USB_DEVICE_KEYBOARD_LAYOUT_FI_FI = 0x040B,
  ESP_USB_DEVICE_KEYBOARD_LAYOUT_FR_FR = 0x040C,
  ESP_USB_DEVICE_KEYBOARD_LAYOUT_HU_HU = 0x040E,
  ESP_USB_DEVICE_KEYBOARD_LAYOUT_IT_IT = 0x0410,
  ESP_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP = 0x0411,
  ESP_USB_DEVICE_KEYBOARD_LAYOUT_KO_KR = 0x0412,
  ESP_USB_DEVICE_KEYBOARD_LAYOUT_NL_NL = 0x0413,
  ESP_USB_DEVICE_KEYBOARD_LAYOUT_NB_NO = 0x0414,
  ESP_USB_DEVICE_KEYBOARD_LAYOUT_PT_BR = 0x0416,
  ESP_USB_DEVICE_KEYBOARD_LAYOUT_SV_SE = 0x041D,
  ESP_USB_DEVICE_KEYBOARD_LAYOUT_ZH_CN = 0x0804,
  ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_GB = 0x0809,
  ESP_USB_DEVICE_KEYBOARD_LAYOUT_PT_PT = 0x0816,
  ESP_USB_DEVICE_KEYBOARD_LAYOUT_ES_ES = 0x0C0A,
  ESP_USB_DEVICE_KEYBOARD_LAYOUT_FR_CH = 0x100C,
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
  bool startTinyUsb = true;
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
static constexpr uint8_t ESP_USB_HID_KEY_1 = 0x1e;
static constexpr uint8_t ESP_USB_HID_KEY_2 = 0x1f;
static constexpr uint8_t ESP_USB_HID_KEY_3 = 0x20;
static constexpr uint8_t ESP_USB_HID_KEY_4 = 0x21;
static constexpr uint8_t ESP_USB_HID_KEY_5 = 0x22;
static constexpr uint8_t ESP_USB_HID_KEY_6 = 0x23;
static constexpr uint8_t ESP_USB_HID_KEY_7 = 0x24;
static constexpr uint8_t ESP_USB_HID_KEY_8 = 0x25;
static constexpr uint8_t ESP_USB_HID_KEY_9 = 0x26;
static constexpr uint8_t ESP_USB_HID_KEY_0 = 0x27;
static constexpr uint8_t ESP_USB_HID_KEY_ENTER = 0x28;
static constexpr uint8_t ESP_USB_HID_KEY_ESCAPE = 0x29;
static constexpr uint8_t ESP_USB_HID_KEY_BACKSPACE = 0x2a;
static constexpr uint8_t ESP_USB_HID_KEY_TAB = 0x2b;
static constexpr uint8_t ESP_USB_HID_KEY_SPACE = 0x2c;
static constexpr uint8_t ESP_USB_HID_KEY_MINUS = 0x2d;
static constexpr uint8_t ESP_USB_HID_KEY_EQUAL = 0x2e;
static constexpr uint8_t ESP_USB_HID_KEY_LEFT_BRACKET = 0x2f;
static constexpr uint8_t ESP_USB_HID_KEY_RIGHT_BRACKET = 0x30;
static constexpr uint8_t ESP_USB_HID_KEY_BACKSLASH = 0x31;
static constexpr uint8_t ESP_USB_HID_KEY_SEMICOLON = 0x33;
static constexpr uint8_t ESP_USB_HID_KEY_APOSTROPHE = 0x34;
static constexpr uint8_t ESP_USB_HID_KEY_GRAVE = 0x35;
static constexpr uint8_t ESP_USB_HID_KEY_COMMA = 0x36;
static constexpr uint8_t ESP_USB_HID_KEY_DOT = 0x37;
static constexpr uint8_t ESP_USB_HID_KEY_SLASH = 0x38;
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

static constexpr uint8_t ESP_USB_DEVICE_HID_REPORT_ID_KEYBOARD = 0x01;
static constexpr uint8_t ESP_USB_DEVICE_HID_REPORT_ID_MOUSE = 0x02;
static constexpr uint8_t ESP_USB_DEVICE_HID_REPORT_ID_CONSUMER_CONTROL = 0x04;
static constexpr uint8_t ESP_USB_DEVICE_HID_REPORT_ID_SYSTEM_CONTROL = 0x05;
static constexpr uint8_t ESP_USB_DEVICE_HID_REPORT_ID_VENDOR = 0x06;

static constexpr uint16_t ESP_USB_DEVICE_CONSUMER_CONTROL_NEXT_TRACK = 0x00b5;
static constexpr uint16_t ESP_USB_DEVICE_CONSUMER_CONTROL_PREVIOUS_TRACK = 0x00b6;
static constexpr uint16_t ESP_USB_DEVICE_CONSUMER_CONTROL_PLAY_PAUSE = 0x00cd;
static constexpr uint16_t ESP_USB_DEVICE_CONSUMER_CONTROL_MUTE = 0x00e2;
static constexpr uint16_t ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_UP = 0x00e9;
static constexpr uint16_t ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_DOWN = 0x00ea;

static constexpr uint8_t ESP_USB_DEVICE_SYSTEM_CONTROL_POWER_OFF = 0x01;
static constexpr uint8_t ESP_USB_DEVICE_SYSTEM_CONTROL_STANDBY = 0x02;
static constexpr uint8_t ESP_USB_DEVICE_SYSTEM_CONTROL_WAKE_HOST = 0x03;

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

enum EspUsbDeviceHidReportType : uint8_t
{
  ESP_USB_DEVICE_HID_REPORT_TYPE_INPUT = 0x01,
  ESP_USB_DEVICE_HID_REPORT_TYPE_OUTPUT = 0x02,
  ESP_USB_DEVICE_HID_REPORT_TYPE_FEATURE = 0x03,
};

struct EspUsbDeviceHidReport
{
  uint8_t reportId = 0;
  uint8_t reportType = 0;
  const uint8_t *data = nullptr;
  uint16_t length = 0;
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
  friend class EspUsbDeviceHidKeyboard;
  friend class EspUsbDeviceHidMouse;
  friend class EspUsbDeviceHidCustom;
  friend class EspUsbDeviceHidVendor;
  friend class EspUsbDeviceHidConsumerControl;
  friend class EspUsbDeviceHidSystemControl;
  static constexpr size_t MAX_CLASSES = 4;
  static constexpr size_t MAX_CONFIG_DESCRIPTOR = 256;
  static constexpr size_t MAX_HID_REPORT_DESCRIPTOR = 256;
  static constexpr size_t MAX_STRING_DESCRIPTOR = 64;

  bool buildDescriptors();
  bool compositeHid() const;
  uint8_t classReportId(uint8_t classInstance) const;
  uint8_t classRuntimeInstance(uint8_t classInstance) const;
  void setLastError(esp_err_t error);

  EspUsbDeviceConfig config_;
  EspUsbDeviceClass *classes_[MAX_CLASSES] = {};
  size_t classCount_ = 0;
  bool running_ = false;
  bool ready_ = false;
  bool tinyusbStarted_ = false;
  esp_err_t lastError_ = ESP_OK;
  uint8_t deviceDescriptor_[18] = {};
  uint8_t configDescriptor_[MAX_CONFIG_DESCRIPTOR] = {};
  uint8_t hidReportDescriptor_[MAX_HID_REPORT_DESCRIPTOR] = {};
  uint16_t configDescriptorLength_ = 0;
  uint16_t hidReportDescriptorLength_ = 0;
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
  virtual uint8_t hidReportId() const { return 0; }
  virtual const uint8_t *hidReportDescriptor() const { return nullptr; }
  virtual uint16_t hidReportDescriptorLength() const { return 0; }
  virtual void onHidSetReport(uint8_t reportId, uint8_t reportType, const uint8_t *data, uint16_t length) {}

protected:
  friend class EspUsbDevice;
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
  bool pressKey(char key, uint32_t timeoutMs = 100);
  bool tapUsage(uint8_t usage, uint8_t modifiers = 0, uint32_t holdMs = 10);
  bool tapKey(char key, uint32_t holdMs = 10);
  bool write(const char *text, uint32_t interKeyDelayMs = 5);
  bool releaseUsage(uint8_t usage, uint32_t timeoutMs = 100);
  bool releaseAll(uint32_t timeoutMs = 100);
  void setLayout(EspUsbDeviceKeyboardLayout layout);
  EspUsbDeviceKeyboardLayout layout() const;
  void onOutputReport(OutputReportCallback callback);

  uint16_t configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize) override;
  uint8_t interfaceCount() const override { return 1; }
  uint8_t endpointCount() const override { return 2; }
  uint8_t hidReportId() const override { return ESP_USB_DEVICE_HID_REPORT_ID_KEYBOARD; }
  const uint8_t *hidReportDescriptor() const override;
  uint16_t hidReportDescriptorLength() const override;
  void onHidSetReport(uint8_t reportId, uint8_t reportType, const uint8_t *data, uint16_t length) override;

private:
  bool asciiToUsage(char key, uint8_t &usage, uint8_t &modifiers) const;

  EspUsbDeviceBootKeyboardReport report_;
  EspUsbDeviceKeyboardLayout layout_ = ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US;
  OutputReportCallback outputCallback_;
};

class EspUsbDeviceHidMouse : public EspUsbDeviceClass
{
public:
  explicit EspUsbDeviceHidMouse(EspUsbDevice &device);

  bool begin() override;
  bool sendReport(const EspUsbDeviceBootMouseReport &report, uint32_t timeoutMs = 100);
  bool sendState(uint32_t timeoutMs = 100);
  bool move(int8_t x, int8_t y, int8_t wheel = 0, uint8_t buttons = 0, uint32_t timeoutMs = 100);
  bool wheel(int8_t wheel, uint32_t timeoutMs = 100);
  bool press(uint8_t buttons, uint32_t timeoutMs = 100);
  bool release(uint8_t buttons, uint32_t timeoutMs = 100);
  bool releaseAll(uint32_t timeoutMs = 100);
  bool click(uint8_t button, uint32_t holdMs = 10);
  uint8_t buttons() const;

  uint16_t configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize) override;
  uint8_t interfaceCount() const override { return 1; }
  uint8_t endpointCount() const override { return 1; }
  uint8_t hidReportId() const override { return ESP_USB_DEVICE_HID_REPORT_ID_MOUSE; }
  const uint8_t *hidReportDescriptor() const override;
  uint16_t hidReportDescriptorLength() const override;

private:
  uint8_t buttons_ = 0;
};

class EspUsbDeviceHidCustom : public EspUsbDeviceClass
{
public:
  EspUsbDeviceHidCustom(EspUsbDevice &device, const uint8_t *reportDescriptor, uint16_t reportDescriptorLength, uint16_t inputReportSize = 64);

  bool begin() override;
  bool sendReport(const void *data, size_t length, uint8_t reportId = 0, uint32_t timeoutMs = 100);

  uint16_t configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize) override;
  uint8_t interfaceCount() const override { return 1; }
  uint8_t endpointCount() const override { return 1; }
  const uint8_t *hidReportDescriptor() const override;
  uint16_t hidReportDescriptorLength() const override;

private:
  const uint8_t *reportDescriptor_ = nullptr;
  uint16_t reportDescriptorLength_ = 0;
  uint16_t inputReportSize_ = 64;
};

class EspUsbDeviceHidVendor : public EspUsbDeviceClass
{
public:
  using ReportCallback = std::function<void(const EspUsbDeviceHidReport &)>;

  explicit EspUsbDeviceHidVendor(EspUsbDevice &device, uint16_t reportSize = 63);

  bool begin() override;
  bool sendInput(const void *data, size_t length, uint32_t timeoutMs = 100);
  void onOutputReport(ReportCallback callback);
  void onFeatureReport(ReportCallback callback);

  uint16_t configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize) override;
  uint8_t interfaceCount() const override { return 1; }
  uint8_t endpointCount() const override { return 2; }
  uint8_t hidReportId() const override { return ESP_USB_DEVICE_HID_REPORT_ID_VENDOR; }
  const uint8_t *hidReportDescriptor() const override;
  uint16_t hidReportDescriptorLength() const override;
  void onHidSetReport(uint8_t reportId, uint8_t reportType, const uint8_t *data, uint16_t length) override;

private:
  uint16_t reportSize_ = 63;
  ReportCallback outputCallback_;
  ReportCallback featureCallback_;
};

class EspUsbDeviceHidConsumerControl : public EspUsbDeviceClass
{
public:
  explicit EspUsbDeviceHidConsumerControl(EspUsbDevice &device);

  bool begin() override;
  bool press(uint16_t usage, uint32_t timeoutMs = 100);
  bool release(uint32_t timeoutMs = 100);
  bool click(uint16_t usage, uint32_t holdMs = 10);
  bool sendUsage(uint16_t usage, uint32_t timeoutMs = 100);
  uint16_t usage() const;

  uint16_t configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize) override;
  uint8_t interfaceCount() const override { return 1; }
  uint8_t endpointCount() const override { return 1; }
  uint8_t hidReportId() const override { return ESP_USB_DEVICE_HID_REPORT_ID_CONSUMER_CONTROL; }
  const uint8_t *hidReportDescriptor() const override;
  uint16_t hidReportDescriptorLength() const override;

private:
  uint16_t usage_ = 0;
};

class EspUsbDeviceHidSystemControl : public EspUsbDeviceClass
{
public:
  explicit EspUsbDeviceHidSystemControl(EspUsbDevice &device);

  bool begin() override;
  bool press(uint8_t usage, uint32_t timeoutMs = 100);
  bool release(uint32_t timeoutMs = 100);
  bool click(uint8_t usage, uint32_t holdMs = 10);
  bool sendUsage(uint8_t usage, uint32_t timeoutMs = 100);
  uint8_t usage() const;

  uint16_t configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize) override;
  uint8_t interfaceCount() const override { return 1; }
  uint8_t endpointCount() const override { return 1; }
  uint8_t hidReportId() const override { return ESP_USB_DEVICE_HID_REPORT_ID_SYSTEM_CONTROL; }
  const uint8_t *hidReportDescriptor() const override;
  uint16_t hidReportDescriptorLength() const override;

private:
  uint8_t usage_ = 0;
};

#endif
