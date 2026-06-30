#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

static bool sendAscii(char c)
{
  const uint32_t start = millis();
  while (millis() - start < 1000)
  {
    if (keyboard.tapKey(c))
    {
      return true;
    }
    delay(5);
  }
  return false;
}

void setup()
{
  Serial.begin(115200);
  delay(5000);

  EspUsbDeviceConfig config;
  config.port = ESP_USB_DEVICE_PORT_FULL_SPEED;
  config.speed = ESP_USB_DEVICE_SPEED_FULL;
  config.vid = 0x303a;
  config.pid = 0x4009;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Keyboard Layout";
  config.serialNumber = "espusb-kbd-layout";

  Serial.printf("DEVICE_BEGIN %u\n", device.begin(config) ? 1 : 0);
}

void loop()
{
  while (Serial.available() > 0)
  {
    const char command = static_cast<char>(Serial.read());
    if (command == 'E')
    {
      keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);
      Serial.println("DEVICE_LAYOUT EN_US");
    }
    else if (command == 'J')
    {
      keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP);
      Serial.println("DEVICE_LAYOUT JA_JP");
    }
    else if (command == '?')
    {
      Serial.println("DEVICE_READY");
    }
    else if (command != '\r' && command != '\n')
    {
      const bool ok = sendAscii(command);
      Serial.printf("SEND %c %u\n", command, ok ? 1 : 0);
      if (!ok)
      {
        Serial.printf("SEND_FAILED %d\n", static_cast<int>(command));
      }
    }
  }
  delay(1);
}
