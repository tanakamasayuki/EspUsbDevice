#include "EspUsbDevice.h"
#include "EspUsbHost.h"
#include <string.h>

EspUsbDevice device;
EspUsbDeviceCdcSerial DeviceSerial(device);
EspUsbHost usb;
EspUsbHostCdcSerial HostSerial(usb);

static volatile bool deviceConnected = false;
static volatile bool lineCodingSeen = false;
static volatile uint32_t lineCodingBaud = 0;
static volatile uint8_t lineCodingStopBits = 0;
static volatile uint8_t lineCodingParity = 0;
static volatile uint8_t lineCodingDataBits = 0;

static bool waitFor(volatile bool &flag, uint32_t timeoutMs)
{
  const uint32_t start = millis();
  while (!flag && millis() - start < timeoutMs)
  {
    delay(10);
  }
  return flag;
}

static bool waitHostRx(const char *expected, uint32_t timeoutMs = 3000)
{
  char buffer[64] = {};
  size_t length = 0;
  const uint32_t start = millis();
  while (millis() - start < timeoutMs)
  {
    while (HostSerial.available() > 0 && length + 1 < sizeof(buffer))
    {
      buffer[length++] = static_cast<char>(HostSerial.read());
      buffer[length] = '\0';
    }
    if (strcmp(buffer, expected) == 0)
    {
      Serial.printf("SERIAL_RX %s\n", buffer);
      return true;
    }
    delay(10);
  }
  Serial.printf("SERIAL_RX_TIMEOUT got=%s\n", buffer);
  return false;
}

static bool waitDeviceRx(const char *expected, uint32_t timeoutMs = 3000)
{
  char buffer[64] = {};
  size_t length = 0;
  const uint32_t start = millis();
  while (millis() - start < timeoutMs)
  {
    while (DeviceSerial.available() > 0 && length + 1 < sizeof(buffer))
    {
      buffer[length++] = static_cast<char>(DeviceSerial.read());
      buffer[length] = '\0';
    }
    if (strcmp(buffer, expected) == 0)
    {
      Serial.printf("DEVICE_RX %s\n", buffer);
      return true;
    }
    delay(10);
  }
  Serial.printf("DEVICE_RX_TIMEOUT got=%s\n", buffer);
  return false;
}

static bool waitLineCoding(uint32_t baud, uint8_t stopBits, uint8_t parity, uint8_t dataBits, uint32_t timeoutMs = 3000)
{
  const uint32_t start = millis();
  while (millis() - start < timeoutMs)
  {
    if (lineCodingSeen && lineCodingBaud == baud && lineCodingStopBits == stopBits &&
        lineCodingParity == parity && lineCodingDataBits == dataBits)
    {
      Serial.printf("DEVICE_LINE_CODING seen=1 baud=%lu stop=%u parity=%u data=%u\n",
                    static_cast<unsigned long>(lineCodingBaud),
                    lineCodingStopBits,
                    lineCodingParity,
                    lineCodingDataBits);
      return true;
    }
    delay(10);
  }
  Serial.printf("DEVICE_LINE_CODING_TIMEOUT seen=%u baud=%lu stop=%u parity=%u data=%u\n",
                lineCodingSeen ? 1 : 0,
                static_cast<unsigned long>(lineCodingBaud),
                lineCodingStopBits,
                lineCodingParity,
                lineCodingDataBits);
  return false;
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("TEST_BEGIN loopback_usb_serial");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &deviceInfo)
                        {
                          Serial.printf("HOST_DEVICE vid=0x%04x pid=0x%04x\n", deviceInfo.vid, deviceInfo.pid);
                          deviceConnected = true;
                        });

  HostSerial.begin(115200);

  EspUsbHostConfig hostConfig;
  hostConfig.port = ESP_USB_HOST_PORT_FULL_SPEED;
  if (!usb.begin(hostConfig))
  {
    Serial.printf("HOST_BEGIN_FAILED %s\n", usb.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }
  Serial.println("HOST_READY fs");

  DeviceSerial.onLineCoding([](const EspUsbDeviceCdcLineCoding &lineCoding)
                            {
                              lineCodingSeen = true;
                              lineCodingBaud = lineCoding.baud;
                              lineCodingStopBits = lineCoding.stopBits;
                              lineCodingParity = lineCoding.parity;
                              lineCodingDataBits = lineCoding.dataBits;
                            });

  EspUsbDeviceConfig deviceConfig;
  deviceConfig.port = ESP_USB_DEVICE_PORT_FULL_SPEED;
  deviceConfig.speed = ESP_USB_DEVICE_SPEED_FULL;
  deviceConfig.vid = 0x303a;
  deviceConfig.pid = 0x4016;
  deviceConfig.manufacturer = "EspUsb";
  deviceConfig.product = "EspUsbDevice Loopback USB Serial";
  deviceConfig.serialNumber = "espusb-loopback-usb-serial";

  if (!device.begin(deviceConfig))
  {
    Serial.printf("DEVICE_BEGIN_FAILED %s\n", device.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }
  Serial.println("DEVICE_READY fs");

  if (!waitFor(deviceConnected, 30000))
  {
    Serial.printf("DEVICE_TIMEOUT host_error=%s device_error=%s\n", usb.lastErrorName(), device.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }

  delay(500);

  bool ok = true;
  const uint8_t devicePayload[] = "device to host";
  Serial.printf("DEVICE_TX %u\n", DeviceSerial.write(devicePayload, sizeof(devicePayload) - 1) == sizeof(devicePayload) - 1 ? 1 : 0);
  ok = ok && waitHostRx("device to host");

  const uint8_t hostPayload[] = "host to serial\n";
  Serial.printf("SERIAL_TX %u\n", HostSerial.write(hostPayload, sizeof(hostPayload) - 1) == sizeof(hostPayload) - 1 ? 1 : 0);
  ok = ok && waitDeviceRx("host to serial\n");

  EspUsbHostSerialConfig config;
  config.baud = 57600;
  config.dataBits = 7;
  config.parity = ESP_USB_HOST_SERIAL_PARITY_EVEN;
  config.stopBits = ESP_USB_HOST_SERIAL_STOP_BITS_2;
  Serial.printf("SERIAL_CONFIG %u\n", HostSerial.setConfig(config) ? 1 : 0);
  ok = ok && waitLineCoding(57600, 2, 2, 7);

  config.baud = 300;
  config.dataBits = 5;
  config.parity = ESP_USB_HOST_SERIAL_PARITY_MARK;
  config.stopBits = ESP_USB_HOST_SERIAL_STOP_BITS_1_5;
  Serial.printf("SERIAL_CONFIG_MARK %u\n", HostSerial.setConfig(config) ? 1 : 0);
  ok = ok && waitLineCoding(300, 1, 3, 5);

  Serial.printf("SERIAL_BAUD %u\n", HostSerial.setBaudRate(115200) ? 1 : 0);
  ok = ok && waitLineCoding(115200, 1, 3, 5);

  Serial.println(ok ? "TEST_END ok" : "TEST_END fail");
  Serial.println(ok ? "OK" : "NG");
}

void loop()
{
  delay(1);
}
