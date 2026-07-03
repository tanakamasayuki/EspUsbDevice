#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceHidMouse mouse(device);

static bool sendKeyboardUsage(uint8_t usage, uint8_t modifiers = 0)
{
  const uint32_t start = millis();
  while (millis() - start < 3000)
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

static bool sendMouseMove(int8_t x, int8_t y, int8_t wheel = 0, uint8_t buttons = 0)
{
  const uint32_t start = millis();
  while (millis() - start < 3000)
  {
    if (mouse.move(x, y, wheel, buttons))
    {
      return true;
    }
    delay(5);
  }
  return false;
}

static bool sendMouseClick(uint8_t button)
{
  if (!sendMouseMove(0, 0, 0, button))
  {
    return false;
  }
  delay(50);
  return sendMouseMove(0, 0, 0, 0);
}

void setup()
{
  Serial.begin(115200);
  delay(5000);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4003;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Keyboard Mouse";
  config.serialNumber = "espusb-kbd-mouse";

  Serial.printf("DEVICE_BEGIN %u\n", device.begin(config) ? 1 : 0);
}

void loop()
{
  while (Serial.available() > 0)
  {
    char command = static_cast<char>(Serial.read());
    if (command == '\r' || command == '\n')
    {
      continue;
    }

    bool ok = true;
    switch (command)
    {
    case 'k':
      ok = sendKeyboardUsage(ESP_USB_HID_KEY_K);
      break;
    case 'r':
      ok = sendMouseMove(40, 0, 0);
      break;
    case 'm':
      ok = sendMouseClick(ESP_USB_DEVICE_MOUSE_LEFT);
      break;
    default:
      ok = false;
      break;
    }

    Serial.printf("CMD %c %u\n", command, ok ? 1 : 0);
    if (!ok)
    {
      Serial.printf("SEND_FAILED %c\n", command);
    }
  }
  delay(1);
}
