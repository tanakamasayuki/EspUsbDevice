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

void loop()
{
  if (Serial.available() > 0)
  {
    char command = Serial.read();
    if (command == '?')
    {
      Serial.println("DEVICE_READY");
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
