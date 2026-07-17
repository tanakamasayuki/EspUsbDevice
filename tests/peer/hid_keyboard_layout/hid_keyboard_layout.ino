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

  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event)
                 {
                   if (event.pressed)
                   {
                     Serial.printf("RAW_KEY ascii=0x%02x keycode=0x%02x modifiers=0x%02x\n",
                                   event.ascii,
                                   event.keycode,
                                   event.modifiers);
                     if (event.ascii)
                     {
                       Serial.printf("KEY %c keycode=0x%02x modifiers=0x%02x\n",
                                     static_cast<char>(event.ascii),
                                     event.keycode,
                                     event.modifiers);
                     }
                   }
                 });

  usb.onHIDInput([](const EspUsbHostHIDInput &input)
                 {
                   if (input.length >= 8)
                   {
                     Serial.printf("HID_INPUT len=%u bytes=%02x %02x %02x %02x %02x %02x %02x %02x\n",
                                   static_cast<unsigned>(input.length),
                                   input.data[0],
                                   input.data[1],
                                   input.data[2],
                                   input.data[3],
                                   input.data[4],
                                   input.data[5],
                                   input.data[6],
                                   input.data[7]);
                   }
                 });

  if (!usb.begin())
  {
    Serial.printf("HOST_BEGIN_FAILED %s\n", usb.lastErrorName());
  }
}

void loop()
{
  while (Serial.available() > 0)
  {
    const char command = static_cast<char>(Serial.read());
    if (command == 'E')
    {
      usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_EN_US);
      Serial.println("HOST_LAYOUT EN_US");
    }
    else if (command == 'J')
    {
      usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_JA_JP);
      Serial.println("HOST_LAYOUT JA_JP");
    }
    else if (command == 'D')
    {
      usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_DE_DE);
      Serial.println("HOST_LAYOUT DE_DE");
    }
    else if (command == 'B')
    {
      usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_PT_BR);
      Serial.println("HOST_LAYOUT PT_BR");
    }
  }
  delay(1);
}
