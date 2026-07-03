#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceHidMouse mouse(device);

void setup()
{
  Serial.begin(115200);
  delay(1500);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4003;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Keyboard Mouse";
  config.serialNumber = "espusb-keyboard-mouse";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB keyboard + mouse ready");
}

void loop()
{
  static uint32_t lastSendMs = 0;
  const uint32_t now = millis();
  if (now - lastSendMs < 3000)
  {
    delay(1);
    return;
  }
  lastSendMs = now;

  if (!keyboard.write("K"))
  {
    Serial.printf("KEY_FAILED error=%s\n", device.lastErrorName());
  }
  if (!mouse.move(40, 0))
  {
    Serial.printf("MOVE_FAILED error=%s\n", device.lastErrorName());
  }
  if (!mouse.click(ESP_USB_DEVICE_MOUSE_LEFT))
  {
    Serial.printf("CLICK_FAILED error=%s\n", device.lastErrorName());
  }
}
