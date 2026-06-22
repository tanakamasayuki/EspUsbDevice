#include "EspUsbDevice.h"

void setup()
{
  Serial.begin(115200);
  delay(1500);

  EspUsbDevice device;
  EspUsbDeviceHidKeyboard keyboard(device);
  EspUsbDeviceHidMouse mouse(device);

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

  (void)keyboard.tapKey('A');
  (void)keyboard.write("hello");
  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP);
  (void)keyboard.write("@[]:\"");
  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);
  (void)keyboard.tapUsage(ESP_USB_HID_KEY_LANG1);
  (void)mouse.click(ESP_USB_DEVICE_MOUSE_LEFT);

  Serial.println("TEST_BEGIN compile_smoke");
  Serial.println("TEST_END");
  Serial.println("OK");
}

void loop()
{
}
