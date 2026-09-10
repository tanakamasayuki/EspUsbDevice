#include "EspUsbHost.h"

EspUsbHost usb;

// Latched from the connect event so a test can ask whether the peer is attached
// instead of having to be the one that saw it arrive.
static volatile uint8_t deviceAddress = 0;
static uint16_t deviceVid = 0;
static uint16_t devicePid = 0;

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          deviceAddress = device.address;
                          deviceVid = device.vid;
                          devicePid = device.pid;
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

// Block until the peer has been enumerated, so every command below answers about
// a device that is actually attached, whatever order the tests run in.
//
// deviceAddress is latched in onDeviceConnected, which fires after the host has
// claimed the interfaces - the right side of the event for anything that reads
// the device's interfaces or endpoints. Waiting here rather than announcing once
// at boot is what lets a test run in any position: a boot announcement is only
// visible to whichever test happens to be first.
static bool waitForDevice(uint32_t timeoutMs = 5000)
{
  const uint32_t startedAt = millis();
  while (deviceAddress == 0 && millis() - startedAt < timeoutMs)
  {
    delay(10);
  }
  return deviceAddress != 0;
}

void loop()
{
  while (Serial.available() > 0)
  {
    const char command = static_cast<char>(Serial.read());
    const bool attached = waitForDevice();
    if (command == '?')
    {
      Serial.printf("HOST_READY %u vid=%04x pid=%04x\n", attached ? 1 : 0, deviceVid, devicePid);
    }
    else if (command == 'E')
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
