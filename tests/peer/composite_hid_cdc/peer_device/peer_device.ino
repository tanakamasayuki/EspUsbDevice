#include "EspUsbDevice.h"

// Composite device: HID keyboard + CDC ACM on one EspUsbDevice.
// Pairs with composite_hid_cdc.ino (host). See tests/TEST_PLAN.ja.md
// "複合デバイス（composite）テスト".

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceCdcSerial UsbSerial(device);

static bool beginOk = false;
static const char *beginError = "ESP_OK";

// Block until the host has configured us, so every command below answers from a
// usable device whatever order the tests run in.
//
// device.ready() is tud_mounted(): the host completed SET_CONFIGURATION. That is
// the event the tests actually depend on, and it is deliberately answered on
// demand rather than announced once at boot - a test that does not run first
// never sees an announcement, which is what made most of this suite fail when
// its modules were run in reverse. tests/peer/usb_msc has had this shape all
// along and is the only peer module that survived that check.
static bool waitForHost(uint32_t timeoutMs = 5000)
{
  const uint32_t started = millis();
  while (!device.ready() && millis() - started < timeoutMs)
  {
    device.task();
    delay(10);
  }
  return device.ready();
}

static bool tapKeyWithRetry(char c)
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
  delay(500);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4020;
  config.manufacturer = "EspUsbDevice";
  config.product = "EspUsbDevice HID+CDC";
  config.serialNumber = "espusb-hid-cdc";

  beginOk = device.begin(config);
  beginError = device.lastErrorName();
  // Common composite output: ok|ng + error name for diagnosis.
  // Also queryable on demand via the 'b' command, since this boot-time line
  // can scroll past before the first test attaches its serial reader.
  Serial.printf("DEVICE_BEGIN %s %s\n", beginOk ? "ok" : "ng", beginError);
}

void loop()
{
  if (Serial.available() > 0)
  {
    char command = Serial.read();
    const bool hostReady = waitForHost();
    if (command == '?')
    {
      Serial.printf("DEVICE_READY %u\n", hostReady ? 1 : 0);
    }
    else if (command == 'b')
    {
      Serial.printf("DEVICE_BEGIN %s %s\n", beginOk ? "ok" : "ng", beginError);
    }
    else if (command == 'k')
    {
      Serial.printf("DEVICE_KEY %u\n", tapKeyWithRetry('a') ? 1 : 0);
    }
    else if (command == 'd')
    {
      const uint8_t payload[] = "device to host";
      Serial.printf("DEVICE_TX %u\n", UsbSerial.write(payload, sizeof(payload) - 1) == sizeof(payload) - 1 ? 1 : 0);
    }
  }

  while (UsbSerial.available() > 0)
  {
    Serial.print("DEVICE_RX ");
    while (UsbSerial.available() > 0)
    {
      Serial.write(UsbSerial.read());
    }
    Serial.println();
  }
  delay(1);
}
