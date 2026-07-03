#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

static uint8_t ledState = 0;

void setup()
{
  Serial.begin(115200);
  delay(1500);

  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);

  keyboard.onOutputReport([](const EspUsbDeviceHidKeyboardOutputReport &report)
                          {
                            ledState = report.leds;
                            Serial.printf("LEDS num=%u caps=%u scroll=%u raw=0x%02x\n",
                                          report.numLock ? 1 : 0,
                                          report.capsLock ? 1 : 0,
                                          report.scrollLock ? 1 : 0,
                                          report.leds);
                          });

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4001;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Keyboard";
  config.serialNumber = "espusb-keyboard";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB keyboard ready");
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

  if (!keyboard.write("Hi from EspUsbDevice\n"))
  {
    Serial.printf("WRITE_FAILED error=%s\n", device.lastErrorName());
  }

  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP);
  keyboard.write("@[]:\"");
  keyboard.tapKey('\n');
  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);

  // Raw HID usage remains available for keys that are not part of the ASCII wrapper.
  keyboard.tapUsage(ESP_USB_HID_KEY_LANG1);
  Serial.printf("last_leds=0x%02x\n", ledState);
}
