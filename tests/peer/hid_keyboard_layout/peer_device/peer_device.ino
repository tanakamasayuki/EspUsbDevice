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

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4009;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Keyboard Layout";
  config.serialNumber = "espusb-kbd-layout";

  Serial.printf("DEVICE_BEGIN %u\n", device.begin(config) ? 1 : 0);
}

// Block until the host has configured us, so every command below answers from a
// usable device whatever order the tests run in.
//
// device.ready() is tud_mounted(): the host completed SET_CONFIGURATION. That is
// the event the tests actually depend on, and it is answered on demand rather
// than announced once at boot - a test that does not run first never sees an
// announcement, which is what made most of this suite fail when its modules were
// run in reverse. tests/peer/usb_msc has had this shape all along and was the
// only peer module that survived that check.
static bool waitForHost(uint32_t timeoutMs = 5000)
{
  const uint32_t startedAt = millis();
  while (!device.ready() && millis() - startedAt < timeoutMs)
  {
    device.task();
    delay(10);
  }
  return device.ready();
}

void loop()
{
  while (Serial.available() > 0)
  {
    const char command = static_cast<char>(Serial.read());
    const bool hostReady = waitForHost();
    if (command == 'E')
    {
      keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);
      Serial.println("DEVICE_LAYOUT EN_US");
    }
    else if (command == 'J')
    {
      keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP);
      Serial.println("DEVICE_LAYOUT JA_JP");
    }
    else if (command == 'D')
    {
      keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_DE_DE);
      Serial.println("DEVICE_LAYOUT DE_DE");
    }
    else if (command == 'B')
    {
      keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_PT_BR);
      Serial.println("DEVICE_LAYOUT PT_BR");
    }
    else if (command == '?')
    {
      Serial.printf("DEVICE_READY %u\n", hostReady ? 1 : 0);
    }
    else if (command != '\r' && command != '\n')
    {
      const bool ok = sendAscii(command);
      Serial.printf("SEND %c %u\n", command, ok ? 1 : 0);
      if (!ok)
      {
        Serial.printf("SEND_FAILED %d\n", static_cast<int>(command));
      }
    }
  }
  delay(1);
}
