#include "EspUsbHost.h"

EspUsbHost usb;
EspUsbHostCdcSerial CdcSerial(usb);

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

  CdcSerial.begin(115200);

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
  if (Serial.available() > 0)
  {
    char command = Serial.read();
    const bool attached = waitForDevice();
    if (command == '?')
    {
      Serial.printf("HOST_READY %u vid=%04x pid=%04x\n", attached ? 1 : 0, deviceVid, devicePid);
    }
    else if (command == 'h')
    {
      Serial.printf("SERIAL_TX %u\n", CdcSerial.write(reinterpret_cast<const uint8_t *>("host to serial\n"), 15) == 15 ? 1 : 0);
    }
    else if (command == 'c')
    {
      EspUsbHostSerialConfig config;
      config.baud = 57600;
      config.dataBits = 7;
      config.parity = ESP_USB_HOST_SERIAL_PARITY_EVEN;
      config.stopBits = ESP_USB_HOST_SERIAL_STOP_BITS_2;
      Serial.printf("SERIAL_CONFIG %u\n", CdcSerial.setConfig(config) ? 1 : 0);
    }
    else if (command == 'm')
    {
      EspUsbHostSerialConfig config;
      config.baud = 300;
      config.dataBits = 5;
      config.parity = ESP_USB_HOST_SERIAL_PARITY_MARK;
      config.stopBits = ESP_USB_HOST_SERIAL_STOP_BITS_1_5;
      Serial.printf("SERIAL_CONFIG_MARK %u\n", CdcSerial.setConfig(config) ? 1 : 0);
    }
    else if (command == 'b')
    {
      Serial.printf("SERIAL_BAUD %u\n", CdcSerial.setBaudRate(115200) ? 1 : 0);
    }
  }

  if (CdcSerial.available() > 0)
  {
    Serial.print("SERIAL_RX ");
    while (CdcSerial.available() > 0)
    {
      Serial.write(CdcSerial.read());
    }
    Serial.println();
  }
  delay(1);
}
