#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

static bool sendUsage(uint8_t usage, uint8_t modifiers = 0)
{
  const uint32_t start = millis();
  while (millis() - start < 1000)
  {
    if (keyboard.pressUsage(usage, modifiers))
    {
      delay(10);
      keyboard.releaseAll();
      return true;
    }
    delay(5);
  }
  return false;
}

static bool sendAscii(char c)
{
  if (c >= 'a' && c <= 'z')
  {
    return sendUsage(static_cast<uint8_t>(ESP_USB_HID_KEY_A + (c - 'a')));
  }
  if (c >= 'A' && c <= 'Z')
  {
    return sendUsage(static_cast<uint8_t>(ESP_USB_HID_KEY_A + (c - 'A')), ESP_USB_DEVICE_MOD_LEFT_SHIFT);
  }
  if (c == ' ')
  {
    return sendUsage(0x2c);
  }
  if (c == ',')
  {
    return sendUsage(0x36);
  }
  return false;
}

void setup()
{
  Serial.begin(115200);
  delay(5000);

  keyboard.onOutputReport([](const EspUsbDeviceHidKeyboardOutputReport &report)
                          {
                            Serial.printf("LED numlock=%u capslock=%u scrolllock=%u\n",
                                          report.numLock ? 1 : 0,
                                          report.capsLock ? 1 : 0,
                                          report.scrollLock ? 1 : 0);
                          });

  EspUsbDeviceConfig config;
  config.port = ESP_USB_DEVICE_PORT_FULL_SPEED;
  config.speed = ESP_USB_DEVICE_SPEED_FULL;
  config.vid = 0x303a;
  config.pid = 0x4001;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Keyboard";
  config.serialNumber = "espusb-kbd";

  Serial.printf("DEVICE_BEGIN %u\n", device.begin(config) ? 1 : 0);
}

void loop()
{
  while (Serial.available() > 0)
  {
    char c = static_cast<char>(Serial.read());
    if (!sendAscii(c))
    {
      Serial.printf("SEND_FAILED %d\n", static_cast<int>(c));
    }
  }
  delay(1);
}
