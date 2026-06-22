#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceHidMouse mouse(device);

void setup()
{
  Serial.begin(115200);
  delay(1500);

  EspUsbDeviceConfig config;
  config.port = ESP_USB_DEVICE_PORT_FULL_SPEED;
  config.speed = ESP_USB_DEVICE_SPEED_FULL;
  config.vid = 0x303a;
  config.pid = 0x4002;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Mouse";
  config.serialNumber = "espusb-mouse";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB mouse ready");
}

void loop()
{
  static uint8_t step = 0;
  static uint32_t lastSendMs = 0;
  const uint32_t now = millis();
  if (now - lastSendMs < 1000)
  {
    delay(1);
    return;
  }
  lastSendMs = now;

  bool ok = false;
  switch (step++ % 6)
  {
  case 0:
    ok = mouse.move(30, 0);
    break;
  case 1:
    ok = mouse.move(0, 30);
    break;
  case 2:
    ok = mouse.move(-30, 0);
    break;
  case 3:
    ok = mouse.move(0, -30);
    break;
  case 4:
    ok = mouse.wheel(1);
    break;
  default:
    ok = mouse.press(ESP_USB_DEVICE_MOUSE_LEFT);
    delay(50);
    ok = ok && mouse.release(ESP_USB_DEVICE_MOUSE_LEFT);
    break;
  }

  if (!ok)
  {
    Serial.printf("MOUSE_FAILED error=%s\n", device.lastErrorName());
  }
}
