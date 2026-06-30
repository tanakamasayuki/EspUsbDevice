#include "EspUsbDevice.h"

static void compileApiSmoke()
{
  EspUsbDevice device;
  EspUsbDeviceHidKeyboard keyboard(device);
  EspUsbDeviceHidMouse mouse(device);
  EspUsbDeviceVendor vendor(device);
  const uint8_t customDescriptor[] = {
      0x05, 0x01,
      0x09, 0x04,
      0xa1, 0x01,
      0xc0,
  };
  EspUsbDeviceHidCustom customHid(device, customDescriptor, sizeof(customDescriptor), 8);

  EspUsbDeviceConfig config;
  config.port = ESP_USB_DEVICE_PORT_FULL_SPEED;
  config.speed = ESP_USB_DEVICE_SPEED_FULL;
  config.vid = 0x303a;
  config.pid = 0x4000;
  config.webusbEnabled = true;
  config.webusbUrl = "example.com/espusbdevice";

  EspUsbDeviceBootKeyboardReport keyboardReport;
  keyboardReport.modifiers = ESP_USB_DEVICE_MOD_LEFT_SHIFT;
  keyboardReport.keys[0] = ESP_USB_HID_KEY_A;

  EspUsbDeviceBootMouseReport mouseReport;
  mouseReport.buttons = ESP_USB_DEVICE_MOUSE_LEFT;
  mouseReport.x = 1;

  (void)config;
  (void)keyboardReport;
  (void)mouseReport;
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
  (void)vendor.available();
  (void)vendor.write(static_cast<uint8_t>('x'));
  vendor.onRx([](size_t available)
              {
                (void)available;
              });
  vendor.onControlRequest([&vendor](const EspUsbDeviceVendorControlRequest &request)
                          {
                            return vendor.sendControlResponse(request);
                          });
  const uint8_t customReport[8] = {};
  (void)customHid.sendReport(customReport, sizeof(customReport));
}

void setup()
{
  Serial.begin(115200);
  delay(1500);

  Serial.println("TEST_BEGIN compile_smoke");
  Serial.println("TEST_END");
  Serial.println("OK");
}

void loop()
{
}
