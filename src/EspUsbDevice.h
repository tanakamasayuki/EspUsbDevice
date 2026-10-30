#ifndef ESP_USB_DEVICE_H
#define ESP_USB_DEVICE_H

#include <Arduino.h>
#include <Print.h>
#include <Stream.h>
#include <atomic>
#include <functional>
#include "espusbdevice_version.h"
#include "internal/EspUsbAudioControl.h"
#include "internal/EspUsbAudioDescriptor.h"
#include "internal/EspUsbAudioEvent.h"

#if __has_include(<SD.h>)
#include <SD.h>
#define ESP_USB_DEVICE_HAS_ARDUINO_SD 1
#else
#define ESP_USB_DEVICE_HAS_ARDUINO_SD 0
#endif

#if __has_include("soc/soc_caps.h")
#include "soc/soc_caps.h"
#endif

#if __has_include(<esp_err.h>)
#include <esp_err.h>
#else
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_SUPPORTED 0x106
#endif

// begin() always validates descriptors and reserves selected Function resources.
// DescriptorsOnly omits the PHY, interrupt and USB worker; ready() remains false.
enum class EspUsbDeviceStartMode : uint8_t
{
  Usb,
  DescriptorsOnly,
};

enum class EspUsbController : uint8_t
{
  Auto,
  FullSpeed,
  HighSpeed,
};

// How much of the controller's transmit FIFO each bulk IN endpoint gets.
//
// The DWC2 core gives an IN endpoint one packet's worth of FIFO by default, so
// the next packet cannot be staged until the previous one has left. Two
// packets' worth lets the controller send one while the stack fills the other,
// which is what a sustained device-to-host stream is otherwise waiting for.
//
// The cost is not RAM. The FIFO is a fixed block inside the controller - 256
// 32-bit words on ESP32-S2/S3 and on the ESP32-P4 full-speed controller, 1024
// on the P4 high-speed controller - shared by the receive FIFO and every IN
// endpoint. Ask for more than there is and the endpoint simply does not open,
// which is a device that fails to enumerate rather than one that runs slowly.
enum class EspUsbBulkInBuffering : uint8_t
{
  // Double every bulk IN endpoint if they all fit, and change nothing if they
  // do not. All or nothing on purpose: a rule that doubles some endpoints and
  // not others makes throughput depend on the order functions were registered.
  Auto,
  // One packet per bulk IN endpoint, which is what the controller does without
  // being asked.
  Single,
  // Two packets for every bulk IN endpoint. begin() fails with
  // ESP_ERR_INVALID_SIZE if the FIFO cannot hold that, rather than starting a
  // device whose endpoints did not open.
  Double,
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

// Shape of the Microsoft OS 2.0 descriptor set, which is what decides whether
// Windows binds WinUSB to a vendor interface without an .inf file.
//
// The set may carry the compatible ID directly under its header, or wrap it in a
// configuration subset and a function subset. The subsets exist to name one
// function of a composite device, and Windows only resolves them through
// usbccgp.sys - the parent driver it loads for composite devices and nothing
// else. Put a function subset on a single-interface device and the compatible ID
// has no function to attach to: the device node never sees USB\MS_COMP_WINUSB,
// no driver matches, and Device Manager shows CM_PROB_FAILED_INSTALL (code 28)
// with not a line written to setupapi.dev.log.
enum EspUsbDeviceMsOs20Layout : uint8_t
{
  // Subsets when the configuration has more than one interface, flat when it has
  // exactly one. This is the shape Windows expects in both cases.
  ESP_USB_DEVICE_MS_OS_20_AUTO = 0,
  // Always put the compatible ID directly under the set header.
  ESP_USB_DEVICE_MS_OS_20_FLAT,
  // Always wrap it in configuration and function subsets.
  ESP_USB_DEVICE_MS_OS_20_SUBSETS,
};

struct EspUsbDeviceConfig
{
  // UTF-8 text, copied to immutable USB descriptors during begin(). Each
  // string may use at most 126 UTF-16 code units; invalid/long text is rejected.
  // config() retains the supplied pointer values, not ownership of the sources.
  const char *manufacturer = "EspUsbDevice";
  const char *product = "EspUsbDevice";
  const char *serialNumber = nullptr;
  // The three values a host keys the device on. VID/PID default to Espressif's
  // vendor ID and the first PID of the range ESP-IDF's TinyUSB uses by default
  // (0x4000-0x4007), which is fine on a bench and not for anything you ship:
  // see the user guide, "VID, PID and the other identity fields".
  uint16_t vid = 0x303a;
  uint16_t pid = 0x4000;
  // bcdDevice: the device release number, BCD, major in the high byte. Windows
  // folds it into a hardware ID (USB\VID_303A&PID_4000&REV_0100) that an INF
  // can match on; it is not part of the device instance key, so changing it
  // does not make the device a new one to the host. 0x0100 = 1.00, which is
  // what every release before this emitted.
  uint16_t deviceVersion = 0x0100;
  bool selfPowered = false;
  // Advertise capability; only the Host can enable its use.
  bool remoteWakeupSupported = false;
  uint16_t maxPowerMilliamps = 100;
  bool webusbEnabled = false;
  // nullptr advertises WebUSB without a landing page. When enabled, a URL
  // must be valid UTF-8 and fit 252 bytes after removing http(s)://.
  const char *webusbUrl = nullptr;
  EspUsbDeviceMsOs20Layout msOs20Layout = ESP_USB_DEVICE_MS_OS_20_AUTO;
  // The GUID Windows records as this interface's DeviceInterfaceGUIDs, which is
  // what a host application passes to SetupDiGetClassDevs to find the device.
  //
  // It does not affect which driver binds - that is the compatible ID alone -
  // but it does decide who finds you. Leaving it at the library's default means
  // every EspUsbDevice vendor interface in the world answers the same
  // enumeration, so an application looking for its own product also picks up
  // unrelated ones. Give your product its own.
  //
  // `{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}`, braces included, 38 characters.
  // begin() returns false with ESP_ERR_INVALID_ARG on anything else rather than
  // shipping a descriptor Windows will reject. nullptr keeps the default.
  const char *deviceInterfaceGuid = nullptr;
  // Ask Windows to treat this as a composite device even when it is not one.
  //
  // `MS_OS_20_FEATURE_CCGP_DEVICE` makes Windows load usbccgp.sys whatever the
  // interface count, class codes or configuration count say, which means the
  // device's functions get their own child nodes and their own registry
  // properties.
  //
  // Why that is worth asking for: Windows records `DeviceInterfaceGUIDs` on the
  // device's own node when it is not composite, and on the child when it is -
  // and it never removes one. A product that ships as a single vendor interface
  // and later adds a second function therefore ends up with a stale GUID on the
  // parent and the live one on a child, answering the same host-side lookup
  // twice (measured; see the user guide, "What Windows re-reads"). Being
  // composite from the first release avoids ever writing the device-scope one.
  //
  // Forces the subset layout, because function subsets are the point. Costs 4
  // bytes plus the subset headers. Default off: a device that is not going to
  // grow does not need it, and the flat layout is what has been measured most.
  bool msOs20CcgpDevice = false;
  // wVendorRevision in the Microsoft OS 2.0 descriptor set.
  //
  // Windows caches the registry properties it read from a device the first time
  // it enumerated, keyed on VID/PID/serial, and only re-reads them when this
  // number changes. Ship a new firmware with a different deviceInterfaceGuid
  // under the same identity and, without a change here, a PC that has already
  // seen the device keeps the old GUID forever.
  //
  // 0 (the default) derives it from the descriptor set's own bytes, so it moves
  // whenever anything in the set moves and there is nothing to remember to
  // bump. Set it yourself only if you need the number to follow your own
  // release scheme; microsoftOs20VendorRevision() reports whichever applied.
  uint16_t msOs20VendorRevision = 0;
  EspUsbDeviceStartMode startMode = EspUsbDeviceStartMode::Usb;
  // Event slots owned by the selected USB configuration; must be nonzero.
  // One additional slot is reserved for hardware events, excluded from fences.
  uint16_t eventQueueDepth = 16;
  // ESP-IDF task stack capacity in bytes, including application callbacks.
  uint32_t taskStackBytes = 4096;
  // -1 selects the existing default: configMAX_PRIORITIES - 1. Otherwise use
  // an RTOS priority from 0 through configMAX_PRIORITIES - 1.
  int8_t taskPriority = -1;
  EspUsbController controller = EspUsbController::Auto;
  // Use the board's hardware VBUS-valid signal instead of forcing B-session
  // valid. Requires actual VBUS sensing/routing on the board. Reconfigure after
  // end(), before begin(); false keeps the default for boards without sensing.
  bool vbusSensing = false;
  EspUsbBulkInBuffering bulkInBuffering = EspUsbBulkInBuffering::Auto;
  // -1 leaves the worker unpinned; otherwise choose an available core index.
  // A single-core build accepts only 0 or -1.
  int8_t taskCoreId = -1;
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
static constexpr uint8_t ESP_USB_DEVICE_HID_REPORT_ID_GAMEPAD = 0x03;
static constexpr uint8_t ESP_USB_DEVICE_HID_REPORT_ID_CONSUMER_CONTROL = 0x04;
static constexpr uint8_t ESP_USB_DEVICE_HID_REPORT_ID_SYSTEM_CONTROL = 0x05;
static constexpr uint8_t ESP_USB_DEVICE_HID_REPORT_ID_VENDOR = 0x06;

static constexpr uint8_t ESP_USB_DEVICE_GAMEPAD_HAT_CENTER = 0x00;
static constexpr uint8_t ESP_USB_DEVICE_GAMEPAD_HAT_UP = 0x01;
static constexpr uint8_t ESP_USB_DEVICE_GAMEPAD_HAT_UP_RIGHT = 0x02;
static constexpr uint8_t ESP_USB_DEVICE_GAMEPAD_HAT_RIGHT = 0x03;
static constexpr uint8_t ESP_USB_DEVICE_GAMEPAD_HAT_DOWN_RIGHT = 0x04;
static constexpr uint8_t ESP_USB_DEVICE_GAMEPAD_HAT_DOWN = 0x05;
static constexpr uint8_t ESP_USB_DEVICE_GAMEPAD_HAT_DOWN_LEFT = 0x06;
static constexpr uint8_t ESP_USB_DEVICE_GAMEPAD_HAT_LEFT = 0x07;
static constexpr uint8_t ESP_USB_DEVICE_GAMEPAD_HAT_UP_LEFT = 0x08;

static constexpr uint16_t ESP_USB_DEVICE_CONSUMER_CONTROL_NEXT_TRACK = 0x00b5;
static constexpr uint16_t ESP_USB_DEVICE_CONSUMER_CONTROL_PREVIOUS_TRACK = 0x00b6;
static constexpr uint16_t ESP_USB_DEVICE_CONSUMER_CONTROL_PLAY_PAUSE = 0x00cd;
static constexpr uint16_t ESP_USB_DEVICE_CONSUMER_CONTROL_MUTE = 0x00e2;
static constexpr uint16_t ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_UP = 0x00e9;
static constexpr uint16_t ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_DOWN = 0x00ea;

// Generic Desktop usage IDs; the HID array encodes these as selectors 1..3.
static constexpr uint8_t ESP_USB_DEVICE_SYSTEM_CONTROL_POWER_OFF = 0x81;
static constexpr uint8_t ESP_USB_DEVICE_SYSTEM_CONTROL_STANDBY = 0x82;
static constexpr uint8_t ESP_USB_DEVICE_SYSTEM_CONTROL_WAKE_HOST = 0x83;

static constexpr uint8_t ESP_USB_DEVICE_MIDI_CIN_NOTE_OFF = 0x08;
static constexpr uint8_t ESP_USB_DEVICE_MIDI_CIN_NOTE_ON = 0x09;
static constexpr uint8_t ESP_USB_DEVICE_MIDI_CIN_POLY_KEYPRESS = 0x0a;
static constexpr uint8_t ESP_USB_DEVICE_MIDI_CIN_CONTROL_CHANGE = 0x0b;
static constexpr uint8_t ESP_USB_DEVICE_MIDI_CIN_PROGRAM_CHANGE = 0x0c;
static constexpr uint8_t ESP_USB_DEVICE_MIDI_CIN_CHANNEL_PRESSURE = 0x0d;
static constexpr uint8_t ESP_USB_DEVICE_MIDI_CIN_PITCH_BEND_CHANGE = 0x0e;

struct EspUsbDeviceBootKeyboardReport
{
  uint8_t modifiers = 0;
  uint8_t reserved = 0;
  // Up to six held usages, one per slot (0 = empty). A usage array, not a
  // bitmap - see EspUsbDeviceNkroKeyboardReport::bitmap.
  uint8_t keys[6] = {};
};

// Full NKRO keyboard state in one report: modifier byte + a bitmap of usages
// 0x00-0xDF. Modifier usages 0xE0-0xE7 live in `modifiers`, not the bitmap, and
// press() / release() route them there automatically, so callers do not have to
// tell the two apart. Sizes differ from the Host side on purpose: EspUsbHost's
// EspUsbHostKeyboardState covers 0x00-0xFF (32 bytes) because it reads whatever
// a keyboard declares, while a device is bounded by its own report descriptor.
struct EspUsbDeviceNkroKeyboardReport
{
  static constexpr size_t BitmapSize = 28;
  static constexpr uint8_t MaxBitmapUsage = 0xdf;

  uint8_t modifiers = 0;
  // A bitmap, not a usage array: bit (usage & 7) of byte (usage >> 3) is the
  // state of that usage. EspUsbDeviceBootKeyboardReport::keys[6] holds usages
  // instead, which is why the two carry different names - the same assignment
  // would compile against either and silently mean something else.
  uint8_t bitmap[BitmapSize] = {};

  void clear()
  {
    modifiers = 0;
    for (size_t index = 0; index < BitmapSize; index++)
    {
      bitmap[index] = 0;
    }
  }

  // Returns false when the usage is above MaxBitmapUsage and is not a modifier
  // (0xE0-0xE7), i.e. this report cannot represent it.
  bool press(uint8_t usage)
  {
    if (usage >= 0xe0 && usage <= 0xe7)
    {
      modifiers |= static_cast<uint8_t>(1u << (usage - 0xe0));
      return true;
    }
    if (usage > MaxBitmapUsage)
    {
      return false;
    }
    bitmap[usage >> 3] |= static_cast<uint8_t>(1u << (usage & 7));
    return true;
  }

  bool release(uint8_t usage)
  {
    if (usage >= 0xe0 && usage <= 0xe7)
    {
      modifiers = static_cast<uint8_t>(modifiers & ~(1u << (usage - 0xe0)));
      return true;
    }
    if (usage > MaxBitmapUsage)
    {
      return false;
    }
    bitmap[usage >> 3] = static_cast<uint8_t>(bitmap[usage >> 3] & ~(1u << (usage & 7)));
    return true;
  }

  bool isDown(uint8_t usage) const
  {
    if (usage >= 0xe0 && usage <= 0xe7)
    {
      return (modifiers & static_cast<uint8_t>(1u << (usage - 0xe0))) != 0;
    }
    if (usage > MaxBitmapUsage)
    {
      return false;
    }
    return (bitmap[usage >> 3] & static_cast<uint8_t>(1u << (usage & 7))) != 0;
  }
};

// Host LED (Lock) state, as delivered to onOutputReport() and returned by
// EspUsbDeviceHidKeyboard::ledState().
struct EspUsbDeviceHidKeyboardOutputReport
{
  uint8_t leds = 0;
  bool numLock = false;
  bool capsLock = false;
  bool scrollLock = false;
  bool compose = false;
  bool kana = false;

  // The one place that decides which bit means what, so `leds` and the flags
  // cannot disagree. The library never assigns the fields directly. Same role as
  // EspBleHidKeyboardOutputReport::setLeds().
  void setLeds(uint8_t value)
  {
    leds = value;
    numLock = value & ESP_USB_DEVICE_KEYBOARD_LED_NUM_LOCK;
    capsLock = value & ESP_USB_DEVICE_KEYBOARD_LED_CAPS_LOCK;
    scrollLock = value & ESP_USB_DEVICE_KEYBOARD_LED_SCROLL_LOCK;
    compose = value & ESP_USB_DEVICE_KEYBOARD_LED_COMPOSE;
    kana = value & ESP_USB_DEVICE_KEYBOARD_LED_KANA;
  }
};

struct EspUsbDeviceBootMouseReport
{
  uint8_t buttons = 0;
  int8_t x = 0;
  int8_t y = 0;
  int8_t wheel = 0;
};

struct EspUsbDeviceGamepadReport
{
  int8_t x = 0;
  int8_t y = 0;
  int8_t z = 0;
  int8_t rz = 0;
  int8_t rx = 0;
  int8_t ry = 0;
  uint8_t hat = ESP_USB_DEVICE_GAMEPAD_HAT_CENTER;
  uint32_t buttons = 0;
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

struct EspUsbDeviceHidProtocolEvent
{
  uint8_t instance = 0;
  uint8_t protocol = 0;
};

struct EspUsbDeviceCdcLineCoding
{
  uint32_t baud = 0;
  uint8_t stopBits = 0;
  uint8_t parity = 0;
  uint8_t dataBits = 0;
};

struct EspUsbDeviceCdcLineState
{
  bool dtr = false;
  bool rts = false;
};

// Stage of a control transfer, as the observation hook reports it. Same values
// as the USB control-transfer stages.
static constexpr uint8_t ESP_USB_DEVICE_CONTROL_STAGE_SETUP = 1;
static constexpr uint8_t ESP_USB_DEVICE_CONTROL_STAGE_DATA = 2;
static constexpr uint8_t ESP_USB_DEVICE_CONTROL_STAGE_ACK = 3;

// One control request, as seen by EspUsbDevice::onAnyControlRequest().
struct EspUsbDeviceControlRequestInfo
{
  uint8_t rhport = 0;
  uint8_t stage = 0;
  uint8_t bmRequestType = 0;
  uint8_t bRequest = 0;
  uint16_t wValue = 0;
  uint16_t wIndex = 0;
  uint16_t wLength = 0;
  // Bytes the library handed to the stack as the answer, before the stack
  // clamps them to wLength. Non-zero only for the requests the library itself
  // answers: descriptors, the WebUSB URL and the Microsoft OS 2.0 set.
  uint16_t responseLength = 0;
  // At SETUP: the library recognised the request and produced an answer.
  // At ACK: the transfer completed, which a stalled request never reaches.
  bool handled = false;
};

struct EspUsbDeviceVendorControlRequest
{
  uint8_t rhport = 0;
  uint8_t stage = 0;
  uint8_t bmRequestType = 0;
  uint8_t bRequest = 0;
  uint16_t wValue = 0;
  uint16_t wIndex = 0;
  uint16_t wLength = 0;
  // Data-stage bytes transferred so far; 0 at SETUP. DATA/ACK report the
  // actual total in either direction, excluding SETUP/status packets and ZLPs.
  uint16_t actualBytes = 0;
  const void *rawRequest = nullptr;
};

// --- CCID (USB smart card reader, bInterfaceClass 0x0b) --------------------

// Low two bits of bStatus in the RDR_to_PC responses this device sends. Same
// values as EspUsbHost's EspUsbHostCcidIccStatus - they are the wire encoding.
enum EspUsbDeviceCcidIccStatus : uint8_t
{
  ESP_USB_DEVICE_CCID_ICC_ACTIVE = 0,   // card present and activated
  ESP_USB_DEVICE_CCID_ICC_INACTIVE = 1, // card present, not activated
  ESP_USB_DEVICE_CCID_ICC_ABSENT = 2,   // no card in the slot
};

// bError values this device reports (CCID 1.1 Table 6.2-2). Values 0..127 are
// the index of the offending byte in the command message, so 0 - the index of
// bMessageType - is how a reader says "that message type is not supported".
enum EspUsbDeviceCcidError : uint8_t
{
  ESP_USB_DEVICE_CCID_ERROR_CMD_NOT_SUPPORTED = 0x00,
  ESP_USB_DEVICE_CCID_ERROR_BAD_SLOT = 0x05, // index of bSlot
  ESP_USB_DEVICE_CCID_ERROR_HW_ERROR = 0xfb,
  ESP_USB_DEVICE_CCID_ERROR_XFR_OVERRUN = 0xfc,
  ESP_USB_DEVICE_CCID_ERROR_ICC_MUTE = 0xfe,
  ESP_USB_DEVICE_CCID_ERROR_CMD_ABORTED = 0xff,
};

// Longest ATR ISO 7816-3 allows (TS + T0 + 15 interface + 15 historical + TCK).
static constexpr size_t ESP_USB_DEVICE_CCID_MAX_ATR = 33;

#ifdef ESP_USB_DEVICE_CCID_BUFFER_SIZE
#error "Use EspUsbCcidConfig::messageBytes instead of ESP_USB_DEVICE_CCID_BUFFER_SIZE"
#endif
struct EspUsbCcidConfig
{
  size_t messageBytes = 320; // complete message, including its 10-byte header
  bool removable = true; // false omits notification endpoint; insert before begin
  const char *name = nullptr;
};
struct EspUsbCcidState;

struct EspUsbDeviceMidiPacket
{
  uint8_t header = 0;
  uint8_t byte1 = 0;
  uint8_t byte2 = 0;
  uint8_t byte3 = 0;
};

using EspUsbDeviceMscReadCallback = std::function<int32_t(uint32_t lba, uint32_t offset, void *buffer, uint32_t size)>;
using EspUsbDeviceMscWriteCallback = std::function<int32_t(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size)>;
using EspUsbDeviceMscStartStopCallback = std::function<bool(uint8_t powerCondition, bool start, bool loadEject)>;

class EspUsbDeviceClass;

class EspUsbDevice
{
public:
  EspUsbDevice();
  ~EspUsbDevice();

  // Owns a heap allocation, and registers itself with global callback targets, so
  // a copy was never meaningful; saying so here turns it into a build error rather
  // than a double free.
  EspUsbDevice(const EspUsbDevice &) = delete;
  EspUsbDevice &operator=(const EspUsbDevice &) = delete;

  bool begin();
  bool begin(const EspUsbDeviceConfig &config);
  // Stops the device and releases the bus. A host sees this as an unplug and a
  // following begin() as a plug, which is indistinguishable from the cable
  // being pulled - so a host-side test can reproduce disconnection without
  // anyone touching the hardware. The EspUsbHost suite drives exactly this
  // from its peer to test replugging a camera mid-stream.
  // Success means the worker and DMA have stopped and borrowed buffers were
  // returned. A call from a USB callback is rejected with INVALID_STATE.
  esp_err_t end();
  // Successful begin() owns the selected resources until end(), including
  // DescriptorsOnly mode. ready() additionally requires Host configuration.
  bool started() const;
  bool ready() const;
  // Bus suspend is independent of Host configuration: ready() stays true.
  // False before USB startup, in DescriptorsOnly mode, and after end().
  bool suspended() const;
  // Task-context Host-permission query; false after reset/end or in an ISR.
  bool remoteWakeupEnabled() const;
  // Task-context, nonblocking queue request. ESP_OK means accepted, not resumed.
  // Rejected unless configured, suspended and enabled by the Host. Later state
  // changes can cancel an accepted request. Duplicate pending requests fail.
  esp_err_t remoteWakeup();

  const EspUsbDeviceConfig &config() const;
  uint16_t hidEndpointSize() const;
  // Bitmap of IN endpoint numbers that were given a two-packet transmit FIFO,
  // as begin() decided it. 0 before begin(), and 0 whenever
  // EspUsbBulkInBuffering::Auto found the controller's FIFO too full to double
  // every bulk IN endpoint - which is the case worth checking when a stream is
  // slower than expected.
  uint16_t bulkInDoubleBuffered() const;
  esp_err_t lastError() const;
  const char *lastErrorName() const;

  bool addClass(EspUsbDeviceClass *deviceClass);
  // Wait up to timeoutMs for the HID IN endpoint to become available; 0 polls
  // once. A busy timeout sets ESP_ERR_TIMEOUT. Success queues a copied report,
  // rather than waiting for the host to receive it. Call from the sketch task;
  // USB callbacks must use timeoutMs = 0 so the USB task can keep progressing.
  bool sendHidReport(uint8_t instance, uint8_t reportId, const void *data, size_t length, uint32_t timeoutMs = 100);

  // Restart into the chip's ROM download loader - "boot mode", the state the
  // BOOT button produces at reset - so esptool can rewrite the whole flash over
  // the port the ROM answers on. Detaches the USB device first so the host sees
  // a disconnect rather than a vanished device. Does not return.
  //
  // The register differs per target and getting the ESP32-P4 one wrong is
  // costly: there the flag shares LP_SYSTEM_REG_SYS_CTRL_REG with the
  // software-reset bit and IO_MUX_RESET_DISABLE, so it has to be set rather
  // than written. That is the reason this is a library call and not four lines
  // in every sketch.
  //
  // Which connector the loader then answers on is a property of the chip, not
  // of this call: on a one-connector ESP32-S3 the shared PHY returns to
  // USB Serial/JTAG, so the same cable works; on ESP32-P4 it is the
  // USB Serial/JTAG port, which is not the high-speed OTG connector. See
  // docs/ota-over-usb.md.
  //
  // Sets ROM download mode directly, including the shared PHY routing needed
  // for USB Serial/JTAG on S3. It does not initialize Arduino's USB stack.
  bool rebootToBootloader();
  // Restart into the ROM's DFU device on USB-OTG instead of its serial loader,
  // for a host that drives dfu-util rather than esptool. ESP32-S2 and ESP32-S3
  // only; returns false without restarting on ESP32-P4, whose ROM exposes no
  // USB stack to ESP-IDF and whose ROM DFU is defective from silicon v3.1.
  //
  // Not the same thing as EspUsbDeviceDfu: this hands the chip to the ROM,
  // which replaces the whole flash. EspUsbDeviceDfu keeps the sketch running
  // and writes an OTA partition.
  bool rebootToRomDfu();

  const uint8_t *deviceDescriptor();
  const uint8_t *configurationDescriptor(uint8_t index);
  const uint8_t *configurationDescriptorForSpeed(uint8_t index, bool highSpeed);
  const uint8_t *deviceQualifierDescriptor();
  const uint8_t *otherSpeedConfigurationDescriptor(uint8_t index, bool currentHighSpeed);
  const uint8_t *bosDescriptor() const;
  uint16_t bosDescriptorLength() const;
  const uint8_t *microsoftOs20Descriptor() const;
  uint16_t microsoftOs20DescriptorLength() const;
  // The wVendorRevision the descriptor set was built with, derived or given.
  uint16_t microsoftOs20VendorRevision() const;
  // Whether the built descriptor set wraps its compatible ID in configuration
  // and function subsets. Resolves EspUsbDeviceMsOs20Layout::AUTO against the
  // configuration that was actually built.
  bool microsoftOs20UsesSubsets() const;
  // Byte length / interface count of only the HID interface descriptors inside
  // configDescriptor_ (excluding the configuration header and other functions).
  uint16_t hidInterfacesLength() const;
  uint8_t hidInterfaceCount() const;
  const uint16_t *stringDescriptor(uint8_t index, uint16_t langid);
  // Cached WebUSB URL #1, or nullptr when no landing page is configured.
  const uint8_t *webUsbUrlDescriptor() const;
  const uint8_t *hidReportDescriptor(uint8_t instance);
  // Length of what hidReportDescriptor(instance) returns. For a composite HID
  // that is the merged descriptor, which is not any one class's own length.
  uint16_t hidReportDescriptorLength(uint8_t instance);

  using ControlRequestObserver = std::function<void(const EspUsbDeviceControlRequestInfo &)>;
  // Watch control requests, including every one the library answers itself.
  //
  // EspUsbDeviceVendor::onControlRequest() sees only what is left over: the
  // library takes the WebUSB and Microsoft OS 2.0 vendor codes, and the whole
  // standard descriptor path, before that callback runs. Those are exactly the
  // requests worth watching when a host refuses to bind a driver, and they were
  // the ones invisible from a sketch.
  //
  // Purely an observer - the return value cannot change what the device answers.
  // It fires at SETUP for every vendor request (whether or not the library
  // answers it, so a stalled one still shows up) and at ACK for every control
  // transfer that completes, which covers the standard and class requests the
  // stack handles internally. Runs on the usbd task; keep it short.
  void onAnyControlRequest(ControlRequestObserver callback);
  void handleHidSetReport(uint8_t instance, uint8_t reportId, uint8_t reportType, const uint8_t *data, uint16_t length);
  void handleHidSetProtocol(uint8_t instance, uint8_t protocol);
  void handleBusAttached();
  void handleBusDetached();
  bool handleVendorControlRequest(uint8_t rhport, uint8_t stage, const void *request);
  // Report one control request to the observer, if a sketch installed one.
  void notifyControlRequest(const EspUsbDeviceControlRequestInfo &info);
  bool hasControlObserver() const { return static_cast<bool>(controlObserver_); }
  // Remember what the library just answered, so the ACK observation can say how
  // many bytes went out. Matched on the request fields rather than assumed to
  // belong to the next completion, because a request the device stalls never
  // produces one.
  void recordControlResponse(uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint16_t length);
  uint16_t takeControlResponse(uint8_t bRequest, uint16_t wValue, uint16_t wIndex);

private:
  friend class EspUsbDeviceClass;
  friend class EspUsbDeviceCdcSerial;
  friend class EspUsbDeviceBulkStream;
  friend class EspUsbDeviceHidKeyboard;
  friend class EspUsbDeviceHidMouse;
  friend class EspUsbDeviceHidCustom;
  friend class EspUsbDeviceHidVendor;
  friend class EspUsbDeviceHidGamepad;
  friend class EspUsbDeviceHidConsumerControl;
  friend class EspUsbDeviceHidSystemControl;
  friend class EspUsbDeviceVendor;
  friend class EspUsbAudioFunction;
  friend class EspUsbDeviceVideo;
  friend class EspUsbDeviceNet;
  friend class EspUsbDeviceCcid;
  friend class EspUsbDeviceDfu;
  friend class EspUsbDeviceMsc;
  friend class EspUsbHidFunction;
  friend struct EspUsbHidState;
  // String descriptor indices 1-3 are manufacturer / product / serial. Index
  // 4 is reserved; Function names precede their class-specific strings.
  static constexpr uint8_t FIRST_FUNCTION_STRING_INDEX = 5;
  bool buildDescriptors();
  bool buildStringDescriptors();
  bool buildWebUsbUrlDescriptor();
  void releaseDescriptors();
  bool buildConfigurationDescriptor(bool highSpeed);
  bool buildMergedHidReport();
  bool needsHighSpeedDescriptors() const;
  bool buildWebUsbDescriptors();
  // Resolves config_.msOs20Layout for the configuration currently in
  // configDescriptor_. Only meaningful after the configuration is assembled.
  bool microsoftOs20SubsetLayout() const;
  bool validateControllerEndpoints(const uint8_t *descriptor,
                                   uint16_t length);
  // Which bulk IN endpoints can have a two-packet transmit FIFO, given
  // everything else this configuration asks the controller's FIFO for.
  // `fits` reports whether doubling all of them was possible at all, which is
  // what EspUsbBulkInBuffering::Double turns into a begin() failure.
  uint16_t bulkInDoubleBufferMask(bool highSpeed, bool *fits) const;
  // Does every transmit FIFO this configuration needs fit the controller's
  // FIFO SPRAM? Isochronous endpoints are why this exists: an isochronous IN
  // endpoint whose FIFO cannot be placed still enumerates and still reports
  // completed transfers, and the host receives nothing.
  bool transmitFifoFits(bool highSpeed) const;
  bool hasIsochronousInEndpoint(bool highSpeed) const;
  bool compositeHid() const;
  bool hasHidClass() const;
  bool hasCdcClass() const;
  bool hasMidiClass() const;
  bool hasMscClass() const;
  bool hasVendorClass() const;
  bool hasAudioClass() const;
  bool hasVideoClass() const;
  bool hasNetClass() const;
  uint8_t classReportId(uint8_t classInstance) const;
  // Two different numbers meet here. classReportId() and classRuntimeInstance()
  // take the class's slot in classes_ (EspUsbDeviceClass::hidInstance_), which
  // is registration order. hidClassForInstance() takes the HID driver instance
  // number - always 0 in this build - and returns the class behind it. Treating
  // one as the other is what once broke a HID class registered after a non-HID
  // one.
  uint8_t classRuntimeInstance(uint8_t classInstance) const;
  EspUsbDeviceClass *hidClassForInstance(uint8_t instance) const;
  void setLastError(esp_err_t error);
  void removeClass(EspUsbDeviceClass *deviceClass);

  EspUsbDeviceConfig config_;
  // Intrusive configuration order; the exact-sized lookup/name table exists
  // only during a build/run and is released on end or failed begin.
  EspUsbDeviceClass *firstClass_ = nullptr, *lastClass_ = nullptr;
  EspUsbDeviceClass **classes_ = nullptr;
  size_t classCount_ = 0, rejectedClassCount_ = 0;
  struct RegistrationTransaction;
  bool allocateClassRegistry();
  void releaseClassRegistry();
  EspUsbDeviceClass *classAt(size_t index) const;
  std::atomic<bool> running_{false};
  bool ready_ = false;
  bool usbStarted_ = false;
  esp_err_t lastError_ = ESP_OK;
  uint8_t deviceDescriptor_[18] = {};
  // Exact-sized, independently selected descriptor storage.
  uint8_t *configDescriptor_ = nullptr;
  uint8_t *configDescriptorHighSpeed_ = nullptr;
  uint8_t *otherSpeedDescriptor_ = nullptr;
  uint8_t deviceQualifierDescriptor_[10] = {};
  uint8_t *bosDescriptor_ = nullptr;
  uint8_t *microsoftOs20Descriptor_ = nullptr;
  uint8_t *hidReportDescriptor_ = nullptr;
  uint16_t configDescriptorLength_ = 0, highSpeedDescriptorLength_ = 0;
  uint16_t bosDescriptorLength_ = 0;
  uint16_t microsoftOs20DescriptorLength_ = 0;
  uint16_t microsoftOs20VendorRevision_ = 0;
  uint16_t bulkInDoubleBuffered_ = 0;
  uint16_t hidInterfacesLength_ = 0;
  uint8_t hidInterfaceCount_ = 0;
  uint16_t hidReportDescriptorLength_ = 0;
  // One exact-sized block: word offsets followed by immutable UTF-16 strings.
  uint16_t *stringDescriptors_ = nullptr;
  uint16_t stringDescriptorSlots_ = 0;
  uint8_t *webUsbUrlDescriptor_ = nullptr;
  ControlRequestObserver controlObserver_;
  uint8_t controlResponseRequest_ = 0;
  uint16_t controlResponseValue_ = 0;
  uint16_t controlResponseIndex_ = 0;
  uint16_t controlResponseLength_ = 0;
};

class EspUsbDeviceClass
{
public:
  EspUsbDeviceClass(const EspUsbDeviceClass &) = delete;
  EspUsbDeviceClass &operator=(const EspUsbDeviceClass &) = delete;
  EspUsbDeviceClass(EspUsbDeviceClass &&) = delete;
  EspUsbDeviceClass &operator=(EspUsbDeviceClass &&) = delete;
  virtual ~EspUsbDeviceClass();
  virtual bool begin() { return true; }
  virtual bool afterDeviceStarted() { return true; }
  virtual esp_err_t startError() const { return ESP_FAIL; }
  virtual void end() {}
  virtual void prepareToStop() {}
  virtual bool isHid() const { return true; }
  virtual bool isCdc() const { return false; }
  virtual bool isMidi() const { return false; }
  virtual bool isMsc() const { return false; }
  virtual bool isVendor() const { return false; }
  virtual bool isAudio() const { return false; }
  virtual bool isVideo() const { return false; }
  virtual bool isNet() const { return false; }
  virtual bool isDfu() const { return false; }
  // Required v3 writer contract: nullptr measures the required byte count;
  // it must not write. The size must stay stable until configuration changes.
  virtual uint16_t configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize) = 0;
  // nullptr/0 queries the size; non-null writers must respect capacity.
  virtual uint16_t configurationDescriptorForSpeed(
      uint8_t *dst, size_t capacity, uint8_t interfaceNumber,
      uint8_t endpointNumber, bool highSpeed);
  virtual uint8_t interfaceCount() const = 0;
  // Span of endpoint numbers reserved for this Function, not the number of
  // directional endpoint addresses. IN and OUT may share one number.
  virtual uint8_t endpointCount() const = 0;
  // Name to publish for this function, or null for none. A composite device
  // that repeats one class needs it: without a string the host has nothing to
  // tell two identical functions apart. UTF-8 is copied during begin(); the
  // returned pointer must remain valid until begin() returns.
  virtual const char *functionName() const { return nullptr; }
  // Optional class-specific strings (e.g. an NCM MAC address), separate from
  // functionName(). Keep count/text stable through begin(); the core snapshots
  // them as UTF-16. A declared string must be non-null. No storage when unused.
  virtual size_t auxiliaryStringCount() const { return 0; }
  virtual const char *auxiliaryString(size_t) const { return nullptr; }
  // A contiguous range after all function names; first=0 when count=0.
  virtual void assignAuxiliaryStringIds(uint8_t first) { (void)first; }
  // Called by buildDescriptors() before asking for the descriptor, so a class
  // can emit and address itself correctly.
  //
  // instance is this function's logical index among functions of the same
  // class in descriptor order. Legacy adapters may map it to their stack slot;
  // independent functions dispatch their endpoints by interface identity.
  //
  // stringIndex is the string descriptor index reserved for functionName(), or
  // 0 when the class did not ask for a name.
  virtual void assignFunctionIds(uint8_t instance, uint8_t stringIndex)
  {
    (void)instance;
    (void)stringIndex;
  }
  // Preferred ID. Repeated HID types receive unique IDs in a composite.
  virtual uint8_t hidReportId() const { return 0; }
  // Assigned composite identity from the last descriptor build; 0 after end
  // or for a standalone HID. Normal sendReport/sendInput choose it for you.
  uint8_t compositeReportId() const { return assignedReportId_; }
  virtual const uint8_t *hidReportDescriptor() const { return nullptr; }
  virtual uint16_t hidReportDescriptorLength() const { return 0; }
  virtual bool sendHidInput(uint8_t, const void *, size_t, uint32_t) { return false; }
  // Preferred IN endpoint wMaxPacketSize for this HID class, or 0 for "no
  // preference" (use the default). NKRO keyboards return a larger size so their
  // multi-byte bitmap report fits one packet. This is a bus packet preference,
  // independent of the complete report's runtime storage.
  virtual uint16_t hidInEndpointSize() const { return 0; }
  // wMaxPacketSize this HID function wants for its interrupt endpoints in the
  // high-speed configuration, or 0 to keep the full-speed value there too.
  //
  // Full speed caps an interrupt endpoint at 64 bytes and high speed at 1024, so
  // the two configurations genuinely differ for a class that moves more than one
  // report's worth per interval - which, of the classes here, is only
  // EspUsbDeviceHidVendor. Everything else sends 8 or 16 bytes and returns 0.
  virtual uint16_t hidHighSpeedEndpointSize() const { return 0; }
  virtual void onHidSetReport(uint8_t reportId, uint8_t reportType, const uint8_t *data, uint16_t length) {}
  virtual void onHidSetProtocol(uint8_t protocol) { (void)protocol; }
  // Bus state changed, so anything the class believes the host currently knows is
  // no longer true and cached host-facing state must be dropped.
  //
  // Both hooks are needed. onBusDetached() runs on SET_CONFIGURATION 0, deinit,
  // and - only on boards that sense VBUS - unplug. A bare bus reset does not
  // reach it, so replugging a board without VBUS sensing goes straight to
  // re-enumeration; onBusAttached() (SET_CONFIGURATION n) is what always fires
  // and closes that gap. At mount the host knows nothing is held and has set no
  // LEDs, so clearing there is correct either way.
  virtual void onBusAttached() {}
  virtual void onBusDetached() {}
  virtual bool onVendorControlRequest(uint8_t, uint8_t, const void *) { return false; }

protected:
  friend class EspUsbDevice;
  explicit EspUsbDeviceClass(EspUsbDevice &device);
  // Worst negotiated bulk packet for the selected controller, before startup.
  uint16_t maximumBulkPacketSize() const;
  EspUsbDevice &device_;
  // This class's slot in EspUsbDevice::classes_, i.e. its registration order.
  // Logical Function index, mapped through classReportId() and
  // classRuntimeInstance() to the selected HID driver.
  uint8_t hidInstance_ = UINT8_MAX;
  EspUsbDeviceClass *nextRegistered_ = nullptr;
  bool registered_ = false, registrationRejected_ = false;
  uint8_t assignedReportId_ = 0, descriptorInterfaceNumber_ = 0xff;
};

struct EspUsbHidState;
// Common runtime for HID Functions. A Device's merged HID shares one transport;
// report descriptor sizes select storage, without TinyUSB class buffers.
class EspUsbHidFunction : public EspUsbDeviceClass
{
public:
  explicit EspUsbHidFunction(EspUsbDevice &device);
  ~EspUsbHidFunction() override;
  bool begin() override;
  void prepareToStop() override;
  void end() override;
  esp_err_t startError() const override { return hidStartError_; }
  bool sendHidInput(uint8_t reportId, const void *, size_t, uint32_t timeoutMs) override;
  // Set current Feature state without sending an interrupt report. Payload
  // excludes the report ID; 0 selects this Function's unambiguous default ID.
  // Valid after begin(), including before the host configures the interface.
  esp_err_t setFeatureReport(const void *data, size_t length, uint8_t reportId = 0);
  // Shared interface capacities, including a report ID byte when declared.
  size_t inputReportCapacity() const;
  size_t outputReportCapacity() const;
  size_t featureReportCapacity() const;
protected:
  bool hidStarted() const;
  bool failHidBegin(esp_err_t error) { hidStartError_ = error; return false; }
private:
  EspUsbHidState *hidState_ = nullptr;
  esp_err_t hidStartError_ = ESP_OK;
};

// Per-call submission error. Rejection never borrows a buffer or notifies.
enum class EspUsbTransferError : uint8_t
{
  None = 0,
  // The selected receive mode or transport does not support this operation.
  NotSupported,
  // No device, or this interface's endpoints are not open yet.
  NotMounted,
  // Invalid buffer/length, including a Function's format or capacity limit.
  BadArgument,
  // The buffer's address is not 64-byte aligned. Its *length* need not be.
  NotAligned,
  // The buffer is not in DMA-capable memory (PSRAM, for instance).
  NotDmaCapable,
  // A transfer is already in flight on this endpoint. Ordinary backpressure.
  Busy,
  // The controller refused the transfer.
  TransferFailed,
};

struct EspUsbTransferSubmission
{
  EspUsbTransferError error = EspUsbTransferError::None;
  uint64_t id = 0; // Nonzero only for an accepted loan; survives begin/end.
  explicit operator bool() const { return error == EspUsbTransferError::None; }
};

// Capacities belong to this instance. Configure while stopped; begin allocates
// the selected resources and end releases them. No TinyUSB class-size flags.
enum class EspUsbBulkRxMode : uint8_t { Stream, Callback, Borrowed, Disabled };
enum class EspUsbTransferTermination : uint8_t { Continue, EndWithShortPacket };

enum class EspUsbStreamTermination : uint8_t { FunctionDefault, Continue, EndWithShortPacket };

struct EspUsbBulkStreamConfig
{
  size_t rxCapacity = 512;
  size_t txCapacity = 512;
  // Optional non-DMA ring storage. Capacities describe the provided regions;
  // keep them alive from begin through end. Null selects automatic allocation.
  uint8_t *rxBuffer = nullptr;
  uint8_t *txBuffer = nullptr;
  uint16_t transferBytes = 512;
  bool autoFlush = true;
  EspUsbStreamTermination streamTermination = EspUsbStreamTermination::FunctionDefault;
  EspUsbBulkRxMode rxMode = EspUsbBulkRxMode::Stream;
};

using EspUsbVendorConfig = EspUsbBulkStreamConfig;
using EspUsbVendorRxMode = EspUsbBulkRxMode;
struct EspUsbBulkState;
enum class EspUsbTransferStatus : uint8_t { Success, BusReset, Stopped, TransferError, Cancelled };

struct EspUsbTransferResult
{
  const void *buffer = nullptr;
  size_t requestedBytes = 0;
  size_t actualBytes = 0;
  EspUsbTransferStatus status = EspUsbTransferStatus::Success;
  // On reset/stop the controller may have sent an unknown prefix.
  bool actualBytesKnown = true;
  uint64_t id = 0;
};
class EspUsbDeviceBulkStream : public EspUsbDeviceClass
{
public:
  using RxCallback = std::function<void(size_t)>;
  using TxCompleteCallback = std::function<void(size_t)>;
  using RxDataCallback = std::function<void(const uint8_t *, size_t)>;
  using DirectCompleteCallback = std::function<void(const EspUsbTransferResult &)>;

  ~EspUsbDeviceBulkStream() override;
  esp_err_t configure(const EspUsbBulkStreamConfig &config);
  const EspUsbBulkStreamConfig &configuration() const { return config_; }
  bool begin() override;
  void end() override;
  bool isHid() const override { return false; }
  void prepareToStop() override;

  bool mounted() const;
  int available();
  int read();
  int peek();
  size_t read(uint8_t *buffer, size_t size);
  virtual size_t write(uint8_t data);
  // Copies accepted bytes. Automatic dispatch by default, never a completion wait.
  virtual size_t write(const uint8_t *buffer, size_t size);
  // Shared implementation for the CDC/Vendor Arduino Print facades.
  int availableForWrite();
  size_t writeAvailable() const;
  // Effective allocated capacities; zero before begin and after end.
  size_t writeCapacity() const;
  size_t readCapacity() const;
  bool waitWritable(size_t bytes, uint32_t timeoutMs = 100);
  // Starts draining pending copied data, also when autoFlush is disabled.
  void flush();

  // Strict borrowed TX: no hidden copy, 64-byte aligned DMA memory, bounded by
  // maxDirectTransfer() on the negotiated link. Keep it unchanged until the
  // typed completion, including BusReset/Stopped. Rejection borrows nothing.
  // Register the completion callback while stopped. Runs on the USB worker;
  // it may submit another prepared buffer but must not block or call end().
  static bool directWriteSupported();
  size_t maxDirectTransfer() const;
  EspUsbTransferSubmission writeDirect(const void *buffer, size_t length,
                   EspUsbTransferTermination termination = EspUsbTransferTermination::Continue);
  esp_err_t onDirectTxComplete(DirectCompleteCallback callback);
  // Borrowed RX mode: capacity is a multiple of MPS and 64, the entire region
  // is exclusively DMA-owned until completion. Short/ZLP may return less data.
  EspUsbTransferSubmission readDirect(void *buffer, size_t capacity);
  size_t maxDirectReceive() const;
  esp_err_t onDirectRxComplete(DirectCompleteCallback callback);
  // Request cancellation; the typed completion returns ownership later.
  // A timeout or successful request alone never permits freeing the buffer.
  // id=0 selects the current loan. A nonzero id cannot cancel a newer loan.
  esp_err_t cancelDirectTx(uint64_t id = 0);
  esp_err_t cancelDirectRx(uint64_t id = 0);
  esp_err_t onTxComplete(TxCompleteCallback callback);
  // Callback RX mode requires rxCapacity=0. Packet data is borrowed only for
  // this callback; copy what must outlive it. Set callbacks while stopped.
  esp_err_t onRxData(RxDataCallback callback);
  esp_err_t onRx(RxCallback callback);
  uint16_t endpointSize() const { return endpointSize_; }
  void handleRx(const uint8_t *data, size_t length);
  void handleTxComplete(size_t sentBytes);

protected:
  explicit EspUsbDeviceBulkStream(EspUsbDevice &device, uint16_t endpointSize = 64);
  // Protocol framing hooks. Called under the transport lock; never invoke user
  // callbacks here. The filter may compact valid bytes, returning at most n.
  virtual size_t bulkPacketUnit() const { return 1; }
  virtual size_t filterBulkRx(uint8_t *, size_t n) { return n; }
  virtual bool validateBulkTx(const uint8_t *, size_t) const { return true; }
  size_t copyTransferCapacity() const;
  bool txPending() const;
  virtual uint16_t openBulkInterface(uint8_t port, const uint8_t *descriptor, uint16_t length,
      uint8_t &in, uint8_t &out, uint16_t &mps) = 0;
  bool openBulkPair(uint8_t port, const uint8_t *endpoints, uint8_t &in, uint8_t &out, uint16_t &mps);
  virtual bool onBulkControl(uint8_t port, uint8_t stage, const void *request) {
    return onVendorControlRequest(port, stage, request);
  }
  virtual bool defaultStreamTermination() const { return false; }
  virtual void onBulkReset() {}
  virtual bool onBulkExtraCompletion(uint8_t, uint8_t, uint32_t) { return false; }
  friend struct EspUsbBulkState;
  esp_err_t startError() const override { return startError_; }
  esp_err_t startError_ = ESP_OK;
  EspUsbBulkStreamConfig config_;
  EspUsbBulkState *state_ = nullptr;
  uint16_t endpointSize_ = 64;
  uint8_t interfaceNumber_ = 0;
  RxCallback rxCallback_;
  RxDataCallback rxDataCallback_;
  TxCompleteCallback txCompleteCallback_;
  DirectCompleteCallback directTxCompleteCallback_;
  DirectCompleteCallback directRxCompleteCallback_;
  uint64_t nextTransferId_ = 0;
};

class EspUsbDeviceVendor : public EspUsbDeviceBulkStream, public Print
{
public:
  using Print::write;
  size_t write(uint8_t value) override { return EspUsbDeviceBulkStream::write(value); }
  size_t write(const uint8_t *buffer, size_t size) override { return EspUsbDeviceBulkStream::write(buffer, size); }
  int availableForWrite() override { return EspUsbDeviceBulkStream::availableForWrite(); }
  void flush() override { EspUsbDeviceBulkStream::flush(); }
  using ControlRequestCallback = std::function<bool(const EspUsbDeviceVendorControlRequest &)>;
  explicit EspUsbDeviceVendor(EspUsbDevice &device, uint16_t endpointSize = 64);
  ~EspUsbDeviceVendor() override;
  bool isVendor() const override { return true; }
  uint8_t interfaceCount() const override { return 1; }
  uint8_t endpointCount() const override { return 1; }
  uint16_t configurationDescriptor(uint8_t *, uint8_t, uint8_t, uint16_t) override;
  bool onVendorControlRequest(uint8_t port, uint8_t stage, const void *request) override;
  esp_err_t onControlRequest(ControlRequestCallback callback);
  bool sendControlResponse(const EspUsbDeviceVendorControlRequest &, const void *data = nullptr, size_t length = 0);
  bool handleControlRequest(uint8_t port, uint8_t stage, const void *request);
protected:
  uint16_t openBulkInterface(uint8_t, const uint8_t *, uint16_t, uint8_t &, uint8_t &, uint16_t &) override;
private:
  ControlRequestCallback controlRequestCallback_;
};

class EspUsbDeviceCdcSerial : public EspUsbDeviceBulkStream, public Stream
{
public:
  using Print::write;
  size_t write(uint8_t value) override { return EspUsbDeviceBulkStream::write(value); }
  // Copies all bytes when possible, waiting up to the TX timeout for room.
  // Worker calls stay nonblocking; ISR calls accept nothing.
  size_t write(const uint8_t *buffer, size_t size) override;
  int availableForWrite() override { return EspUsbDeviceBulkStream::availableForWrite(); }
  // Starts copied TX and waits for data/ZLP completion within the TX timeout.
  void flush() override;
  void setTxTimeoutMs(uint32_t timeout) { txTimeoutMs_.store(timeout, std::memory_order_relaxed); }
  uint32_t txTimeoutMs() const { return txTimeoutMs_.load(std::memory_order_relaxed); }
  using EspUsbDeviceBulkStream::read;
  int available() override { return EspUsbDeviceBulkStream::available(); }
  int read() override { return EspUsbDeviceBulkStream::read(); }
  int peek() override { return EspUsbDeviceBulkStream::peek(); }
  using BreakCallback = std::function<void(uint16_t)>;
  using LineCodingCallback = std::function<void(const EspUsbDeviceCdcLineCoding &)>;
  using LineStateCallback = std::function<void(const EspUsbDeviceCdcLineState &)>;
  explicit EspUsbDeviceCdcSerial(EspUsbDevice &device, const char *name = nullptr);
  ~EspUsbDeviceCdcSerial() override;
  bool begin() override;
  void end() override;
  bool isCdc() const override { return true; }
  uint8_t interfaceCount() const override { return 2; }
  uint8_t endpointCount() const override { return 2; }
  uint16_t configurationDescriptor(uint8_t *, uint8_t, uint8_t, uint16_t) override;
  const char *functionName() const override { return name_; }
  void assignFunctionIds(uint8_t instance, uint8_t stringIndex) override;
  // Index in CDC descriptor order, assigned at begin; independent of TinyUSB.
  uint8_t port() const { return instance_; }
  esp_err_t setName(const char *name);
  bool connected() const;
  EspUsbDeviceCdcLineCoding lineCoding() const;
  EspUsbDeviceCdcLineState lineState() const;
  esp_err_t onLineCoding(LineCodingCallback callback);
  esp_err_t onLineState(LineStateCallback callback);
  esp_err_t onBreak(BreakCallback callback);
protected:
  uint16_t openBulkInterface(uint8_t, const uint8_t *, uint16_t, uint8_t &, uint8_t &, uint16_t &) override;
  bool onBulkControl(uint8_t port, uint8_t stage, const void *request) override;
  bool defaultStreamTermination() const override { return true; }
  void onBulkReset() override;
  bool onBulkExtraCompletion(uint8_t endpoint, uint8_t result, uint32_t bytes) override;
private:
  std::atomic<uint32_t> txTimeoutMs_{1000}, txEpoch_{0};
  const char *name_ = nullptr;
  uint8_t instance_ = 0, stringIndex_ = 0, notifyEndpoint_ = 0;
  uint8_t *controlBuffer_ = nullptr;
  EspUsbDeviceCdcLineCoding lineCoding_{115200, 0, 0, 8};
  EspUsbDeviceCdcLineState lineState_;
  BreakCallback breakCallback_;
  LineCodingCallback lineCodingCallback_;
  LineStateCallback lineStateCallback_;
};

struct EspUsbNetConfig {
  size_t ntbInBytes = 3200, ntbOutBytes = 3200;
  size_t txBuffers = 3, rxBuffers = 2;
  size_t maxDatagrams = 8;
  uint16_t maxFrameBytes = 1514;
  uint8_t *txBuffer = nullptr, *rxBuffer = nullptr;
  size_t txBufferBytes = 0, rxBufferBytes = 0;
  const char *name = nullptr;
};
struct EspUsbNetStats {
  uint32_t txFrames = 0, rxFrames = 0, invalidNtbs = 0;
};
struct EspUsbNetState;
struct EspUsbNetifGlue;

// CDC-NCM (USB network / "USB Ethernet") device function. Presents the device
// to a USB host as a network adapter. Phase 1 exposes raw Ethernet frames via
// onFrame() / sendFrame(); lwIP integration (a netif + DHCP server) is layered
// on top separately.
class EspUsbDeviceNet : public EspUsbDeviceClass
{
public:
  // A complete frame on the USB worker; data is valid until callback returns.
  using FrameCallback = std::function<void(const uint8_t *, size_t)>;

  explicit EspUsbDeviceNet(EspUsbDevice &device);
  ~EspUsbDeviceNet() override;

  esp_err_t configure(const EspUsbNetConfig &config);
  const EspUsbNetConfig &configuration() const { return netConfig_; }
  bool begin() override;
  esp_err_t startError() const override { return startError_; }
  void prepareToStop() override;
  void end() override;
  bool isHid() const override { return false; }
  bool isNet() const override { return true; }
  uint16_t configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize) override;
  uint8_t interfaceCount() const override { return 2; }         // CDC control + data
  uint8_t endpointCount() const override { return 2; }          // notif IN + bulk duplex

  // Set the device's 6-byte MAC address. Call before EspUsbDevice::begin().
  // Defaults to a locally-administered address derived per Function.
  esp_err_t macAddress(const uint8_t mac[6]);
  const uint8_t *macAddress() const;

  esp_err_t onFrame(FrameCallback callback);
  // Send one Ethernet frame to the host. Returns false if the link cannot
  // currently accept a frame of this size (caller may retry).
  bool sendFrame(const uint8_t *data, size_t length);
  bool linkUp() const;
  size_t ntbInCapacity() const;
  size_t ntbOutCapacity() const;
  size_t txBufferCount() const;
  size_t rxBufferCount() const;
  EspUsbNetStats stats() const;
  const char *functionName() const override { return netConfig_.name; }
  void assignFunctionIds(uint8_t instance, uint8_t nameIndex) override;
  size_t auxiliaryStringCount() const override { return 1; }
  const char *auxiliaryString(size_t index) const override { return index ? nullptr : macString_; }
  void assignAuxiliaryStringIds(uint8_t first) override { macIndex_ = first; }

  // --- Optional lwIP / esp_netif integration (Phase 2) ---
  // Configure the interface's IPv4 address, gateway, and subnet mask. Used for a
  // static address and as the DHCP server's own address/pool base. Defaults to
  // 192.168.7.1 / 192.168.7.1 / 255.255.255.0. Call before beginNetwork().
  esp_err_t ipConfig(IPAddress local, IPAddress gateway, IPAddress subnet);
  // Run a DHCP server so the USB host is handed an address automatically. OFF by
  // default (never enabled unless requested). Mutually exclusive with dhcpClient.
  esp_err_t dhcpServer(bool enable);
  // Act as a DHCP client (get our address from the network, e.g. when the PC
  // bridges the USB NIC onto its LAN). OFF by default. Mutually exclusive with
  // dhcpServer. When neither is enabled the static ipConfig() address is used.
  esp_err_t dhcpClient(bool enable);
  // By default the DHCP server does NOT advertise itself as a gateway or DNS —
  // the device is a local endpoint, and advertising a non-forwarding gateway as
  // the host's default route would black-hole its off-link traffic. Enable these
  // only if the device actually forwards (e.g. bridged to another uplink) or the
  // advertised DNS is reachable. Call before beginNetwork().
  esp_err_t dhcpAdvertiseGateway(bool enable);  // offer the ipConfig() gateway as the DHCP router (option 3)
  esp_err_t dhcpDns(IPAddress dns);             // offer this DNS server (option 6); 0.0.0.0 = do not offer
  // Make this interface the ESP's own default route (esp_netif default netif).
  // OFF by default: the USB netif uses a low route priority so a coexisting Wi-Fi
  // STA (priority 100) stays the default and the USB link never hijacks the ESP's
  // outbound traffic. Enable only when the ESP should reach the internet *through*
  // the USB host (e.g. a PC that bridges/NATs to it, with dhcpClient(true)). Call
  // before beginNetwork().
  esp_err_t defaultRoute(bool enable);
  // Bring up the esp_netif/lwIP interface bound to this NCM function. Call once
  // after EspUsbDevice::begin(). If never called, the class stays a raw-frame
  // transport (onFrame/sendFrame) with no IP stack — useful for host-side bridging.
  bool beginNetwork();
  bool endNetwork();
  bool networkUp() const;
  IPAddress localIP() const;

private:
  friend struct EspUsbNetState;
  friend struct EspUsbNetifGlue;
  void handleRecv(const uint8_t *src, uint16_t size);
  void refreshMacString();
  EspUsbNetConfig netConfig_;
  EspUsbNetState *state_ = nullptr;
  esp_err_t startError_ = ESP_OK;
  uint8_t interface_ = 255, nameIndex_ = 0, macIndex_ = 0;
  uint8_t mac_[6] = {};
  uint16_t defaultMacSuffix_ = 0;
  bool macUserSet_ = false;
  char macString_[13] = {};
  FrameCallback frameCallback_;
  void *netif_ = nullptr, *networkContext_ = nullptr, *networkDriver_ = nullptr;
  void (*networkReceive_)(void *, const uint8_t *, size_t) = nullptr;
  void (*networkStop_)(EspUsbDeviceNet &) = nullptr;
  std::atomic<unsigned> networkUsers_{0};
  bool netStarted_ = false;
  bool dhcpServer_ = false, dhcpClient_ = false;
  bool dhcpAdvertiseGateway_ = false, defaultRoute_ = false;
  uint32_t dhcpDns_ = 0, cfgIp_ = 0, cfgGateway_ = 0, cfgNetmask_ = 0;
};

// USB CCID smart card reader function. The device side of what EspUsbHost's
// ccid* API drives: it presents one bulk CCID slot (bInterfaceClass 0x0b,
// subclass 0, protocol 0) and answers the PC_to_RDR messages itself. The card
// behind the slot is the sketch's: insertCard() decides whether a card is there
// and what ATR it answers with, and onApdu() answers the exchanges.
//
// Scope: one slot, T=1, short APDU level exchange. Chaining, PIN pad / secure
// entry, and clock / data-rate negotiation are out - the device declares it
// supports none of them, so a conforming host never asks.
class EspUsbDeviceCcid : public EspUsbDeviceClass
{
public:
  // Answer one exchange. Write the response into `response` (at most `capacity`
  // bytes) and return its length; return 0 to fail the command, which the host
  // sees as command status FAILED with bError = ICC_MUTE.
  //
  // For onApdu() the buffers are the APDU and its answer including SW1SW2 - the
  // device does not add or interpret a status word. Runs in the USB worker
  // task: no blocking, and no USB calls back into this object.
  using ApduCallback = std::function<size_t(const uint8_t *apdu, size_t length,
                                            uint8_t *response, size_t capacity)>;
  // PC_to_RDR_Escape payload in, RDR_to_PC_Escape payload out. Same contract.
  using EscapeCallback = ApduCallback;
  // The host activated (true) or deactivated (false) the card in the slot.
  using PowerCallback = std::function<void(bool on)>;

  explicit EspUsbDeviceCcid(EspUsbDevice &device);
  ~EspUsbDeviceCcid() override;

  esp_err_t configure(const EspUsbCcidConfig &config);
  const EspUsbCcidConfig &configuration() const { return config_; }
  size_t messageCapacity() const;
  esp_err_t startError() const override { return startError_; }
  void prepareToStop() override;
  const char *functionName() const override { return config_.name; }
  void assignFunctionIds(uint8_t, uint8_t stringIndex) override { stringIndex_ = stringIndex; }
  bool begin() override;
  void end() override;
  bool isHid() const override { return false; }
  uint16_t configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize) override;
  uint8_t interfaceCount() const override { return 1; }
  uint8_t endpointCount() const override { return config_.removable ? 2 : 1; } // bulk duplex + optional interrupt IN
  void onBusAttached() override;
  void onBusDetached() override;

  // --- The card in the slot ---
  // Put a card in the slot and set the ATR IccPowerOn answers with. `length`
  // must be 2..ESP_USB_DEVICE_CCID_MAX_ATR. Notifies the host over the
  // interrupt endpoint when the device is mounted. Call from the sketch task.
  bool insertCard(const uint8_t *atr, size_t length);
  esp_err_t removeCard();
  bool cardPresent() const;
  // True once the host has activated the card with IccPowerOn.
  bool cardPowered() const;
  size_t atr(uint8_t *buffer, size_t capacity) const;

  esp_err_t onApdu(ApduCallback callback);
  esp_err_t onEscape(EscapeCallback callback);
  esp_err_t onPower(PowerCallback callback);

  // True while the host has the CCID interface configured.
  bool mounted() const;
  // PC_to_RDR messages answered, XfrBlock exchanges answered, and the message
  // type of the last command. Diagnostics for tests and examples.
  uint32_t commandCount() const;
  uint32_t apduCount() const;
  uint8_t lastMessageType() const;

private:
  // Internal: a complete PC_to_RDR message arrived (USB worker).
  void handleMessage(const uint8_t *message, size_t length);
  // Internal: a message longer than the device's buffer arrived and was dropped.
  void handleOverrun(uint8_t messageType, uint8_t slot, uint8_t sequence);
  // Internal: CCID class request ABORT for this slot / sequence.
  void handleAbort(uint8_t slot, uint8_t sequence);
  // Internal: the interrupt IN endpoint finished a slot change notification.
  void handleNotifyComplete();

private:
  friend struct EspUsbCcidState;
  EspUsbCcidConfig config_;
  EspUsbCcidState *state_ = nullptr;
  esp_err_t startError_ = ESP_OK;
  uint8_t interfaceNumber_ = 255, stringIndex_ = 0;
  size_t writeHeader(uint8_t messageType, uint32_t dataLength, uint8_t slot,
                     uint8_t sequence, uint8_t iccStatus, uint8_t commandStatus,
                     uint8_t error, uint8_t parameter);
  void sendStatus(uint8_t messageType, uint8_t slot, uint8_t sequence,
                  uint8_t commandStatus, uint8_t error, uint8_t parameter);
  void sendData(uint8_t messageType, uint8_t slot, uint8_t sequence,
                const uint8_t *data, size_t length, uint8_t parameter);
  uint8_t iccStatus() const;
  void notifySlotState();

  ApduCallback apduCallback_;
  EscapeCallback escapeCallback_;
  PowerCallback powerCallback_;

  uint8_t atr_[ESP_USB_DEVICE_CCID_MAX_ATR] = {};
  uint8_t atrLength_ = 0;
  std::atomic<bool> cardPresent_{false};
  std::atomic<bool> cardPowered_{false};
  // A slot change happened while the interrupt endpoint still had an unpolled
  // notification on it, so the host has not been told the current state yet.
  std::atomic<bool> notifyPending_{false};
  bool aborted_ = false; // an ABORT request is pending for the current slot
  uint8_t abortSequence_ = 0;

  uint8_t *response_ = nullptr;
  std::atomic<uint32_t> commandCount_{0};
  std::atomic<uint32_t> apduCount_{0};
  std::atomic<uint8_t> lastMessageType_{0};
};

enum class EspUsbMidiRxMode : uint8_t { Queue, Callback, Disabled };
struct EspUsbMidiConfig
{
  uint8_t inCables = 1, outCables = 1;
  const char *name = nullptr;
  size_t rxPackets = 128;
  size_t txPackets = 128;
  // 0 chooses one bus packet for the selected controller. A nonzero value
  // selects complete MIDI events per copied submission, including values < MPS.
  size_t transferPackets = 0;
  EspUsbMidiRxMode rxMode = EspUsbMidiRxMode::Queue;
  bool autoFlush = true;
  EspUsbStreamTermination streamTermination = EspUsbStreamTermination::FunctionDefault;
  EspUsbDeviceMidiPacket *rxBuffer = nullptr;
  EspUsbDeviceMidiPacket *txBuffer = nullptr;
};

class EspUsbDeviceMidi : public EspUsbDeviceBulkStream
{
public:
  // USB MIDI 1.0 addresses cables with a 4-bit field, so one interface carries
  // at most 16.
  static constexpr uint8_t MAX_CABLES = 16;

  // cableCount must be in 1..MAX_CABLES. Each cable appears to the host as a
  // separate MIDI port; they all share one pair of bulk endpoints, multiplexed
  // by the cable number in each packet header.
  explicit EspUsbDeviceMidi(EspUsbDevice &device, uint8_t cableCount = 1);
  // The two directions need not have the same number of cables. Both are named
  // from the host's point of view, as USB endpoint directions and EspUsbHost's
  // EspUsbHostMidiPortInfo are: IN is device to host (what this device sends),
  // OUT is host to device (what it receives). Beware that the MIDI class document
  // names embedded jacks the other way round, from the device's side.
  EspUsbDeviceMidi(EspUsbDevice &device, uint8_t inCableCount, uint8_t outCableCount);
  ~EspUsbDeviceMidi() override;

  bool begin() override;
  using EspUsbDeviceBulkStream::startError;
  esp_err_t configure(const EspUsbMidiConfig &config);
  const EspUsbMidiConfig &configuration() const { return midiConfig_; }
  using PacketCallback = std::function<void(const EspUsbDeviceMidiPacket &)>;
  esp_err_t onPacket(PacketCallback callback);
  size_t availablePackets();
  size_t availableForWritePackets() const;
  size_t receiveCapacity() const;
  size_t sendCapacity() const;
  size_t transferCapacity() const;
  uint32_t receiveErrors() const { return receiveErrors_.load(std::memory_order_relaxed); }
  bool isHid() const override { return false; }
  bool isMidi() const override { return true; }
  uint16_t configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize) override;
  uint16_t configurationDescriptorForSpeed(
      uint8_t *dst, size_t capacity, uint8_t interfaceNumber,
      uint8_t endpointNumber, bool highSpeed) override;
  // Cables multiplex one bulk pair, so neither count depends on cableCount().
  uint8_t interfaceCount() const override { return 2; }
  uint8_t endpointCount() const override { return 1; }

  // Cables this device sends on / receives on, host-view as above.
  uint8_t inCableCount() const { return midiConfig_.inCables; }
  uint8_t outCableCount() const { return midiConfig_.outCables; }
  const char *functionName() const override { return midiConfig_.name; }
  void assignFunctionIds(uint8_t, uint8_t stringIndex) override { stringIndex_ = stringIndex; }
  // Bytes configurationDescriptor() will write, so callers can size a buffer
  // before asking for the descriptor.
  uint16_t descriptorLength() const;

  bool readPacket(EspUsbDeviceMidiPacket &packet);
  bool writePacket(const EspUsbDeviceMidiPacket &packet);
  // cable is 0-based (0..inCableCount()-1, since these all send) and matches the
  // high nibble of EspUsbDeviceMidiPacket::header. Sending to a cable the host was
  // never told about fails instead of silently landing on another port.
  bool noteOn(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t cable = 0);
  bool noteOff(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t cable = 0);
  bool controlChange(uint8_t channel, uint8_t control, uint8_t value, uint8_t cable = 0);
  bool programChange(uint8_t channel, uint8_t program, uint8_t cable = 0);
  bool polyPressure(uint8_t channel, uint8_t note, uint8_t pressure, uint8_t cable = 0);
  bool channelPressure(uint8_t channel, uint8_t pressure, uint8_t cable = 0);
  bool pitchBend(uint8_t channel, uint16_t value, uint8_t cable = 0);

private:
  // The public MIDI API deals in complete events. The shared byte transport
  // remains an implementation detail. MIDI is neither Print nor Stream.
  size_t write(uint8_t) override { return 0; }
  size_t write(const uint8_t *, size_t) override;
  using EspUsbDeviceBulkStream::read;
  using EspUsbDeviceBulkStream::peek;
  using EspUsbDeviceBulkStream::availableForWrite;
  using EspUsbDeviceBulkStream::available;
  using EspUsbDeviceBulkStream::writeAvailable;
  using EspUsbDeviceBulkStream::readCapacity;
  using EspUsbDeviceBulkStream::writeCapacity;
  using EspUsbDeviceBulkStream::waitWritable;
  using EspUsbDeviceBulkStream::onRx;
  using EspUsbDeviceBulkStream::onRxData;
  using EspUsbDeviceBulkStream::onTxComplete;
  using EspUsbDeviceBulkStream::writeDirect;
  using EspUsbDeviceBulkStream::readDirect;
  using EspUsbDeviceBulkStream::onDirectTxComplete;
  using EspUsbDeviceBulkStream::onDirectRxComplete;
  using EspUsbDeviceBulkStream::cancelDirectTx;
  using EspUsbDeviceBulkStream::cancelDirectRx;
  size_t bulkPacketUnit() const override { return 4; }
  size_t filterBulkRx(uint8_t *, size_t) override;
  bool validateBulkTx(const uint8_t *, size_t) const override;
  bool defaultStreamTermination() const override { return true; }
  uint16_t openBulkInterface(uint8_t, const uint8_t *, uint16_t,
      uint8_t &, uint8_t &, uint16_t &) override;
  EspUsbMidiConfig midiConfig_;
  PacketCallback packetCallback_;
  std::atomic<uint32_t> receiveErrors_{0};
  static uint8_t status(uint8_t codeIndex, uint8_t channel);
  static uint8_t clamp7(uint8_t value);
  static uint8_t header(uint8_t codeIndex, uint8_t cable);

  uint8_t stringIndex_ = 0;
};

struct EspUsbAudioFormat
{
  uint32_t sampleRate = 48000;
  uint8_t channels = 2;
  uint8_t bytesPerSample = 2;
  uint8_t bitsPerSample = 16;
};

enum class EspUsbAudioDirection : uint8_t
{
  Playback,
  Capture,
};

enum class EspUsbAudioProtocol : uint8_t
{
  Uac1,
  Uac2,
};

enum class EspUsbAudioEventType : uint8_t
{
  SampleRateChanged,
  MuteChanged,
  VolumeChanged,
  StreamStateChanged,
};

enum class EspUsbAudioEventTarget : uint8_t
{
  Function,
  Playback,
  Capture,
};

struct EspUsbAudioEvent
{
  EspUsbAudioEventType type = EspUsbAudioEventType::SampleRateChanged;
  EspUsbAudioEventTarget target = EspUsbAudioEventTarget::Function;
  uint8_t channel = 0;
  uint8_t alternateSetting = 0;
  uint32_t sampleRate = 0;
  int16_t volumeDb256 = 0;
  bool muted = false;
  bool enabled = false;
};

struct EspUsbAudioStreamStats
{
  uint32_t transferredBytes = 0;
  uint32_t overrunCount = 0;
  uint32_t overrunBytes = 0;
  uint32_t underrunCount = 0;
  uint32_t underrunBytes = 0;
};

struct EspUsbAudioVolumeRange
{
  int16_t min = 0;
  int16_t max = 0;
  int16_t resolution = 0;
};

struct EspUsbAudioState;
namespace espusb { namespace internal { struct AudioTransport; } }
class EspUsbAudioFunction;

class EspUsbAudioPlaybackStream
{
public:
  static constexpr EspUsbAudioDirection direction()
  {
    return EspUsbAudioDirection::Playback;
  }
  bool addFormat(const EspUsbAudioFormat &format);
  // Stopped only: release format settings; the stream remains selected.
  // Add at least one format before the next begin(). PCM is clearBuffer().
  esp_err_t clearFormats();
  // Active PCM snapshot; false while idle/draining, leaving format unchanged.
  bool currentFormat(EspUsbAudioFormat &format) const;
  int available() const;
  size_t capacity() const;
  size_t packetCapacity() const;
  size_t read(void *data, size_t length);
  bool clearBuffer();
  EspUsbAudioStreamStats stats() const;
  void resetStats();

private:
  friend class EspUsbAudioFunction;
  friend struct EspUsbAudioState;
  friend struct espusb::internal::AudioTransport;
  explicit EspUsbAudioPlaybackStream(EspUsbAudioFunction &function)
      : function_(function) {}

  EspUsbAudioFunction &function_;
  std::atomic<uint32_t> transferredBytes_{0};
  std::atomic<uint32_t> overrunCount_{0};
  std::atomic<uint32_t> overrunBytes_{0};
};

class EspUsbAudioCaptureStream
{
public:
  static constexpr EspUsbAudioDirection direction()
  {
    return EspUsbAudioDirection::Capture;
  }
  bool addFormat(const EspUsbAudioFormat &format);
  // Stopped only: release format settings; the stream remains selected.
  // Add at least one format before the next begin(). PCM is clearBuffer().
  esp_err_t clearFormats();
  // Active PCM snapshot; false while idle/draining, leaving format unchanged.
  bool currentFormat(EspUsbAudioFormat &format) const;
  size_t write(const void *data, size_t length);
  size_t availableForWrite() const;
  size_t capacity() const;
  size_t packetCapacity() const;
  bool clearBuffer();
  EspUsbAudioStreamStats stats() const;
  void resetStats();

private:
  friend class EspUsbAudioFunction;
  friend struct EspUsbAudioState;
  friend struct espusb::internal::AudioTransport;
  explicit EspUsbAudioCaptureStream(EspUsbAudioFunction &function)
      : function_(function) {}

  EspUsbAudioFunction &function_;
  std::atomic<uint32_t> transferredBytes_{0};
  std::atomic<uint32_t> overrunCount_{0};
  std::atomic<uint32_t> overrunBytes_{0};
  std::atomic<uint32_t> underrunCount_{0};
  std::atomic<uint32_t> underrunBytes_{0};
};

// Which PCM a full playback ring keeps when a received packet does not fit.
enum class EspUsbAudioOverflowPolicy : uint8_t
{
  DropNewest, // keep the unread PCM, drop the part of the packet that does not fit
  DropOldest, // push out the oldest unread PCM, keep the newest complete frames
};

struct EspUsbAudioConfig {
  size_t eventCapacity = 8;
  size_t playbackBufferFrames = 256;
  size_t captureBufferFrames = 256;
  uint8_t *playbackBuffer = nullptr;
  uint8_t *captureBuffer = nullptr;
  // Extents of optional CPU ring storage, kept alive through end().
  // Must cover bufferFrames * largest registered PCM frame. Null uses 0.
  size_t playbackBufferBytes = 0;
  size_t captureBufferBytes = 0;
  EspUsbAudioOverflowPolicy playbackOverflowPolicy =
      EspUsbAudioOverflowPolicy::DropOldest;
};

class EspUsbAudioFunction : public EspUsbDeviceClass
{
public:
  explicit EspUsbAudioFunction(
      EspUsbDevice &device,
      EspUsbAudioProtocol protocol = EspUsbAudioProtocol::Uac1);
  ~EspUsbAudioFunction() override;

  EspUsbAudioProtocol protocol() const { return protocol_; }
  EspUsbAudioPlaybackStream &addPlaybackStream();
  EspUsbAudioCaptureStream &addCaptureStream();
  bool hasPlaybackStream() const { return playbackEnabled_; }
  bool hasCaptureStream() const { return captureEnabled_; }

  esp_err_t configure(const EspUsbAudioConfig &config);
  const EspUsbAudioConfig &configuration() const { return config_; }
  size_t eventCapacity() const;
  esp_err_t startError() const override { return startError_; }
  bool begin() override;
  void prepareToStop() override;
  void end() override;
  bool isHid() const override { return false; }
  bool isAudio() const override { return true; }
  uint16_t configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber,
                                   uint8_t endpointNumber,
                                   uint16_t endpointSize) override;
  uint16_t configurationDescriptorForSpeed(
      uint8_t *dst, size_t capacity, uint8_t interfaceNumber,
      uint8_t endpointNumber, bool highSpeed) override;
  uint8_t interfaceCount() const override;
  uint8_t endpointCount() const override;

  bool handleGetEntityRequest(uint8_t rhport, const void *request);
  bool handleSetEntityRequest(const void *request, const uint8_t *data);
  bool handleGetEndpointRequest(uint8_t rhport, const void *request);
  bool handleSetEndpointRequest(const void *request, const uint8_t *data);
  bool handleSetInterface(const void *request);
  bool handlePlaybackTransfer(uint16_t bytes, uint8_t alternateSetting);
  bool handleCaptureTransfer(uint16_t bytes, uint8_t alternateSetting);
  uint32_t currentSampleRate() const;
  bool hasMute(EspUsbAudioDirection direction =
                   EspUsbAudioDirection::Playback,
               uint8_t channel = 0) const;
  bool hasVolume(EspUsbAudioDirection direction =
                     EspUsbAudioDirection::Playback,
                 uint8_t channel = 0) const;
  bool getMute(bool &mute,
               EspUsbAudioDirection direction =
                   EspUsbAudioDirection::Playback,
               uint8_t channel = 0) const;
  bool setMute(bool mute,
               EspUsbAudioDirection direction =
                   EspUsbAudioDirection::Playback,
               uint8_t channel = 0);
  bool getVolume(int16_t &volume,
                 EspUsbAudioDirection direction =
                     EspUsbAudioDirection::Playback,
                 uint8_t channel = 0) const;
  bool setVolume(int16_t volume,
                 EspUsbAudioDirection direction =
                     EspUsbAudioDirection::Playback,
                 uint8_t channel = 0);
  bool getVolumeRange(
      EspUsbAudioVolumeRange &range,
      EspUsbAudioDirection direction = EspUsbAudioDirection::Playback,
      uint8_t channel = 0) const;
  bool pollEvent(EspUsbAudioEvent &event);
  size_t pendingEvents() const;
  uint32_t droppedEvents() const;
  void clearEvents();

private:
  friend struct EspUsbAudioState;
  friend struct espusb::internal::AudioTransport;
  friend class EspUsbAudioPlaybackStream;
  friend class EspUsbAudioCaptureStream;

  esp_err_t clearFormats(espusb::internal::AudioDirection direction);
  bool addFormat(espusb::internal::AudioDirection direction,
                 const EspUsbAudioFormat &format);
  bool buildGraph(uint8_t interfaceNumber, uint8_t endpointNumber);
  uint8_t controlEntityId(EspUsbAudioDirection direction) const;
  EspUsbAudioEventTarget eventTarget(uint8_t entityId) const;
  void pushControlEvent(espusb::internal::AudioControlSelector selector,
                        uint8_t entityId, uint8_t channel, int32_t value);

  EspUsbAudioState *state_ = nullptr;
  EspUsbAudioConfig config_;
  esp_err_t startError_ = ESP_OK;
  std::atomic<bool> started_{false};
  bool playbackEnabled_ = false;
  bool captureEnabled_ = false;
  EspUsbAudioProtocol protocol_ = EspUsbAudioProtocol::Uac1;
  EspUsbAudioPlaybackStream playback_{*this};
  EspUsbAudioCaptureStream capture_{*this};
  espusb::internal::AudioFunctionModel model_;
  espusb::internal::AudioFunctionGraph graph_;
  espusb::internal::AudioControlState controls_;
  espusb::internal::AudioEventQueue events_;
  uint8_t playbackAlternate_ = 0;
  uint8_t captureAlternate_ = 0;
};

// Pixel format the camera advertises.
//
// Mjpeg is the one worth using on a full-speed part: a full-speed isochronous
// endpoint carries at most 1023 bytes per 1 ms frame, so about 1 MB/s, and one
// uncompressed QVGA frame is 150 KB. Yuy2 is here because it needs no encoder
// and every host can display it, which makes it the honest choice for a test
// pattern and for tiny frames.
enum class EspUsbDeviceVideoFormat : uint8_t
{
  Mjpeg = 0,
  Yuy2,
};

enum class EspUsbVideoTransport : uint8_t { Isochronous, Bulk };
struct EspUsbVideoConfig {
  EspUsbVideoTransport transport = EspUsbVideoTransport::Isochronous;
  size_t payloadBytes = 512; // Includes the two-byte UVC header.
  uint16_t isochronousPacketBytes = 0; // 0: derive from payloadBytes and speed.
  uint8_t *payloadBuffer = nullptr; // Optional internal DMA RAM, 64-byte aligned.
  size_t payloadBufferBytes = 0;
};
struct EspUsbVideoState;
namespace espusb { namespace internal { struct VideoTransport; } }

// What the host actually committed to after negotiating with the device.
//
// UVC calls this probe/commit: the host proposes parameters, the device may
// adjust them, and the committed set is what streaming then uses. The values
// expose the accepted payload limit; this implementation advertises one fixed
// format, frame size and frame interval.
struct EspUsbDeviceVideoCommit
{
  uint8_t formatIndex = 0;
  uint8_t frameIndex = 0;
  // 100 ns units, as UVC counts: 333333 is 30 fps.
  uint32_t frameInterval = 0;
  uint32_t maxVideoFrameSize = 0;
  // Most the host will accept in one transfer, payload header included.
  // Bounded by configuration().payloadBytes and the selected ISO packet size.
  uint32_t maxPayloadTransferSize = 0;
};

// A USB Video Class camera: one control interface and one streaming interface,
// advertising one format at one frame size and rate.
//
// The device is the source; there is no host-to-device direction. Give it a
// frame with sendFrame() and the class driver paces the payloads out of the
// selected endpoint, calling the completion callback when the last one has
// gone. One frame is in flight at a time.
//
// Available throughput depends on USB speed, endpoint packet size and the
// controller FIFO budget. Advertising a frame rate does not guarantee that
// an encoder or the selected transport can sustain it.
class EspUsbDeviceVideo : public EspUsbDeviceClass
{
public:
  // name, when given, is published as the function name (iFunction on the
  // IAD and iInterface on both interfaces), which is what Windows shows for
  // the camera.
  explicit EspUsbDeviceVideo(EspUsbDevice &device, const char *name = nullptr);
  ~EspUsbDeviceVideo() override;

  // All four take effect at begin() and are refused afterwards, because the
  // descriptor is built there and the host has already read it.
  bool setFormat(EspUsbDeviceVideoFormat format);
  // Bounded by what one configuration descriptor can describe rather than by
  // anything the transport can carry: a frame is sent in as many payloads as
  // it takes. 4096x4096 is the limit here.
  bool setFrameSize(uint16_t width, uint16_t height);
  bool setFrameRate(uint8_t framesPerSecond);
  // Upper bound on one encoded frame, which MJPEG needs and uncompressed does
  // not (there the size is width * height * 2, exactly). Hosts use it to size
  // their own buffers, so a value smaller than a frame this device actually
  // sends is a defect the host sees as a torn image. 0 restores the default:
  // the uncompressed size for Yuy2, and an eighth of it for Mjpeg.
  bool setMaxFrameSize(uint32_t bytes);

  esp_err_t configure(const EspUsbVideoConfig &config);
  const EspUsbVideoConfig &configuration() const { return videoConfig_; }
  bool begin() override;
  esp_err_t startError() const override { return startError_; }
  void prepareToStop() override;
  void end() override;
  bool isHid() const override { return false; }
  bool isVideo() const override { return true; }
  uint16_t configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize) override;
  uint16_t configurationDescriptorForSpeed(
      uint8_t *dst, size_t capacity, uint8_t interfaceNumber,
      uint8_t endpointNumber, bool highSpeed) override;
  // Video control and video streaming.
  uint8_t interfaceCount() const override { return 2; }
  uint8_t endpointCount() const override { return 1; }
  const char *functionName() const override { return name_; }
  void assignFunctionIds(uint8_t instance, uint8_t stringIndex) override;

  EspUsbDeviceVideoFormat format() const { return format_; }
  uint16_t width() const { return width_; }
  uint16_t height() const { return height_; }
  uint8_t frameRate() const { return frameRate_; }
  uint32_t maxFrameSize() const;
  // Bytes configurationDescriptor() will write for the given speed, so a
  // caller can size a buffer or check the endpoint budget first.
  uint16_t descriptorLength(bool highSpeed = false) const;
  // wMaxPacketSize this function asks for at the given speed. Isochronous
  // bandwidth is reserved per frame interval, so this number is the streaming
  // rate: 1023 B/ms at full speed, 1024 B/125 us at high speed.
  uint16_t isochronousPacketSize(bool highSpeed) const;
  // Actual logical staging capacity, or zero before begin()/after end().
  size_t payloadBufferSize() const;
  // Per-Function runtime selection; configure while stopped, then begin().
  bool bulkStreaming() const;

  // True after commit and (for ISO) streaming alternate selection. False
  // during cancellation/reconfiguration fences, stop and unplug.
  bool streaming() const;
  // Borrows one frame until onFrameResult returns ownership, on every terminal
  // status. Keep it valid and unchanged until then. YUY2 requires the exact
  // configured frame size; MJPEG accepts 1..maxFrameSize() bytes.
  // Concurrent callers are serialized; a second pending frame is refused.
  bool sendFrame(const void *frame, size_t length);
  // Same submission/result contract as writeDirect; the frame itself does not
  // require DMA alignment. A rejected submission has id=0 and borrows nothing.
  EspUsbTransferSubmission submitFrame(const void *frame, size_t length);
  // Asynchronous: ownership returns through onFrameResult, not this call.
  // A stale/zero ticket returns ESP_ERR_NOT_FOUND.
  esp_err_t cancelFrame(uint64_t id);
  // True while the driver borrows a frame, including a cancellation fence.
  bool frameInFlight() const { return inFlight_.load(); }

  using FrameCompleteCallback = std::function<void()>;
  using CommitCallback = std::function<void(const EspUsbDeviceVideoCommit &)>;
  // Runs on the USB device task when the last payload of a frame has gone.
  // Arming the next frame from inside it is the intended shape and is what
  // keeps the endpoint busy; keep everything else out of it.
  // Receives Success, Cancelled, BusReset, TransferError or Stopped.
  // actualBytes excludes UVC headers; actualBytesKnown can be false on abort.
  // Callbacks run on the USB worker; registration is refused while started.
  using FrameResultCallback = std::function<void(const EspUsbTransferResult &)>;
  esp_err_t onFrameResult(FrameResultCallback callback);
  esp_err_t onFrameComplete(FrameCompleteCallback callback);
  // Runs when the host commits parameters. Recommit can cancel an old frame;
  // only its result callback returns ownership, even after this callback.
  esp_err_t onCommit(CommitCallback callback);
  // What the host committed to, or a zeroed struct before it has.
  EspUsbDeviceVideoCommit commit() const;

private:
  friend struct EspUsbVideoState;
  friend struct espusb::internal::VideoTransport;
  void handleFrameResult(const EspUsbTransferResult &result);
  void handleCommit(const EspUsbDeviceVideoCommit &commit);
  EspUsbVideoConfig videoConfig_;
  EspUsbVideoState *state_ = nullptr;
  FrameResultCallback frameResultCallback_;
  const char *name_ = nullptr;
  uint8_t stringIndex_ = 0;
  uint8_t instance_ = 0;
  EspUsbDeviceVideoFormat format_ = EspUsbDeviceVideoFormat::Mjpeg;
  uint16_t width_ = 320;
  uint16_t height_ = 240;
  uint8_t frameRate_ = 15;
  uint32_t maxFrameSize_ = 0;
  std::atomic<bool> running_{false}, stopping_{false};
  esp_err_t startError_ = ESP_OK;
  // Published under the transport lock and read from application tasks.
  std::atomic<bool> inFlight_{false};
  FrameCompleteCallback frameCompleteCallback_;
  CommitCallback commitCallback_;
  EspUsbDeviceVideoCommit commit_;
};

struct EspUsbMscConfig {
  size_t transferBytes = 512;
  uint8_t *transferBuffer = nullptr;
  const char *name = nullptr;
};
struct EspUsbMscState;
class EspUsbDeviceMsc : public EspUsbDeviceClass
{
public:
  explicit EspUsbDeviceMsc(EspUsbDevice &device);
  ~EspUsbDeviceMsc() override;

  bool begin() override;
  bool begin(uint32_t blockCount, uint16_t blockSize);
  void prepareToStop() override;
  void end() override;
  esp_err_t startError() const override { return startError_; }
  esp_err_t configure(const EspUsbMscConfig &config);
  const EspUsbMscConfig &configuration() const { return config_; }
  size_t transferCapacity() const;
  bool mounted() const;
  const char *functionName() const override { return config_.name; }
  void assignFunctionIds(uint8_t, uint8_t index) override { stringIndex_ = index; }
  bool isHid() const override { return false; }
  bool isMsc() const override { return true; }
  uint16_t configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize) override;
  uint8_t interfaceCount() const override { return 1; }
  uint8_t endpointCount() const override { return 1; }

  esp_err_t vendorID(const char *value);
  esp_err_t productID(const char *value);
  esp_err_t productRevision(const char *value);
  void mediaPresent(bool value);
  void isWritable(bool value);
  esp_err_t onRead(EspUsbDeviceMscReadCallback callback);
  esp_err_t onWrite(EspUsbDeviceMscWriteCallback callback);
  esp_err_t onStartStop(EspUsbDeviceMscStartStopCallback callback);

  uint8_t maxLun() const;
  void inquiry(uint8_t vendor[8], uint8_t product[16], uint8_t revision[4]) const;
  bool testUnitReady() const;
  void capacity(uint32_t *blockCount, uint16_t *blockSize) const;
  bool startStop(uint8_t powerCondition, bool start, bool loadEject);
  int32_t read10(uint32_t lba, uint32_t offset, void *buffer, uint32_t size);
  int32_t write10(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size);
  bool writable() const;

private:
  friend struct EspUsbMscState;
  EspUsbMscState *state_ = nullptr;
  EspUsbMscConfig config_;
  uint8_t interfaceNumber_ = 0;
  esp_err_t startError_ = ESP_OK;
  uint8_t stringIndex_ = 0;
  static void copyPadded(uint8_t *dst, size_t size, const char *value);

  char vendor_[9] = "ESP32";
  char product_[17] = "MSC";
  char revision_[5] = "1.0";
  uint32_t blockCount_ = 0;
  uint16_t blockSize_ = 0;
  std::atomic<bool> mediaPresent_{false};
  std::atomic<bool> writable_{true};
  EspUsbDeviceMscReadCallback readCallback_;
  EspUsbDeviceMscWriteCallback writeCallback_;
  EspUsbDeviceMscStartStopCallback startStopCallback_;
};

class EspUsbDeviceMscRamDisk
{
public:
  EspUsbDeviceMscRamDisk(uint8_t *storage, uint32_t blockCount, uint16_t blockSize = 512);

  // Installs callbacks and geometry, preserving media/writable settings.
  // Call msc.mediaPresent(true) before device.begin() to expose the disk.
  // MSC is writable by default; use msc.isWritable(false) for read-only media.
  bool attach(EspUsbDeviceMsc &msc);
  bool valid() const;
  uint8_t *data();
  const uint8_t *data() const;
  uint32_t blockCount() const;
  uint16_t blockSize() const;
  size_t byteSize() const;
  void clear(uint8_t value = 0);
  bool readBlock(uint32_t lba, void *buffer) const;
  bool writeBlock(uint32_t lba, const void *buffer);
  void writeByte(uint32_t lba, uint16_t offset, uint8_t value);

private:
  int32_t read(uint32_t lba, uint32_t offset, void *buffer, uint32_t size) const;
  int32_t write(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size);

  uint8_t *storage_ = nullptr;
  uint32_t blockCount_ = 0;
  uint16_t blockSize_ = 512;
};

class EspUsbDeviceMscFatRamDisk
{
public:
  using EjectCallback = std::function<void()>;

  EspUsbDeviceMscFatRamDisk(uint8_t *storage, size_t size);

  bool format(const char *volumeLabel = "ESPUSB");
  // Like RamDisk::attach(), preserves media/writable settings. Explicitly call
  // msc.mediaPresent(true) before device.begin(); unlike FirmwareDisk::attach(),
  // this does not mark media present or force write access.
  bool attach(EspUsbDeviceMsc &msc);
  void onEject(EjectCallback callback);
  bool addFile(const char *name, const uint8_t *data, size_t size);
  bool addTextFile(const char *name, const char *text);
  bool exists(const char *name) const;
  size_t fileSize(const char *name) const;
  size_t readFile(const char *name, uint8_t *buffer, size_t size) const;
  uint32_t blockCount() const;
  uint16_t blockSize() const;
  size_t byteSize() const;

private:
  bool normalizeName(const char *name, char out[11]) const;
  bool findFile(const char name[11], uint32_t *firstCluster, uint32_t *size) const;
  bool allocateClusters(size_t size, uint16_t *firstCluster, size_t *clusterCount);
  void setFatEntry(uint16_t cluster, uint16_t value);
  uint16_t fatEntry(uint16_t cluster) const;
  uint8_t *clusterPtr(uint16_t cluster);
  const uint8_t *clusterPtr(uint16_t cluster) const;
  uint8_t *rootEntry(uint16_t index);
  const uint8_t *rootEntry(uint16_t index) const;
  bool handleStartStop(uint8_t powerCondition, bool start, bool loadEject);

  uint8_t *storage_ = nullptr;
  size_t size_ = 0;
  uint32_t blockCount_ = 0;
  uint16_t sectorsPerFat_ = 0;
  uint16_t rootEntryCount_ = 64;
  uint16_t rootDirSectors_ = 0;
  uint16_t dataStartSector_ = 0;
  uint16_t clusterCount_ = 0;
  uint16_t nextFreeCluster_ = 2;
  uint16_t nextRootEntry_ = 0;
  EspUsbDeviceMscRamDisk blocks_;
  EjectCallback ejectCallback_;
};

#if ESP_USB_DEVICE_HAS_ARDUINO_SD
class EspUsbDeviceMscSdCard
{
public:
  using EjectCallback = std::function<void()>;

  explicit EspUsbDeviceMscSdCard(fs::SDFS &sd = SD);

  bool begin(uint8_t ssPin = SS, SPIClass &spi = SPI, uint32_t frequency = 4000000, const char *mountpoint = "/sd", uint8_t maxFiles = 5);
  bool attach(EspUsbDeviceMsc &msc);
  void onEject(EjectCallback callback);
  void readOnly(bool value);
  bool readOnly() const;
  uint32_t blockCount() const;
  uint16_t blockSize() const;
  bool mounted() const;

private:
  int32_t read(uint32_t lba, uint32_t offset, void *buffer, uint32_t size);
  int32_t write(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size);
  bool handleStartStop(uint8_t powerCondition, bool start, bool loadEject);

  fs::SDFS *sd_ = nullptr;
  uint32_t blockCount_ = 0;
  uint16_t blockSize_ = 512;
  bool mounted_ = false;
  bool readOnly_ = false;
  EjectCallback ejectCallback_;
};
#endif

// --- Firmware update ------------------------------------------------------

// Writes a new application image into the OTA partition that is not running,
// verifies it, and switches the boot partition.
//
// Transport-independent on purpose: EspUsbDeviceDfu drives it, and so can a
// sketch that receives an image over CDC, vendor bulk, MSC or an HTTP upload
// across the CDC-NCM interface. It is not a USB class - it registers nothing
// with EspUsbDevice and works on its own.
//
// Nothing here is reversible by itself: end() makes the new image the one that
// boots. A device that must survive a bad image needs bootloader rollback
// (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) plus a markValid() call once the new
// firmware has proved itself; without that, the only way back is the ROM
// loader. See docs/ota-over-usb.md.
class EspUsbDeviceFirmwareUpdate
{
public:
  EspUsbDeviceFirmwareUpdate();
  ~EspUsbDeviceFirmwareUpdate();

  // Holds an open OTA handle, which is a flash resource and not copyable.
  EspUsbDeviceFirmwareUpdate(const EspUsbDeviceFirmwareUpdate &) = delete;
  EspUsbDeviceFirmwareUpdate &operator=(const EspUsbDeviceFirmwareUpdate &) = delete;

  // Whether this firmware has a second application partition to write into.
  // False under a single-app partition scheme such as `huge_app`, where no
  // self-update can work at all - check it before telling a host an update
  // path exists, rather than failing halfway through an upload.
  static bool available();
  // Byte capacity of the partition an update would be written into, or 0 when
  // there is none. The largest image begin() accepts.
  static size_t capacity();
  // Label of the partition an update would be written into ("app1", say), or
  // nullptr when there is none.
  static const char *targetLabel();

  // Open the update.
  //
  // expectedSize is the image length when the transport knows it in advance,
  // which lets write() reject an overrun on the first byte past the end rather
  // than at the end of a long upload. 0 means unknown, which is normal for a
  // stream. Either way the flash is erased as the write advances, never up
  // front: erasing a whole 1.25 MB partition takes seconds, and a USB callback
  // is the wrong place to spend them.
  bool begin(size_t expectedSize = 0);
  // Open the update for writes that arrive in any order.
  //
  // imageSize is required rather than optional here, and that is the whole
  // difference: flash cannot be erased a byte at a time, so a write that
  // arrives out of order cannot erase around itself. The image's worth of
  // partition is erased before the first write instead of as the write
  // advances - a few hundred milliseconds for a typical image, paid once.
  //
  // Only a transport that is told the length up front can use this. UF2 is the
  // case it exists for: every block carries its own target address and the
  // header says how many there are.
  bool beginRandomAccess(size_t imageSize);
  // Append the next bytes of the image, in order. Erases the sectors it
  // reaches. False leaves the update aborted; read lastErrorName().
  bool write(const void *data, size_t length);
  // Write at an explicit offset within the image. Needs beginRandomAccess();
  // the two write calls cannot be mixed on one update, and the wrong one fails
  // with ESP_ERR_INVALID_STATE rather than writing to the wrong place.
  bool writeAt(size_t offset, const void *data, size_t length);
  // Verify what was written and make it the partition that boots next.
  //
  // Checks the image header, the length, and the checksum, so a truncated or
  // corrupt upload fails here rather than at the next boot. The running
  // firmware is untouched until the next restart.
  bool end();
  // Give up. The boot partition is left alone and the target partition keeps
  // whatever half-written bytes it has, which the next begin() overwrites.
  void abort();

  bool active() const;
  // Bytes accepted by write() so far.
  // Bytes accepted so far. For a random-access update that is the sum of what
  // every writeAt() took, which says how much of the image has arrived but not
  // which parts.
  size_t written() const;
  // The size begin() or beginRandomAccess() was given, or 0 when begin() was
  // not told.
  size_t expectedSize() const;
  // Whether this update is taking writes at explicit offsets.
  bool randomAccess() const;
  esp_err_t lastError() const;
  const char *lastErrorName() const;

  // Confirm the firmware that is running now, cancelling the rollback that a
  // bootloader built with CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE would otherwise
  // perform at the next restart. Call it once the new image has shown it works
  // - after the USB device enumerates again, not in setup().
  //
  // True also when no rollback was pending, since there is then nothing to
  // confirm; a stock Arduino build is that case.
  static bool markValid();
  // Roll back to the previously running image and restart. Only possible when
  // a valid previous application partition exists. Does not return on success.
  static bool rollback();
  // Point the boot partition back at the firmware running now, undoing a
  // successful end(). For a caller that inspected the image after it verified
  // and decided against it; the written bytes stay where they are and the next
  // begin() overwrites them.
  static bool cancelPendingBoot();

private:
  uint32_t handle_ = 0;
  const void *partition_ = nullptr;   // const esp_partition_t*
  size_t written_ = 0;
  size_t expectedSize_ = 0;
  bool active_ = false;
  bool randomAccess_ = false;
  esp_err_t lastError_ = ESP_OK;
};

// A small FAT volume whose data region *is* the OTA partition: the host drops a
// firmware `.bin` onto the drive and the device writes it straight to flash.
//
// The image is never held in RAM. Only the volume's metadata - boot sector, FAT,
// root directory - and a scratch area for whatever the host's file manager
// leaves behind live in the buffer the sketch supplies. Everything past that is
// streamed into EspUsbDeviceFirmwareUpdate as it arrives, which is what lets a
// board with 320 KB of RAM accept a 1.25 MB image.
//
// Two file formats, and the difference between them is what the host is allowed
// to do:
//
//  - A raw `.bin` is recognised by the ESP image magic `0xE9`, and must be
//    written in ascending order. That is what every file manager does when
//    copying onto an empty volume, but nothing makes it promise to. A write
//    that jumps backwards or leaves a gap is refused and the update abandoned
//    with ESP_ERR_INVALID_STATE rather than half-written.
//  - A `.uf2` carries its own target address in every 512-byte block, so any
//    order works, host metadata is rejected rather than guessed at, the length
//    is known from the header instead of from a directory entry, and a family
//    ID stops an image for the wrong chip. Produce one with uf2conv.py using
//    uf2FamilyId() and base address 0.
class EspUsbDeviceMscFirmwareDisk
{
public:
  using EjectCallback = std::function<void()>;
  // Bytes of the image written so far.
  using ProgressCallback = std::function<void(size_t written)>;
  // The image was written and verified and the boot partition has moved.
  // Return false to refuse it, which puts the boot partition back.
  using CompleteCallback = std::function<bool()>;
  using ErrorCallback = std::function<void(esp_err_t error)>;

  // storage holds the volume metadata and the scratch area. 8 KB is the
  // minimum; 16 KB is comfortable. The scratch area is what absorbs
  // `System Volume Information`, `.fseventsd`, `.Spotlight-V100` and the rest,
  // and a host that fills it starts allocating clusters inside the firmware
  // region - where a write that is not the start of an ESP image is ignored.
  EspUsbDeviceMscFirmwareDisk(uint8_t *storage, size_t size);
  ~EspUsbDeviceMscFirmwareDisk();

  EspUsbDeviceMscFirmwareDisk(const EspUsbDeviceMscFirmwareDisk &) = delete;
  EspUsbDeviceMscFirmwareDisk &operator=(const EspUsbDeviceMscFirmwareDisk &) = delete;

  // Lay out the volume for the OTA partition this firmware would update.
  // False when there is no such partition (a single-app partition scheme), when
  // the buffer is too small for the metadata plus one scratch cluster, or when
  // the partition needs more clusters than FAT12 can address at the largest
  // cluster size.
  bool begin(const char *volumeLabel = "ESPUSB");
  bool attach(EspUsbDeviceMsc &msc);

  // The UF2 family ID for the chip this was built for, which is what
  // `uf2conv.py --family` has to be given for the image to be accepted here.
  // From the registry in microsoft/uf2.
  static uint32_t uf2FamilyId();

  // A file the volume shows before anything is copied to it. Call between
  // begin() and attach(); the text lives in the scratch area. Mostly useful for
  // saying what to drop on the drive and what happens next.
  bool addTextFile(const char *name, const char *text);

  void onProgress(ProgressCallback callback);
  void onComplete(CompleteCallback callback);
  void onError(ErrorCallback callback);
  // Ejecting the drive commits whatever arrived, which is the second way an
  // update finishes: the first is the file's own directory entry saying how
  // long it is, and that usually lands first.
  void onEject(EjectCallback callback);

  // Restart into the newly written firmware once it verifies. On by default:
  // the drive exists to install an image, and the host has already been told
  // the medium is going away. Turn it off to restart at a moment the sketch
  // chooses.
  void restartWhenComplete(bool enable);

  uint32_t blockCount() const;
  uint16_t blockSize() const;
  // Sectors of the volume that live in `storage`. Everything at or past this
  // one is the OTA partition.
  uint32_t ramSectorCount() const;
  size_t written() const;
  bool updating() const;
  // Whether the update in progress arrived as UF2 blocks rather than a raw
  // image. Meaningless while updating() is false.
  bool updatingUf2() const;
  EspUsbDeviceFirmwareUpdate &firmware();

  // Internal (called from the MSC callbacks on the usbd task).
  int32_t read(uint32_t lba, uint32_t offset, void *buffer, uint32_t size);
  int32_t write(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size);
  bool handleStartStop(uint8_t powerCondition, bool start, bool loadEject);

private:
  // How the file the host is writing is packaged. Decided once, from the first
  // bytes to reach the firmware region, and reset when the update ends.
  enum class ImageFormat : uint8_t
  {
    Unknown,
    Raw,
    Uf2,
  };

  bool writeFirmwareRegion(size_t regionOffset, const uint8_t *data, uint32_t size);
  bool writeRawImage(size_t regionOffset, const uint8_t *data, uint32_t size);
  bool writeUf2Stream(size_t regionOffset, const uint8_t *data, uint32_t size);
  bool writeUf2Block(const uint8_t *block);
  void releaseUf2State();
  void failUpdate(esp_err_t error);
  bool commit();
  // Length of the file the host is copying, taken from the root directory entry
  // whose first cluster falls in the firmware region. 0 until the host writes
  // that entry, which it may do before, during or after the data.
  size_t announcedImageSize() const;
  void checkForCompletion();
  uint8_t *rootEntry(uint16_t index);
  const uint8_t *rootEntry(uint16_t index) const;
  void setFatEntry(uint16_t cluster, uint16_t value);
  bool allocateScratchFile(const char name[11], size_t size, uint16_t *firstCluster);

  uint8_t *storage_ = nullptr;
  size_t size_ = 0;

  uint16_t sectorsPerCluster_ = 8;
  uint16_t sectorsPerFat_ = 0;
  uint16_t rootDirSectors_ = 1;
  uint16_t rootEntryCount_ = 16;
  uint32_t dataStartSector_ = 0;   // LBA of cluster 2
  uint32_t scratchSectors_ = 0;    // RAM-backed sectors inside the data region
  uint32_t ramSectors_ = 0;        // dataStartSector_ + scratchSectors_
  uint32_t firmwareSectors_ = 0;   // sectors mapped onto the OTA partition
  uint32_t blockCount_ = 0;
  uint16_t nextScratchCluster_ = 2;
  uint16_t nextRootEntry_ = 0;
  bool formatted_ = false;

  // LBA at which the current image started, so a later write can be turned back
  // into an offset inside the partition. 0 while no update is open.
  uint32_t imageStartSector_ = 0;
  bool updating_ = false;
  bool restartWhenComplete_ = true;
  ImageFormat format_ = ImageFormat::Unknown;

  // UF2 bookkeeping. The bitmap is what makes the block count exact: a host
  // that writes a block twice must not look like progress, or the update would
  // commit early on an image that is not all there.
  uint32_t uf2BaseAddress_ = 0;
  uint32_t uf2TotalBlocks_ = 0;
  uint32_t uf2BlocksSeen_ = 0;
  uint8_t *uf2SeenBitmap_ = nullptr;

  EspUsbDeviceFirmwareUpdate firmware_;
  ProgressCallback progressCallback_;
  CompleteCallback completeCallback_;
  ErrorCallback errorCallback_;
  EjectCallback ejectCallback_;
};

// --- DFU (USB Device Firmware Upgrade, bInterfaceClass 0xfe / subclass 1) ---

enum class EspUsbDeviceDfuMode : uint8_t
{
  // Advertise Runtime DFU, report its status/state, and accept DFU_DETACH.
  // `dfu-util -e` sends DETACH. The default action is to
  // restart into the chip's ROM download loader, so a host tool can ask for
  // boot mode without a protocol of this library's own.
  Runtime,
  // Implement the download. `dfu-util -D firmware.bin` writes the image into
  // the spare OTA partition through EspUsbDeviceFirmwareUpdate, and the device
  // restarts into it. The ROM is not involved, so this behaves identically on
  // ESP32-S2, ESP32-S3 and ESP32-P4.
  Download,
};

enum class EspUsbDfuStatus : uint8_t {
  Ok = 0, Target, File, Write, Erase, CheckErased, Program, Verify,
  Address, NotDone, Firmware, Vendor, UsbReset, PowerOnReset, Unknown, StalledPacket
};
struct EspUsbDfuConfig {
  size_t transferBytes = 1024;
  uint8_t *transferBuffer = nullptr; // Download only; CPU copy, no DMA alignment.
  uint16_t detachTimeoutMs = 1000;
  uint32_t downloadPollMs = 1;
  uint32_t manifestPollMs = 250;
  bool manifestationTolerant = false;
};
// Synchronous callbacks on the USB worker. Download's data is valid only until
// the callback returns. A supplied backend owns verification and activation;
// the built-in OTA Complete callback and automatic restart do not apply to it.
struct EspUsbDfuBackend {
  std::function<EspUsbDfuStatus(uint16_t block, const uint8_t *data, size_t bytes)> download;
  std::function<EspUsbDfuStatus()> manifest;
  std::function<void()> abort;
};
struct EspUsbDfuState;

// A DFU function. Costs one interface and no endpoints: every DFU transfer
// travels on EP0, which is why this is the one class that can be added to a
// device whose endpoint budget is already spent.
class EspUsbDeviceDfu : public EspUsbDeviceClass
{
public:
  using DetachCallback = std::function<void()>;
  using ProgressCallback = std::function<void(size_t written)>;
  using CompleteCallback = std::function<bool()>;
  using ErrorCallback = std::function<void(EspUsbDfuStatus status)>;
  explicit EspUsbDeviceDfu(EspUsbDevice &device,
                           EspUsbDeviceDfuMode mode = EspUsbDeviceDfuMode::Download,
                           const char *name = nullptr);
  ~EspUsbDeviceDfu() override;
  esp_err_t configure(const EspUsbDfuConfig &config);
  const EspUsbDfuConfig &configuration() const { return config_; }
  esp_err_t setBackend(const EspUsbDfuBackend &backend);
  bool begin() override;
  esp_err_t startError() const override { return startError_; }
  void prepareToStop() override;
  void end() override;
  bool isHid() const override { return false; }
  bool isDfu() const override { return true; }
  uint16_t configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize) override;
  uint8_t interfaceCount() const override { return 1; }
  uint8_t endpointCount() const override { return 0; }
  const char *functionName() const override { return name_; }
  void assignFunctionIds(uint8_t instance, uint8_t stringIndex) override;
  EspUsbDeviceDfuMode mode() const { return mode_; }
  uint16_t transferSize() const { return uint16_t(config_.transferBytes); }
  // Actual allocated Download buffer bytes; 0 before begin/end or in Runtime.
  size_t transferCapacity() const;
  esp_err_t onDetach(DetachCallback callback);
  esp_err_t onProgress(ProgressCallback callback);
  // Built-in OTA only: false vetoes the image and restores the running boot partition.
  esp_err_t onComplete(CompleteCallback callback);
  esp_err_t onError(ErrorCallback callback);
  // Built-in OTA only; true by default. Frozen while started.
  esp_err_t restartWhenComplete(bool enable);
  size_t written() const { return written_.load(); }
  EspUsbDeviceFirmwareUpdate &firmware() { return firmware_; }
  void onBusDetached() override;
private:
  friend struct EspUsbDfuState;
  void handleDetach();
  EspUsbDfuStatus handleDownload(uint16_t block, const uint8_t *data, uint16_t bytes);
  EspUsbDfuStatus handleManifest();
  void handleAbort();
  EspUsbDfuStatus failDownload(EspUsbDfuStatus status);
  EspUsbDeviceDfuMode mode_ = EspUsbDeviceDfuMode::Download;
  const char *name_ = nullptr;
  uint8_t stringIndex_ = 0, interface_ = 255;
  EspUsbDfuConfig config_;
  EspUsbDfuBackend backend_;
  EspUsbDfuState *state_ = nullptr;
  esp_err_t startError_ = ESP_OK;
  bool restartWhenComplete_ = true, downloadOpen_ = false;
  std::atomic<size_t> written_{0};
  EspUsbDeviceFirmwareUpdate firmware_;
  DetachCallback detachCallback_;
  ProgressCallback progressCallback_;
  CompleteCallback completeCallback_;
  ErrorCallback errorCallback_;
};

class EspUsbDeviceHidKeyboard : public EspUsbHidFunction
{
public:
  using OutputReportCallback = std::function<void(const EspUsbDeviceHidKeyboardOutputReport &)>;
  using ProtocolCallback = std::function<void(const EspUsbDeviceHidProtocolEvent &)>;

  explicit EspUsbDeviceHidKeyboard(EspUsbDevice &device);
  ~EspUsbDeviceHidKeyboard() override { end(); }

  bool begin() override;
  bool sendReport(const EspUsbDeviceBootKeyboardReport &report, uint32_t timeoutMs = 100);
  // Sends the whole held-key state as one report. Requires enableNkro() before
  // EspUsbDevice::begin(); fails otherwise (a missing enableNkro() is a setup
  // mistake, and folding the state down would make every seventh key vanish
  // silently). Still folds down to the 6-key boot format when the host selected
  // boot protocol, which is a host-driven runtime condition the sketch cannot
  // control. The boot-report overload above stays valid and keeps working.
  bool sendReport(const EspUsbDeviceNkroKeyboardReport &report, uint32_t timeoutMs = 100);
  bool pressUsage(uint8_t usage, uint8_t modifiers = 0, uint32_t holdMs = 10);
  bool pressKey(char key, uint32_t timeoutMs = 100);
  bool tapUsage(uint8_t usage, uint8_t modifiers = 0, uint32_t holdMs = 10);
  bool tapKey(char key, uint32_t holdMs = 10);
  bool write(const char *text, uint32_t interKeyDelayMs = 5);
  bool releaseUsage(uint8_t usage, uint32_t timeoutMs = 100);
  bool releaseAll(uint32_t timeoutMs = 100);
  void setLayout(EspUsbDeviceKeyboardLayout layout);
  EspUsbDeviceKeyboardLayout layout() const;
  esp_err_t onOutputReport(OutputReportCallback callback);
  // Latest LED output report from the host. Updated whether or not an
  // onOutputReport() callback is installed, so a sketch can read Lock state even
  // when an integration layer owns the single callback slot. LEDs are state, not
  // an event, so polling this is enough; the callback stays single-slot.
  // Everything reads false until the host sends its first output report (hosts
  // typically do so right after enumeration and whenever a Lock key is pressed),
  // and the state is cleared on bus reset / unplug so a previous session's LEDs
  // are never read back as current.
  //
  // Returns by value, not by reference: the host writes this from the USB
  // device task while the sketch reads it from its own task, so a reference would
  // hand out an object another task mutates. The raw LED byte is stored
  // atomically and the returned report is built from that one read, which makes a
  // torn combination of fields impossible. EspBle's ledState() returns by value
  // for the same reason.
  EspUsbDeviceHidKeyboardOutputReport ledState() const;
  esp_err_t onProtocol(ProtocolCallback callback);
  uint8_t protocol() const;

  // Opt-in N-key rollover. Call before EspUsbDevice::begin(). When enabled the
  // interface reports a bitmap covering usages 0x00-0xDF (all standard keys plus
  // International/LANG keys, so JIS layouts work), and press/release track any
  // number of simultaneous keys. The boot 6-key format is still sent when the
  // host selects boot protocol (BIOS). Default off (unchanged 6KRO behaviour).
  esp_err_t enableNkro(bool enable = true);
  bool nkroEnabled() const;

  // The state the host was last told about (NKRO only; stays cleared while NKRO
  // is off, since 6KRO keeps its held state as the boot report instead). Lets a
  // state-based caller suppress duplicate sends and resynchronise without a
  // shadow copy or a releaseAll(). While the host selected boot protocol this is
  // the requested state, not the bytes on the wire - those are folded to 6 keys.
  const EspUsbDeviceNkroKeyboardReport &heldState() const;

  uint16_t configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize) override;
  uint8_t interfaceCount() const override { return 1; }
  uint8_t endpointCount() const override { return 1; }
  uint8_t hidReportId() const override { return ESP_USB_DEVICE_HID_REPORT_ID_KEYBOARD; }
  const uint8_t *hidReportDescriptor() const override;
  uint16_t hidReportDescriptorLength() const override;
  uint16_t hidInEndpointSize() const override;
  void onHidSetReport(uint8_t reportId, uint8_t reportType, const uint8_t *data, uint16_t length) override;
  void onHidSetProtocol(uint8_t protocol) override;
  void onBusAttached() override;
  void onBusDetached() override;

private:
  bool asciiToUsage(char key, uint8_t &usage, uint8_t &modifiers) const;
  // Apply a bus attach/detach that the USB task flagged. Called from the sketch's
  // task at the head of every entry point that touches the held-key state, so
  // that state keeps exactly one writer and heldState() can hand out a reference.
  void applyPendingBusChange() const;
  // Put nkroState_ on the wire. Every NKRO send goes through here so the bitmap
  // layout and the boot-protocol fold-down live in one place.
  bool sendNkroReport(uint32_t timeoutMs = 100);

  EspUsbDeviceBootKeyboardReport report_;
  EspUsbDeviceKeyboardLayout layout_ = ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US;
  std::atomic<uint8_t> protocol_{1};
  bool nkroEnabled_ = false;
  // The held-key state while NKRO is on. Holding the public report type itself
  // keeps the bitmap layout and the modifier routing defined in exactly one
  // place, and lets heldState() hand out a reference. Mutable because the const
  // heldState() may have to apply a pending bus change before returning it.
  mutable EspUsbDeviceNkroKeyboardReport nkroState_;
  // Raw LED byte as the host last set it. One atomic byte rather than the whole
  // report struct, because the writer is the USB worker and the reader
  // is the sketch: a single store/load cannot be observed half-applied, so
  // ledState() can rebuild the flags from one read without a lock.
  std::atomic<uint8_t> ledsRaw_{0};
  // Set by the USB task when the bus attached or detached; consumed by the
  // sketch's task in applyPendingBusChange(). The held-key state is NOT cleared
  // from the USB task: that would give it a second writer and make the reference
  // returned by heldState() racy.
  mutable std::atomic<bool> busChangePending_{false};
  OutputReportCallback outputCallback_;
  ProtocolCallback protocolCallback_;
};

class EspUsbDeviceHidMouse : public EspUsbHidFunction
{
public:
  explicit EspUsbDeviceHidMouse(EspUsbDevice &device);
  ~EspUsbDeviceHidMouse() override { end(); }

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

class EspUsbDeviceHidCustom : public EspUsbHidFunction
{
public:
  EspUsbDeviceHidCustom(EspUsbDevice &device, const uint8_t *reportDescriptor, uint16_t reportDescriptorLength, uint16_t inputReportSize = 64);
  ~EspUsbDeviceHidCustom() override { end(); }

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

// Raw byte pipe over a HID interrupt endpoint. The one class here that needs no
// host-side driver on any OS, which is why it is worth making fast.
//
// reportSize selects runtime storage from the report descriptor. Large reports
// may span bus packets; no build flags are needed. The interim core's transfer
// length is uint16_t, leaving at most 65534 payload bytes plus the report ID.
// Available memory and the host's report support remain practical bounds.
class EspUsbDeviceHidVendor : public EspUsbHidFunction
{
public:
  using ReportCallback = std::function<void(const EspUsbDeviceHidReport &)>;

  explicit EspUsbDeviceHidVendor(EspUsbDevice &device, uint16_t reportSize = 63);
  ~EspUsbDeviceHidVendor() override { end(); }

  bool begin() override;
  bool sendInput(const void *data, size_t length, uint32_t timeoutMs = 100);
  esp_err_t onOutputReport(ReportCallback callback);
  esp_err_t onFeatureReport(ReportCallback callback);

  // Payload bytes per report, as the report descriptor declares it.
  uint16_t reportSize() const { return reportSize_; }
  // Interim transfer-length limit, independent of bus packet size/build flags.
  static uint16_t maxReportSize();

  uint16_t configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize) override;
  uint8_t interfaceCount() const override { return 1; }
  uint8_t endpointCount() const override { return 1; }
  uint8_t hidReportId() const override { return ESP_USB_DEVICE_HID_REPORT_ID_VENDOR; }
  const uint8_t *hidReportDescriptor() const override;
  uint16_t hidReportDescriptorLength() const override;
  uint16_t hidHighSpeedEndpointSize() const override;
  void onHidSetReport(uint8_t reportId, uint8_t reportType, const uint8_t *data, uint16_t length) override;

private:
  void buildReportDescriptor();

  uint16_t reportSize_ = 63;
  ReportCallback outputCallback_;
  ReportCallback featureCallback_;
  // Built from reportSize_ rather than shared and constant, because Report Count
  // is part of the contract: a host sizes its reads from what the report
  // descriptor declares, so a descriptor that says 63 while the endpoint carries
  // 512 gets the host to read 64 bytes and the device to overrun it. 32 bytes
  // covers the fixed items plus a two-byte Report Count for sizes above 255.
  uint8_t reportDescriptor_[32] = {};
  uint8_t reportDescriptorLength_ = 0;
};

class EspUsbDeviceHidGamepad : public EspUsbHidFunction
{
public:
  explicit EspUsbDeviceHidGamepad(EspUsbDevice &device);
  ~EspUsbDeviceHidGamepad() override { end(); }

  bool begin() override;
  bool sendReport(const EspUsbDeviceGamepadReport &report, uint32_t timeoutMs = 100);
  bool send(int8_t x,
            int8_t y,
            int8_t z,
            int8_t rz,
            int8_t rx,
            int8_t ry,
            uint8_t hat,
            uint32_t buttons,
            uint32_t timeoutMs = 100);
  bool releaseAll(uint32_t timeoutMs = 100);

  uint16_t configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize) override;
  uint8_t interfaceCount() const override { return 1; }
  uint8_t endpointCount() const override { return 1; }
  uint8_t hidReportId() const override { return ESP_USB_DEVICE_HID_REPORT_ID_GAMEPAD; }
  const uint8_t *hidReportDescriptor() const override;
  uint16_t hidReportDescriptorLength() const override;

private:
  EspUsbDeviceGamepadReport report_;
};

class EspUsbDeviceHidConsumerControl : public EspUsbHidFunction
{
public:
  explicit EspUsbDeviceHidConsumerControl(EspUsbDevice &device);
  ~EspUsbDeviceHidConsumerControl() override { end(); }

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

class EspUsbDeviceHidSystemControl : public EspUsbHidFunction
{
public:
  explicit EspUsbDeviceHidSystemControl(EspUsbDevice &device);
  ~EspUsbDeviceHidSystemControl() override { end(); }

  bool begin() override;
  // Standard usage IDs 0x81/0x82/0x83, or 0 to release; other values are refused.
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
