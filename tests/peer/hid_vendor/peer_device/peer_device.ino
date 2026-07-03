#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceHidVendor vendor(device);

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

  vendor.onOutputReport([](const EspUsbDeviceHidReport &report)
                        {
                          printReportText("DEVICE_OUTPUT", report);
                        });
  vendor.onFeatureReport([](const EspUsbDeviceHidReport &report)
                         {
                           printReportText("DEVICE_FEATURE", report);
                         });

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4005;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Vendor HID";
  config.serialNumber = "espusb-vendor-hid";

  Serial.printf("DEVICE_BEGIN %u\n", device.begin(config) ? 1 : 0);
}

void loop()
{
  while (Serial.available() > 0)
  {
    const char command = static_cast<char>(Serial.read());
    if (command == '\r' || command == '\n')
    {
      continue;
    }
    const bool ok = command == 'h' && sendVendorInput();
    Serial.printf("CMD %c %u\n", command, ok ? 1 : 0);
    if (!ok)
    {
      Serial.printf("SEND_FAILED %c\n", command);
    }
  }
  delay(1);
}
