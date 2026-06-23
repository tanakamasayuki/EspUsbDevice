#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          Serial.printf("HOST_CONNECTED vid=%04x pid=%04x\n", device.vid, device.pid);
                        });

  usb.onSystemControl([](const EspUsbHostSystemControlEvent &event)
                      {
                        Serial.printf("SYSTEM usage=0x%02x pressed=%u released=%u\n",
                                      event.usage,
                                      event.pressed ? 1 : 0,
                                      event.released ? 1 : 0);
                      });

  if (!usb.begin())
  {
    Serial.printf("HOST_BEGIN_FAILED %s\n", usb.lastErrorName());
  }
}

void loop()
{
  delay(1);
}
