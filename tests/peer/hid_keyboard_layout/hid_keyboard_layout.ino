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
                   if (event.pressed && event.ascii)
                   {
                     Serial.printf("KEY %c keycode=0x%02x modifiers=0x%02x\n",
                                   static_cast<char>(event.ascii),
                                   event.keycode,
                                   event.modifiers);
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
  }
  delay(1);
}
