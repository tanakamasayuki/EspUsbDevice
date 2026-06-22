#include "EspUsbDevice.h"

void setup()
{
  Serial.begin(115200);

  EspUsbDeviceConfig config;
  config.port = ESP_USB_DEVICE_PORT_FULL_SPEED;
  config.speed = ESP_USB_DEVICE_SPEED_FULL;
  config.vid = 0x303a;
  config.pid = 0x4000;

  EspUsbDeviceBootKeyboardReport keyboardReport;
  keyboardReport.modifiers = ESP_USB_DEVICE_MOD_LEFT_SHIFT;
  keyboardReport.keys[0] = ESP_USB_HID_KEY_A;

  EspUsbDeviceBootMouseReport mouseReport;
  mouseReport.buttons = ESP_USB_DEVICE_MOUSE_LEFT;
  mouseReport.x = 1;

  Serial.println("TEST_BEGIN compile_smoke");
  Serial.println("TEST_END");
  Serial.println("OK");
}

void loop()
{
}
