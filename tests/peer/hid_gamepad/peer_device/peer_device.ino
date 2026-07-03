#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceHidGamepad gamepad(device);

static bool sendGamepad(int8_t x,
                        int8_t y,
                        int8_t z,
                        int8_t rz,
                        int8_t rx,
                        int8_t ry,
                        uint8_t hat,
                        uint32_t buttons)
{
  const uint32_t start = millis();
  while (millis() - start < 3000)
  {
    if (gamepad.send(x, y, z, rz, rx, ry, hat, buttons))
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
  config.vid = 0x303a;
  config.pid = 0x4008;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Gamepad";
  config.serialNumber = "espusb-gamepad";

  Serial.printf("DEVICE_BEGIN %u\n", device.begin(config) ? 1 : 0);
}

void loop()
{
  while (Serial.available() > 0)
  {
    const char command = static_cast<char>(Serial.read());
    if (command == '\r' || command == '\n')
    {
      continue;
    }

    bool ok = false;
    switch (command)
    {
    case 'a':
      ok = sendGamepad(10, -10, 20, -20, 30, -30, ESP_USB_DEVICE_GAMEPAD_HAT_RIGHT, 0x00000005);
      break;
    case '0':
      ok = sendGamepad(0, 0, 0, 0, 0, 0, ESP_USB_DEVICE_GAMEPAD_HAT_CENTER, 0);
      break;
    case '1':
      ok = sendGamepad(0, 0, 0, 0, 0, 0, ESP_USB_DEVICE_GAMEPAD_HAT_UP, 0);
      break;
    case '2':
      ok = sendGamepad(0, 0, 0, 0, 0, 0, ESP_USB_DEVICE_GAMEPAD_HAT_UP_RIGHT, 0);
      break;
    case '3':
      ok = sendGamepad(0, 0, 0, 0, 0, 0, ESP_USB_DEVICE_GAMEPAD_HAT_RIGHT, 0);
      break;
    case '4':
      ok = sendGamepad(0, 0, 0, 0, 0, 0, ESP_USB_DEVICE_GAMEPAD_HAT_DOWN_RIGHT, 0);
      break;
    case '5':
      ok = sendGamepad(0, 0, 0, 0, 0, 0, ESP_USB_DEVICE_GAMEPAD_HAT_DOWN, 0);
      break;
    case '6':
      ok = sendGamepad(0, 0, 0, 0, 0, 0, ESP_USB_DEVICE_GAMEPAD_HAT_DOWN_LEFT, 0);
      break;
    case '7':
      ok = sendGamepad(0, 0, 0, 0, 0, 0, ESP_USB_DEVICE_GAMEPAD_HAT_LEFT, 0);
      break;
    case '8':
      ok = sendGamepad(0, 0, 0, 0, 0, 0, ESP_USB_DEVICE_GAMEPAD_HAT_UP_LEFT, 0);
      break;
    case 'b':
      ok = sendGamepad(0, 0, 0, 0, 0, 0, ESP_USB_DEVICE_GAMEPAD_HAT_CENTER, 0x00007fff);
      break;
    default:
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
