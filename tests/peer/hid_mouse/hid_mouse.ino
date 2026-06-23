#include "EspUsbHost.h"

EspUsbHost usb;

static void printBytes(const uint8_t *data, size_t length, size_t maxLength)
{
  const size_t count = length < maxLength ? length : maxLength;
  for (size_t i = 0; i < count; i++)
  {
    Serial.printf("%s%02x", i == 0 ? "" : " ", data[i]);
  }
}

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

  usb.onHIDReportDescriptor([](const EspUsbHostHIDReportDescriptor &descriptor)
                            {
                              Serial.printf("HID_DESC iface=%u reported=%u len=%u first=%02x last=%02x\n",
                                            descriptor.interfaceNumber,
                                            descriptor.reportedLength,
                                            descriptor.length,
                                            descriptor.length > 0 ? descriptor.data[0] : 0,
                                            descriptor.length > 0 ? descriptor.data[descriptor.length - 1] : 0);
                            });

  usb.onHIDInput([](const EspUsbHostHIDInput &input)
                 {
                   Serial.printf("HID_INPUT iface=%u subclass=%u protocol=%u len=%u data=",
                                 input.interfaceNumber,
                                 input.subclass,
                                 input.protocol,
                                 static_cast<unsigned>(input.length));
                   printBytes(input.data, input.length, 8);
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
