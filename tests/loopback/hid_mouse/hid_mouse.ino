#include "EspUsbDevice.h"
#include "EspUsbHost.h"

EspUsbDevice device;
EspUsbDeviceHidMouse mouse(device);
EspUsbHost usb;

static volatile bool deviceConnected = false;
static volatile uint8_t eventStep = 0;

static bool waitFor(volatile bool &flag, uint32_t timeoutMs)
{
  const uint32_t start = millis();
  while (!flag && millis() - start < timeoutMs)
  {
    delay(10);
  }
  return flag;
}

static bool waitStep(uint8_t expected, uint32_t timeoutMs = 3000)
{
  const uint32_t start = millis();
  while (eventStep < expected && millis() - start < timeoutMs)
  {
    delay(10);
  }
  return eventStep >= expected;
}

static bool sendClick(uint8_t button)
{
  const uint32_t start = millis();
  while (millis() - start < 3000)
  {
    if (mouse.click(button, 50))
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
  delay(1000);

  Serial.println("TEST_BEGIN loopback_hid_mouse");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &deviceInfo)
                        {
                          Serial.printf("HOST_DEVICE vid=0x%04x pid=0x%04x\n", deviceInfo.vid, deviceInfo.pid);
                          deviceConnected = true;
                        });

  usb.onMouse([](const EspUsbHostMouseEvent &event)
              {
                if (event.moved || event.buttonsChanged)
                {
                  Serial.printf("MOUSE x=%d y=%d wheel=%d buttons=%u previous=%u moved=%u changed=%u\n",
                                event.x,
                                event.y,
                                event.wheel,
                                event.buttons,
                                event.previousButtons,
                                event.moved ? 1 : 0,
                                event.buttonsChanged ? 1 : 0);
                  eventStep++;
                }
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
  deviceConfig.pid = 0x4011;
  deviceConfig.manufacturer = "EspUsb";
  deviceConfig.product = "EspUsbDevice Loopback Mouse";
  deviceConfig.serialNumber = "espusb-loopback-mouse";

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
  ok = ok && mouse.move(40, 0) && waitStep(1);
  ok = ok && mouse.move(-40, 0) && waitStep(2);
  ok = ok && mouse.move(0, 40) && waitStep(3);
  ok = ok && mouse.move(0, -40) && waitStep(4);
  ok = ok && mouse.wheel(1) && waitStep(5);
  ok = ok && sendClick(ESP_USB_DEVICE_MOUSE_MIDDLE) && waitStep(7);
  ok = ok && sendClick(ESP_USB_DEVICE_MOUSE_LEFT) && waitStep(9);
  ok = ok && sendClick(ESP_USB_DEVICE_MOUSE_RIGHT) && waitStep(11);
  ok = ok && sendClick(ESP_USB_DEVICE_MOUSE_BACK) && waitStep(13);
  ok = ok && sendClick(ESP_USB_DEVICE_MOUSE_FORWARD) && waitStep(15);

  Serial.println(ok ? "TEST_END ok" : "TEST_END fail");
  Serial.println(ok ? "OK" : "NG");
}

void loop()
{
  delay(1);
}
