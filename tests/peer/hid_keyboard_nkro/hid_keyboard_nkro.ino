#include "EspUsbHost.h"

// Host side (DUT) of the NKRO peer test. Reports each pressed keycode and how
// many keys are held at the same time, plus whether the enumerated report is a
// bitmap. This is the EspUsbDevice-repo copy; the assertions in
// test_hid_keyboard_nkro.py differ from the EspUsbHost-repo copy: they check the
// exact set of held keycodes (identity, not just count) and that high-usage
// International / LANG (JIS) keys traverse the full 0x00-0xDF bitmap.

EspUsbHost usb;

static volatile int pressedCount = 0;
static volatile int maxSimultaneous = 0;
static volatile uint8_t connectedAddress = 0;

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_EN_US);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          connectedAddress = device.address;
                          Serial.printf("HOST_CONNECTED vid=%04x pid=%04x\n", device.vid, device.pid);
                        });
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             (void)device;
                             connectedAddress = 0;
                             pressedCount = 0;
                             Serial.println("HOST_DISCONNECTED");
                           });

  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event)
                 {
                   if (event.pressed)
                   {
                     pressedCount++;
                     if (pressedCount > maxSimultaneous)
                     {
                       maxSimultaneous = pressedCount;
                     }
                     Serial.printf("PRESS keycode=0x%02x n=%d\n", event.keycode, pressedCount);
                   }
                   else
                   {
                     if (pressedCount > 0)
                     {
                       pressedCount--;
                     }
                     Serial.printf("RELEASE keycode=0x%02x n=%d\n", event.keycode, pressedCount);
                   }
                 });

  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == 'i')
    {
      Serial.printf("NKRO bitmap=%u\n", usb.keyboardUsesBitmapReport(connectedAddress) ? 1 : 0);
    }
    else if (command == 'r')
    {
      pressedCount = 0;
      maxSimultaneous = 0;
      Serial.println("RESET");
    }
    else if (command == 'm')
    {
      Serial.printf("MAX n=%d\n", maxSimultaneous);
    }
  }
  delay(1);
}
