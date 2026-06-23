#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceHidConsumerControl consumer(device);

static bool clickConsumer(uint16_t usage)
{
  const uint32_t start = millis();
  while (millis() - start < 3000)
  {
    if (consumer.click(usage))
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
  config.pid = 0x4006;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Consumer Control";
  config.serialNumber = "espusb-consumer-control";

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
    if (command == 'u')
    {
      ok = clickConsumer(ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_UP);
    }
    else if (command == 'd')
    {
      ok = clickConsumer(ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_DOWN);
    }
    else if (command == 'p')
    {
      ok = clickConsumer(ESP_USB_DEVICE_CONSUMER_CONTROL_PLAY_PAUSE);
    }
    else if (command == 'n')
    {
      ok = clickConsumer(ESP_USB_DEVICE_CONSUMER_CONTROL_NEXT_TRACK);
    }
    else if (command == 's')
    {
      ok = clickConsumer(ESP_USB_DEVICE_CONSUMER_CONTROL_PREVIOUS_TRACK);
    }
    else if (command == 'm')
    {
      ok = clickConsumer(ESP_USB_DEVICE_CONSUMER_CONTROL_MUTE);
    }

    Serial.printf("CMD %c %u\n", command, ok ? 1 : 0);
    if (!ok)
    {
      Serial.printf("SEND_FAILED %c\n", command);
    }
  }
  delay(1);
}
