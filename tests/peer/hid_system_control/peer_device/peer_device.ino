#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceHidSystemControl systemControl(device);

static bool clickSystem(uint8_t usage)
{
  const uint32_t start = millis();
  while (millis() - start < 3000)
  {
    if (systemControl.click(usage))
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
  config.pid = 0x4007;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice System Control";
  config.serialNumber = "espusb-system-control";

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

    bool ok = false;
    if (command == 'p')
    {
      ok = clickSystem(ESP_USB_DEVICE_SYSTEM_CONTROL_POWER_OFF);
    }
    else if (command == 's')
    {
      ok = clickSystem(ESP_USB_DEVICE_SYSTEM_CONTROL_STANDBY);
    }

    Serial.printf("CMD %c %u\n", command, ok ? 1 : 0);
    if (!ok)
    {
      Serial.printf("SEND_FAILED %c\n", command);
    }
  }
  delay(1);
}
