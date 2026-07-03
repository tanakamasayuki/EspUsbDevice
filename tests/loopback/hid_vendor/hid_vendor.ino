#include "EspUsbDevice.h"
#include "EspUsbHost.h"
#include <string.h>

EspUsbDevice device;
EspUsbDeviceHidVendor vendor(device);
EspUsbHost usb;

static const uint8_t OUTPUT_REPORT[63] = {
    'h', 'o', 's', 't', ' ', 'o', 'u', 't', 'p', 'u', 't'};
static const uint8_t FEATURE_REPORT[63] = {
    'h', 'o', 's', 't', ' ', 'f', 'e', 'a', 't', 'u', 'r', 'e'};

static volatile bool deviceConnected = false;
static volatile bool vendorInputSeen = false;
static volatile bool outputSeen = false;
static volatile bool featureSeen = false;

static bool waitFor(volatile bool &flag, uint32_t timeoutMs)
{
  const uint32_t start = millis();
  while (!flag && millis() - start < timeoutMs)
  {
    delay(10);
  }
  return flag;
}

static void printReportText(const char *prefix, const EspUsbDeviceHidReport &report)
{
  Serial.print(prefix);
  Serial.printf(" id=%u len=%u ", report.reportId, report.length);
  for (uint16_t i = 0; i < report.length && report.data && report.data[i] != 0; i++)
  {
    Serial.write(report.data[i]);
  }
  Serial.println();
}

static bool sendVendorInput()
{
  const uint8_t report[63] = {
      'h', 'e', 'l', 'l', 'o', ' ', 'v', 'e', 'n', 'd', 'o', 'r'};
  const uint32_t start = millis();
  while (millis() - start < 3000)
  {
    if (vendor.sendInput(report, sizeof(report)))
    {
      Serial.println("VENDOR_INPUT_TX 1");
      return true;
    }
    delay(5);
  }
  Serial.printf("VENDOR_INPUT_TX 0 error=%s\n", device.lastErrorName());
  return false;
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("TEST_BEGIN loopback_hid_vendor");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &deviceInfo)
                        {
                          Serial.printf("HOST_DEVICE vid=0x%04x pid=0x%04x\n", deviceInfo.vid, deviceInfo.pid);
                          deviceConnected = true;
                        });

  usb.onHIDVendorInput([](const EspUsbHostHIDVendorInput &input)
                       {
                         Serial.print("VENDOR ");
                         for (size_t i = 0; i < input.reportLength && input.reportData[i] != 0; i++)
                         {
                           Serial.write(input.reportData[i]);
                         }
                         Serial.println();
                         vendorInputSeen = true;
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

  vendor.onOutputReport([](const EspUsbDeviceHidReport &report)
                        {
                          printReportText("DEVICE_OUTPUT", report);
                          outputSeen = report.length == 63;
                        });
  vendor.onFeatureReport([](const EspUsbDeviceHidReport &report)
                         {
                           printReportText("DEVICE_FEATURE", report);
                           featureSeen = report.reportId == ESP_USB_DEVICE_HID_REPORT_ID_VENDOR && report.length == 63;
                         });

  EspUsbDeviceConfig deviceConfig;
  deviceConfig.vid = 0x303a;
  deviceConfig.pid = 0x4005;
  deviceConfig.manufacturer = "EspUsb";
  deviceConfig.product = "EspUsbDevice Loopback Vendor HID";
  deviceConfig.serialNumber = "espusb-loopback-vendor-hid";

  if (!device.begin(deviceConfig))
  {
    Serial.printf("DEVICE_BEGIN_FAILED %s\n", device.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }
  Serial.println("DEVICE_READY fs");

  bool ok = waitFor(deviceConnected, 30000);

  ok = ok && sendVendorInput();
  ok = ok && waitFor(vendorInputSeen, 5000);

  const bool featureSent = usb.sendHIDVendorFeature(FEATURE_REPORT, sizeof(FEATURE_REPORT));
  Serial.printf("SEND_FEATURE %u\n", featureSent ? 1 : 0);
  ok = ok && featureSent && waitFor(featureSeen, 5000);

  const bool outputSent = usb.sendHIDVendorOutput(OUTPUT_REPORT, sizeof(OUTPUT_REPORT));
  Serial.printf("SEND_OUTPUT %u\n", outputSent ? 1 : 0);
  ok = ok && outputSent && waitFor(outputSeen, 5000);

  Serial.println(ok ? "TEST_END ok" : "TEST_END fail");
  Serial.println(ok ? "OK" : "NG");
}

void loop()
{
  delay(1);
}
