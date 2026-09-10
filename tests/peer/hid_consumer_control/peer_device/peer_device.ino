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
  config.vid = 0x303a;
  config.pid = 0x4006;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Consumer Control";
  config.serialNumber = "espusb-consumer-control";

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
  while (Serial.available() > 0)
  {
    const char command = static_cast<char>(Serial.read());
    const bool hostReady = waitForHost();
    if (command == '\r' || command == '\n')
    {
      continue;
    }

    if (command == '?')
    {
      Serial.printf("DEVICE_READY %u\n", hostReady ? 1 : 0);
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
