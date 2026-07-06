#include "EspUsbDevice.h"

// Composite device: HID keyboard + CDC ACM on one EspUsbDevice.
// Pairs with composite_hid_cdc.ino (host). See tests/TEST_PLAN.ja.md
// "複合デバイス（composite）テスト".

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceCdcSerial UsbSerial(device);

static bool beginOk = false;
static const char *beginError = "ESP_OK";

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
    if (command == '?')
    {
      Serial.println("DEVICE_READY");
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
