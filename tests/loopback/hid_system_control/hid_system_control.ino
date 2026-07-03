#include "EspUsbDevice.h"
#include "EspUsbHost.h"

EspUsbDevice device;
EspUsbDeviceHidSystemControl systemControl(device);
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

static bool clickSystem(const char *name, uint8_t usage)
{
  const uint32_t start = millis();
  bool ok = false;
  while (millis() - start < 3000)
  {
    if (systemControl.click(usage))
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

  Serial.println("TEST_BEGIN loopback_hid_system_control");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &deviceInfo)
                        {
                          Serial.printf("HOST_DEVICE vid=0x%04x pid=0x%04x\n", deviceInfo.vid, deviceInfo.pid);
                          deviceConnected = true;
                        });

  usb.onSystemControl([](const EspUsbHostSystemControlEvent &event)
                      {
                        Serial.printf("SYSTEM usage=0x%02x pressed=%u released=%u\n",
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
  deviceConfig.vid = 0x303a;
  deviceConfig.pid = 0x4007;
  deviceConfig.manufacturer = "EspUsb";
  deviceConfig.product = "EspUsbDevice Loopback System Control";
  deviceConfig.serialNumber = "espusb-loopback-system-control";

  if (!device.begin(deviceConfig))
  {
    Serial.printf("DEVICE_BEGIN_FAILED %s\n", device.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }
  Serial.println("DEVICE_READY fs");

  bool ok = waitFor(deviceConnected, 30000);

  ok = ok && clickSystem("power_off", ESP_USB_DEVICE_SYSTEM_CONTROL_POWER_OFF) && waitEvents(2);
  ok = ok && clickSystem("standby", ESP_USB_DEVICE_SYSTEM_CONTROL_STANDBY) && waitEvents(4);
  ok = ok && clickSystem("wake_host", ESP_USB_DEVICE_SYSTEM_CONTROL_WAKE_HOST) && waitEvents(6);

  Serial.println(ok ? "TEST_END ok" : "TEST_END fail");
  Serial.println(ok ? "OK" : "NG");
}

void loop()
{
  delay(1);
}
