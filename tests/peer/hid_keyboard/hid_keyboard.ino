#include "EspUsbHost.h"
#include <string.h>

EspUsbHost usb;
static char textBuffer[32] = {};
static size_t textLength = 0;
static uint8_t keyboardAddress = 0;
static uint8_t keyboardInterface = 0;

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
                          keyboardAddress = device.address;
                        });

  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event)
                 {
                   if (event.pressed && event.ascii)
                   {
                     if (textLength + 1 < sizeof(textBuffer))
                     {
                       textBuffer[textLength++] = static_cast<char>(event.ascii);
                       textBuffer[textLength] = '\0';
                     }
                     if (strcmp(textBuffer, "hello, keyboard") == 0)
                     {
                       Serial.println(textBuffer);
                     }
                   }
                 });

  usb.onHIDReportDescriptor([](const EspUsbHostHIDReportDescriptor &descriptor)
                            {
                              Serial.printf("HID_DESC iface=%u reported=%u len=%u first=%02x last=%02x\n",
                                            descriptor.interfaceNumber,
                                            descriptor.reportedLength,
                                            descriptor.length,
                                            descriptor.length > 0 ? descriptor.data[0] : 0,
                                            descriptor.length > 0 ? descriptor.data[descriptor.length - 1] : 0);
                              keyboardInterface = descriptor.interfaceNumber;
                            });

  usb.onHIDInput([](const EspUsbHostHIDInput &input)
                 {
                   Serial.printf("HID_INPUT iface=%u subclass=%u protocol=%u len=%u data=",
                                 input.interfaceNumber,
                                 input.subclass,
                                 input.protocol,
                                 static_cast<unsigned>(input.length));
                   printBytes(input.data, input.length, 9);
                   Serial.println();
                 });

  if (!usb.begin())
  {
    Serial.printf("HOST_BEGIN_FAILED %s\n", usb.lastErrorName());
  }
}

void loop()
{
  if (Serial.available() > 0)
  {
    char command = Serial.read();
    if (command == 'n')
    {
      Serial.printf("LED_TX %u\n", usb.setKeyboardLeds(true, false, false) ? 1 : 0);
    }
    else if (command == 'c')
    {
      Serial.printf("LED_TX %u\n", usb.setKeyboardLeds(false, true, false) ? 1 : 0);
    }
    else if (command == 's')
    {
      Serial.printf("LED_TX %u\n", usb.setKeyboardLeds(false, false, true) ? 1 : 0);
    }
    else if (command == '0')
    {
      Serial.printf("LED_TX %u\n", usb.setKeyboardLeds(false, false, false) ? 1 : 0);
    }
    else if (command == 'p')
    {
      Serial.printf("PROTOCOL_TX %u iface=%u address=%u\n",
                    usb.sendSetProtocol(keyboardInterface, keyboardAddress) ? 1 : 0,
                    keyboardInterface,
                    keyboardAddress);
    }
  }
  delay(1);
}
