#include "EspUsbDevice.h"

// N-key rollover keyboard: any number of keys can be held at once, unlike the
// 6-key boot report. Enable it with keyboard.enableNkro() before device.begin().
// The device still answers boot protocol (BIOS) with the 6-key format, and the
// bitmap covers usages 0x00-0xDF so International/LANG (JIS) keys work too.

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

// Ten simultaneous keys — impossible with the 6KRO boot report, easy with NKRO.
static const uint8_t chord[] = {
    ESP_USB_HID_KEY_A, ESP_USB_HID_KEY_S, ESP_USB_HID_KEY_D, ESP_USB_HID_KEY_F,
    ESP_USB_HID_KEY_G, ESP_USB_HID_KEY_H, ESP_USB_HID_KEY_J, ESP_USB_HID_KEY_K,
    ESP_USB_HID_KEY_L, ESP_USB_HID_KEY_SEMICOLON,
};

void setup()
{
  Serial.begin(115200);
  delay(1500);

  keyboard.enableNkro();
  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4033;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice NKRO Keyboard";
  config.serialNumber = "espusb-keyboard-nkro";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.printf("USB NKRO keyboard ready (nkro=%u)\n", keyboard.nkroEnabled() ? 1 : 0);
}

void loop()
{
  static uint32_t lastSendMs = 0;
  const uint32_t now = millis();
  if (now - lastSendMs < 4000)
  {
    delay(1);
    return;
  }
  lastSendMs = now;

  if (!device.ready())
  {
    return;
  }

  // Hold all ten keys down at the same time, then release them together.
  for (size_t i = 0; i < sizeof(chord); i++)
  {
    keyboard.pressUsage(chord[i]);
  }
  delay(200);
  keyboard.releaseAll();

  Serial.printf("sent %u-key chord (protocol=%s)\n",
                (unsigned)sizeof(chord),
                keyboard.protocol() == 0 ? "boot" : "report");
}
