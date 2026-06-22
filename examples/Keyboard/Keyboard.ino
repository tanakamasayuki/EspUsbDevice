#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

static uint8_t ledState = 0;

static void sendUsage(uint8_t usage, uint8_t modifiers = 0)
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
  config.port = ESP_USB_DEVICE_PORT_FULL_SPEED;
  config.speed = ESP_USB_DEVICE_SPEED_FULL;
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

  sendUsage(ESP_USB_HID_KEY_H, ESP_USB_DEVICE_MOD_LEFT_SHIFT);
  sendUsage(ESP_USB_HID_KEY_I);
  Serial.printf("last_leds=0x%02x\n", ledState);
}
