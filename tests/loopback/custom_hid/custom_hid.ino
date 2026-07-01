#include "EspUsbDevice.h"
#include "EspUsbHost.h"

static const uint8_t REPORT_DESCRIPTOR[] = {
    0x05, 0x01,
    0x09, 0x04,
    0xa1, 0x01,
    0xa1, 0x00,
    0x05, 0x01,
    0x09, 0x30,
    0x09, 0x31,
    0x09, 0x32,
    0x09, 0x33,
    0x09, 0x34,
    0x09, 0x35,
    0x09, 0x36,
    0x09, 0x36,
    0x15, 0x81,
    0x25, 0x7f,
    0x75, 0x08,
    0x95, 0x08,
    0x81, 0x02,
    0xc0,
    0xc0,
};

EspUsbDevice device;
EspUsbDeviceHidCustom customHid(device, REPORT_DESCRIPTOR, sizeof(REPORT_DESCRIPTOR), 8);
EspUsbHost usb;

static volatile bool deviceConnected = false;
static volatile bool descriptorSeen = false;
static volatile bool inputSeen = false;

static void printBytes(const uint8_t *data, size_t length)
{
  for (size_t i = 0; i < length; i++)
  {
    Serial.printf("%02x", data[i]);
  }
}

static bool waitFor(volatile bool &flag, uint32_t timeoutMs)
{
  const uint32_t start = millis();
  while (!flag && millis() - start < timeoutMs)
  {
    delay(10);
  }
  return flag;
}

static bool sendCustomReport()
{
  const uint8_t report[8] = {0x01, 0x7f, 0x81, 0x22, 0x33, 0x44, 0x55, 0x66};
  const uint32_t start = millis();
  while (millis() - start < 3000)
  {
    if (customHid.sendReport(report, sizeof(report)))
    {
      Serial.println("CUSTOM_TX 1");
      return true;
    }
    delay(5);
  }
  Serial.printf("CUSTOM_TX 0 error=%s\n", device.lastErrorName());
  return false;
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("TEST_BEGIN loopback_custom_hid");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &deviceInfo)
                        {
                          Serial.printf("HOST_DEVICE vid=0x%04x pid=0x%04x\n", deviceInfo.vid, deviceInfo.pid);
                          deviceConnected = true;
                        });

  usb.onHIDReportDescriptor([](const EspUsbHostHIDReportDescriptor &descriptor)
                            {
                              Serial.printf("HID_REPORT iface=%u reported=%u len=%u first=%02x last=%02x\n",
                                            descriptor.interfaceNumber,
                                            descriptor.reportedLength,
                                            descriptor.length,
                                            descriptor.length > 0 ? descriptor.data[0] : 0,
                                            descriptor.length > 0 ? descriptor.data[descriptor.length - 1] : 0);
                              descriptorSeen = descriptor.reportedLength == 38 && descriptor.length == 38;
                            });

  usb.onHIDInput([](const EspUsbHostHIDInput &input)
                 {
                   Serial.printf("CUSTOM len=%u data=", static_cast<unsigned>(input.length));
                   printBytes(input.data, input.length);
                   Serial.println();
                   inputSeen = input.length == 8;
                 });

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

  EspUsbDeviceConfig deviceConfig;
  deviceConfig.port = ESP_USB_DEVICE_PORT_FULL_SPEED;
  deviceConfig.speed = ESP_USB_DEVICE_SPEED_FULL;
  deviceConfig.vid = 0x303a;
  deviceConfig.pid = 0x4004;
  deviceConfig.manufacturer = "EspUsb";
  deviceConfig.product = "EspUsbDevice Loopback Custom HID";
  deviceConfig.serialNumber = "espusb-loopback-custom-hid";

  if (!device.begin(deviceConfig))
  {
    Serial.printf("DEVICE_BEGIN_FAILED %s\n", device.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }
  Serial.println("DEVICE_READY fs");

  bool ok = waitFor(deviceConnected, 30000);
  ok = ok && waitFor(descriptorSeen, 5000);
  ok = ok && sendCustomReport();
  ok = ok && waitFor(inputSeen, 5000);

  Serial.println(ok ? "TEST_END ok" : "TEST_END fail");
  Serial.println(ok ? "OK" : "NG");
}

void loop()
{
  delay(1);
}
