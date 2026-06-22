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
  const EspUsbDeviceKeyboardLayout layouts[] = {
      ESP_USB_DEVICE_KEYBOARD_LAYOUT_ZH_TW,
      ESP_USB_DEVICE_KEYBOARD_LAYOUT_DA_DK,
      ESP_USB_DEVICE_KEYBOARD_LAYOUT_DE_DE,
      ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US,
      ESP_USB_DEVICE_KEYBOARD_LAYOUT_FI_FI,
      ESP_USB_DEVICE_KEYBOARD_LAYOUT_FR_FR,
      ESP_USB_DEVICE_KEYBOARD_LAYOUT_HU_HU,
      ESP_USB_DEVICE_KEYBOARD_LAYOUT_IT_IT,
      ESP_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP,
      ESP_USB_DEVICE_KEYBOARD_LAYOUT_KO_KR,
      ESP_USB_DEVICE_KEYBOARD_LAYOUT_NL_NL,
      ESP_USB_DEVICE_KEYBOARD_LAYOUT_NB_NO,
      ESP_USB_DEVICE_KEYBOARD_LAYOUT_PT_BR,
      ESP_USB_DEVICE_KEYBOARD_LAYOUT_SV_SE,
      ESP_USB_DEVICE_KEYBOARD_LAYOUT_ZH_CN,
      ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_GB,
      ESP_USB_DEVICE_KEYBOARD_LAYOUT_PT_PT,
      ESP_USB_DEVICE_KEYBOARD_LAYOUT_ES_ES,
      ESP_USB_DEVICE_KEYBOARD_LAYOUT_FR_CH,
  };
  for (EspUsbDeviceKeyboardLayout layout : layouts)
  {
    keyboard.setLayout(layout);
    (void)keyboard.write("@[]:\"");
  }
  (void)keyboard.tapUsage(ESP_USB_HID_KEY_LANG1);
  (void)keyboard.pressKey('Z');
  (void)keyboard.releaseAll();
  (void)mouse.click(ESP_USB_DEVICE_MOUSE_LEFT);
  (void)mouse.press(ESP_USB_DEVICE_MOUSE_RIGHT);
  (void)mouse.wheel(1);
  (void)mouse.releaseAll();

  Serial.println("TEST_BEGIN compile_smoke");
  Serial.println("TEST_END");
  Serial.println("OK");
}

void loop()
{
}
