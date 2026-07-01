#include "EspUsbDevice.h"
#include "EspUsbHost.h"

EspUsbDevice device;
EspUsbDeviceHidConsumerControl consumer(device);
EspUsbHost usb;

static volatile bool deviceConnected = false;
static volatile uint8_t eventCount = 0;

static bool waitFor(volatile bool &flag, uint32_t timeoutMs)
{
  const uint32_t start = millis();
  while (!flag && millis() - start < timeoutMs)
  {
    delay(10);
  }
  return flag;
}

static bool waitEvents(uint8_t expected, uint32_t timeoutMs = 3000)
{
  const uint32_t start = millis();
  while (eventCount < expected && millis() - start < timeoutMs)
  {
    delay(10);
  }
  return eventCount >= expected;
}

static bool clickConsumer(const char *name, uint16_t usage)
{
  const uint32_t start = millis();
  bool ok = false;
  while (millis() - start < 3000)
  {
    if (consumer.click(usage))
    {
      ok = true;
      break;
    }
    delay(5);
  }
  Serial.printf("SEND %s %u\n", name, ok ? 1 : 0);
  if (!ok)
  {
    Serial.printf("SEND_FAILED %s %s\n", name, device.lastErrorName());
  }
  return ok;
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("TEST_BEGIN loopback_hid_consumer_control");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &deviceInfo)
                        {
                          Serial.printf("HOST_DEVICE vid=0x%04x pid=0x%04x\n", deviceInfo.vid, deviceInfo.pid);
                          deviceConnected = true;
                        });

  usb.onConsumerControl([](const EspUsbHostConsumerControlEvent &event)
                        {
                          Serial.printf("CONSUMER usage=0x%04x pressed=%u released=%u\n",
                                        event.usage,
                                        event.pressed ? 1 : 0,
                                        event.released ? 1 : 0);
                          eventCount++;
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
  deviceConfig.pid = 0x4006;
  deviceConfig.manufacturer = "EspUsb";
  deviceConfig.product = "EspUsbDevice Loopback Consumer Control";
  deviceConfig.serialNumber = "espusb-loopback-consumer-control";

  if (!device.begin(deviceConfig))
  {
    Serial.printf("DEVICE_BEGIN_FAILED %s\n", device.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }
  Serial.println("DEVICE_READY fs");

  bool ok = waitFor(deviceConnected, 30000);

  ok = ok && clickConsumer("volume_up", ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_UP) && waitEvents(2);
  ok = ok && clickConsumer("volume_down", ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_DOWN) && waitEvents(4);
  ok = ok && clickConsumer("play_pause", ESP_USB_DEVICE_CONSUMER_CONTROL_PLAY_PAUSE) && waitEvents(6);
  ok = ok && clickConsumer("next_track", ESP_USB_DEVICE_CONSUMER_CONTROL_NEXT_TRACK) && waitEvents(8);
  ok = ok && clickConsumer("previous_track", ESP_USB_DEVICE_CONSUMER_CONTROL_PREVIOUS_TRACK) && waitEvents(10);
  ok = ok && clickConsumer("mute", ESP_USB_DEVICE_CONSUMER_CONTROL_MUTE) && waitEvents(12);

  Serial.println(ok ? "TEST_END ok" : "TEST_END fail");
  Serial.println(ok ? "OK" : "NG");
}

void loop()
{
  delay(1);
}
