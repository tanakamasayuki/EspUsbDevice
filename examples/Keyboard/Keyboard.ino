#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

void setup()
{
  Serial.begin(115200);
  delay(1500);

  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);

  // The callback fires the moment the host changes the Lock LEDs. It is a single
  // slot, so keyboard.ledState() below is the way to read the same state when
  // something else (an integration layer, say) already took it.
  keyboard.onOutputReport([](const EspUsbDeviceHidKeyboardOutputReport &report)
                          {
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
  // ledState() holds the latest host output report - no need to mirror it into a
  // global from the callback.
  Serial.printf("last_leds=0x%02x caps=%u\n",
                keyboard.ledState().leds,
                keyboard.ledState().capsLock ? 1 : 0);
}
