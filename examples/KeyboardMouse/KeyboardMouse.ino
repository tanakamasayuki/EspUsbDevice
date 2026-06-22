#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceHidMouse mouse(device);

static void tapKey(uint8_t usage, uint8_t modifiers = 0)
{
  if (!keyboard.pressUsage(usage, modifiers))
  {
    Serial.printf("KEY_FAILED usage=0x%02x error=%s\n", usage, device.lastErrorName());
    return;
  }
  keyboard.releaseAll();
}

void setup()
{
  Serial.begin(115200);
  delay(1500);

  EspUsbDeviceConfig config;
  config.port = ESP_USB_DEVICE_PORT_FULL_SPEED;
  config.speed = ESP_USB_DEVICE_SPEED_FULL;
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

  tapKey(ESP_USB_HID_KEY_K);
  if (!mouse.move(40, 0))
  {
    Serial.printf("MOVE_FAILED error=%s\n", device.lastErrorName());
  }
  if (!mouse.click(ESP_USB_DEVICE_MOUSE_LEFT))
  {
    Serial.printf("CLICK_FAILED error=%s\n", device.lastErrorName());
  }
}
