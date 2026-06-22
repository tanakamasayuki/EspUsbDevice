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

  usb.onMouse([](const EspUsbHostMouseEvent &event)
              {
                Serial.printf("MOUSE x=%d y=%d wheel=%d buttons=%u previous=%u moved=%u changed=%u\n",
                              event.x,
                              event.y,
                              event.wheel,
                              event.buttons,
                              event.previousButtons,
                              event.moved ? 1 : 0,
                              event.buttonsChanged ? 1 : 0);
              });

  usb.onHIDInput([](const EspUsbHostHIDInput &input)
                 {
                   Serial.printf("RAW len=%u", static_cast<unsigned>(input.length));
                   for (size_t i = 0; i < input.length; i++)
                   {
                     Serial.printf(" %02x", input.data[i]);
                   }
                   Serial.println();
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
