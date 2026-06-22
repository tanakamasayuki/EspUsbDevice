#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceHidMouse mouse(device);

static bool sendMove(int8_t x, int8_t y, int8_t wheel = 0, uint8_t buttons = 0)
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

static bool sendClick(uint8_t button)
{
  if (!sendMove(0, 0, 0, button))
  {
    return false;
  }
  delay(50);
  return sendMove(0, 0, 0, 0);
}

void setup()
{
  Serial.begin(115200);
  delay(5000);

  EspUsbDeviceConfig config;
  config.port = ESP_USB_DEVICE_PORT_FULL_SPEED;
  config.speed = ESP_USB_DEVICE_SPEED_FULL;
  config.vid = 0x303a;
  config.pid = 0x4002;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Mouse";
  config.serialNumber = "espusb-mouse";

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
    case 'r':
      ok = sendMove(40, 0, 0);
      break;
    case 'l':
      ok = sendMove(-40, 0, 0);
      break;
    case 'u':
      ok = sendMove(0, -40, 0);
      break;
    case 'd':
      ok = sendMove(0, 40, 0);
      break;
    case 'w':
      ok = sendMove(0, 0, 1);
      break;
    case 'm':
      ok = sendClick(ESP_USB_DEVICE_MOUSE_LEFT);
      break;
    case 'R':
      ok = sendClick(ESP_USB_DEVICE_MOUSE_RIGHT);
      break;
    case 'M':
      ok = sendClick(ESP_USB_DEVICE_MOUSE_MIDDLE);
      break;
    case 'b':
      ok = sendClick(ESP_USB_DEVICE_MOUSE_BACK);
      break;
    case 'f':
      ok = sendClick(ESP_USB_DEVICE_MOUSE_FORWARD);
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
