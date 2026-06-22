#include "EspUsbDevice.h"

static int passCount = 0;
static int failCount = 0;

static void check(bool condition, const char *name)
{
  if (condition)
  {
    passCount++;
  }
  else
  {
    Serial.print("FAIL ");
    Serial.println(name);
    failCount++;
  }
}

static uint16_t le16(const uint8_t *data)
{
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

static void testKeyboardDescriptor()
{
  EspUsbDevice device;
  EspUsbDeviceHidKeyboard keyboard(device);

  EspUsbDeviceConfig config;
  config.speed = ESP_USB_DEVICE_SPEED_FULL;
  config.vid = 0x303a;
  config.pid = 0x4001;
  config.manufacturer = "EspUsb";
  config.product = "Keyboard";
  config.serialNumber = "kbd-1";
  config.maxPowerMilliamps = 100;

  check(device.begin(config), "keyboard_begin");

  const uint8_t *dev = device.deviceDescriptor();
  check(dev[0] == 18 && dev[1] == 0x01, "device_descriptor_header");
  check(le16(&dev[2]) == 0x0200, "device_usb_version");
  check(dev[7] == 64, "device_ep0_mps");
  check(le16(&dev[8]) == 0x303a && le16(&dev[10]) == 0x4001, "device_vid_pid");
  check(dev[14] == 1 && dev[15] == 2 && dev[16] == 3, "device_string_indexes");

  const uint8_t *cfg = device.configurationDescriptor(0);
  check(cfg[0] == 9 && cfg[1] == 0x02, "config_descriptor_header");
  check(le16(&cfg[2]) == 41, "keyboard_config_total_len");
  check(cfg[4] == 1, "keyboard_interface_count");
  check(cfg[7] == 0x80 && cfg[8] == 50, "keyboard_power");

  const uint8_t *itf = &cfg[9];
  check(itf[0] == 9 && itf[1] == 0x04 && itf[2] == 0 && itf[4] == 2, "keyboard_interface");
  check(itf[5] == 0x03 && itf[6] == 0x01 && itf[7] == 0x01, "keyboard_boot_protocol");

  const uint8_t *hid = &cfg[18];
  check(hid[0] == 9 && hid[1] == 0x21 && le16(&hid[7]) == keyboard.hidReportDescriptorLength(), "keyboard_hid_descriptor");

  const uint8_t *epOut = &cfg[27];
  const uint8_t *epIn = &cfg[34];
  check(epOut[0] == 7 && epOut[1] == 0x05 && epOut[2] == 0x01, "keyboard_ep_out_addr");
  check(epIn[0] == 7 && epIn[1] == 0x05 && epIn[2] == 0x82, "keyboard_ep_in_addr");
  check(le16(&epOut[4]) == 8 && le16(&epIn[4]) == 8, "keyboard_ep_mps");

  check(device.hidReportDescriptor(0) == keyboard.hidReportDescriptor(), "keyboard_report_descriptor_ptr");
  check(keyboard.hidReportDescriptorLength() > 50, "keyboard_report_descriptor_len");
}

static void testMouseDescriptor()
{
  EspUsbDevice device;
  EspUsbDeviceHidMouse mouse(device);
  EspUsbDeviceConfig config;
  config.pid = 0x4002;
  config.selfPowered = true;
  config.maxPowerMilliamps = 2;

  check(device.begin(config), "mouse_begin");

  const uint8_t *cfg = device.configurationDescriptor(0);
  check(le16(&cfg[2]) == 34, "mouse_config_total_len");
  check(cfg[4] == 1, "mouse_interface_count");
  check(cfg[7] == 0xc0 && cfg[8] == 1, "mouse_power");

  const uint8_t *itf = &cfg[9];
  check(itf[4] == 1 && itf[5] == 0x03 && itf[7] == 0x02, "mouse_boot_protocol");
  const uint8_t *epIn = &cfg[27];
  check(epIn[2] == 0x81 && le16(&epIn[4]) == 8, "mouse_ep_in");
  check(mouse.hidReportDescriptorLength() > 40, "mouse_report_descriptor_len");
}

static void testCompositeDescriptor()
{
  EspUsbDevice device;
  EspUsbDeviceHidKeyboard keyboard(device);
  EspUsbDeviceHidMouse mouse(device);
  EspUsbDeviceConfig config;
  config.pid = 0x4003;

  check(device.begin(config), "composite_begin");

  const uint8_t *cfg = device.configurationDescriptor(0);
  check(le16(&cfg[2]) == 66, "composite_config_total_len");
  check(cfg[4] == 2, "composite_interface_count");
  check(cfg[9 + 2] == 0, "composite_keyboard_interface_number");
  check(cfg[41 + 2] == 1, "composite_mouse_interface_number");
  check(cfg[27 + 2] == 0x01 && cfg[34 + 2] == 0x82, "composite_keyboard_eps");
  check(cfg[59 + 2] == 0x83, "composite_mouse_ep");
}

static void testStringDescriptors()
{
  EspUsbDevice device;
  EspUsbDeviceConfig config;
  config.manufacturer = "EspUsb";
  config.product = "Device";
  config.serialNumber = nullptr;
  check(device.begin(config), "string_begin");

  const uint16_t *lang = device.stringDescriptor(0, 0);
  check((lang[0] & 0xff) == 4 && (lang[0] >> 8) == 0x03 && lang[1] == 0x0409, "string_lang");

  const uint16_t *manufacturer = device.stringDescriptor(1, 0x0409);
  check((manufacturer[0] & 0xff) == 14 && manufacturer[1] == 'E' && manufacturer[6] == 'b', "string_manufacturer");
  check(device.stringDescriptor(3, 0x0409) == nullptr, "string_serial_null");
}

void setup()
{
  Serial.begin(115200);
  delay(5000);

  Serial.println("TEST_BEGIN descriptor");
  testKeyboardDescriptor();
  testMouseDescriptor();
  testCompositeDescriptor();
  testStringDescriptors();
  Serial.printf("TEST_END pass=%d fail=%d\n", passCount, failCount);
  Serial.println(failCount == 0 ? "OK" : "NG");
  Serial.flush();
}

void loop()
{
}
