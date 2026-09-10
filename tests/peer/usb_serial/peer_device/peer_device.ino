#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceCdcSerial UsbSerial(device);

volatile bool lineCodingSeen = false;
volatile uint32_t lineCodingBaud = 0;
volatile uint8_t lineCodingStopBits = 0;
volatile uint8_t lineCodingParity = 0;
volatile uint8_t lineCodingDataBits = 0;

void setup()
{
  Serial.begin(115200);
  delay(500);

  UsbSerial.onLineCoding([](const EspUsbDeviceCdcLineCoding &lineCoding)
                         {
                           lineCodingSeen = true;
                           lineCodingBaud = lineCoding.baud;
                           lineCodingStopBits = lineCoding.stopBits;
                           lineCodingParity = lineCoding.parity;
                           lineCodingDataBits = lineCoding.dataBits;
                         });

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4016;
  config.manufacturer = "EspUsbDevice";
  config.product = "EspUsbDevice USB Serial";
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
  if (Serial.available() > 0)
  {
    char command = Serial.read();
    const bool hostReady = waitForHost();
    if (command == '?')
    {
      Serial.printf("DEVICE_READY %u\n", hostReady ? 1 : 0);
    }
    else if (command == 'd')
    {
      const uint8_t payload[] = "device to host";
      Serial.printf("DEVICE_TX %u\n", UsbSerial.write(payload, sizeof(payload) - 1) == sizeof(payload) - 1 ? 1 : 0);
    }
    else if (command == 'l')
    {
      Serial.printf("DEVICE_LINE_CODING seen=%u baud=%lu stop=%u parity=%u data=%u\n",
                    lineCodingSeen ? 1 : 0,
                    static_cast<unsigned long>(lineCodingBaud),
                    lineCodingStopBits,
                    lineCodingParity,
                    lineCodingDataBits);
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
