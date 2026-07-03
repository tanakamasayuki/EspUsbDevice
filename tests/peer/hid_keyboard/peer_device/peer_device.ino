#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

static bool sendAscii(char c)
{
  const uint32_t start = millis();
  while (millis() - start < 1000)
  {
    if (keyboard.tapKey(c))
    {
      return true;
    }
    delay(5);
  }
  return false;
}

void setup()
{
  Serial.begin(115200);
  delay(5000);

  keyboard.onOutputReport([](const EspUsbDeviceHidKeyboardOutputReport &report)
                          {
                            Serial.printf("LED numlock=%u capslock=%u scrolllock=%u\n",
                                          report.numLock ? 1 : 0,
                                          report.capsLock ? 1 : 0,
                                          report.scrollLock ? 1 : 0);
                          });
  keyboard.onProtocol([](const EspUsbDeviceHidProtocolEvent &event)
                      {
                        Serial.printf("PROTOCOL instance=%u protocol=%u\n", event.instance, event.protocol);
                      });

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4001;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Keyboard";
  config.serialNumber = "espusb-kbd";

  Serial.printf("DEVICE_BEGIN %u\n", device.begin(config) ? 1 : 0);
}

void loop()
{
  while (Serial.available() > 0)
  {
    char c = static_cast<char>(Serial.read());
    if (!sendAscii(c))
    {
      Serial.printf("SEND_FAILED %d\n", static_cast<int>(c));
    }
  }
  delay(1);
}
