#include "EspUsbDevice.h"

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

static bool sendCustomReport()
{
  const uint8_t report[8] = {0x01, 0x7f, 0x81, 0x22, 0x33, 0x44, 0x55, 0x66};
  const uint32_t start = millis();
  while (millis() - start < 3000)
  {
    if (customHid.sendReport(report, sizeof(report)))
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
  config.port = ESP_USB_DEVICE_PORT_FULL_SPEED;
  config.speed = ESP_USB_DEVICE_SPEED_FULL;
  config.vid = 0x303a;
  config.pid = 0x4004;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Custom HID";
  config.serialNumber = "espusb-custom-hid";

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
    const bool ok = command == 'a' && sendCustomReport();
    Serial.printf("CMD %c %u\n", command, ok ? 1 : 0);
    if (!ok)
    {
      Serial.printf("SEND_FAILED %c\n", command);
    }
  }
  delay(1);
}
