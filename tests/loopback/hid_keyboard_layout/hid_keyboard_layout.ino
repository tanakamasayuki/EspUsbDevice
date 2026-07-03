#include "EspUsbDevice.h"
#include "EspUsbHost.h"
#include <string.h>

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbHost usb;

static volatile bool deviceConnected = false;
static volatile char lastKey = 0;
static volatile bool keyReceived = false;

static bool waitFor(volatile bool &flag, uint32_t timeoutMs)
{
  const uint32_t start = millis();
  while (!flag && millis() - start < timeoutMs)
  {
    delay(10);
  }
  return flag;
}

static void setLayout(EspUsbDeviceKeyboardLayout deviceLayout, EspUsbHostKeyboardLayout hostLayout, const char *name)
{
  keyboard.setLayout(deviceLayout);
  usb.setKeyboardLayout(hostLayout);
  Serial.printf("LAYOUT %s\n", name);
  delay(500);
}

static bool sendKey(char c)
{
  for (int attempt = 0; attempt < 3; attempt++)
  {
    keyReceived = false;
    lastKey = 0;
    if (!keyboard.tapKey(c))
    {
      delay(50);
      continue;
    }
    const uint32_t start = millis();
    while (millis() - start < 1000)
    {
      if (keyReceived && lastKey == c)
      {
        Serial.printf("KEY %c\n", c);
        return true;
      }
      delay(10);
    }
  }
  Serial.printf("KEY_FAIL %c got=0x%02x\n", c, static_cast<unsigned>(lastKey));
  return false;
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("TEST_BEGIN loopback_hid_keyboard_layout");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &deviceInfo)
                        {
                          Serial.printf("HOST_DEVICE vid=0x%04x pid=0x%04x\n", deviceInfo.vid, deviceInfo.pid);
                          deviceConnected = true;
                        });

  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event)
                 {
                   if (event.pressed && event.ascii)
                   {
                     lastKey = static_cast<char>(event.ascii);
                     keyReceived = true;
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

  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);

  EspUsbDeviceConfig deviceConfig;
  deviceConfig.port = ESP_USB_DEVICE_PORT_FULL_SPEED;
  deviceConfig.speed = ESP_USB_DEVICE_SPEED_FULL;
  deviceConfig.vid = 0x303a;
  deviceConfig.pid = 0x4009;
  deviceConfig.manufacturer = "EspUsb";
  deviceConfig.product = "EspUsbDevice Loopback Keyboard Layout";
  deviceConfig.serialNumber = "espusb-loopback-kbd-layout";

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

  setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US, ESP_USB_HOST_KEYBOARD_LAYOUT_EN_US, "EN_US");
  const char enKeys[] = {'@', '^'};
  for (size_t i = 0; i < sizeof(enKeys); i++)
  {
    ok = ok && sendKey(enKeys[i]);
  }

  setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP, ESP_USB_HOST_KEYBOARD_LAYOUT_JA_JP, "JA_JP");
  const char jaKeys[] = {'@', '^', ':', '_'};
  for (size_t i = 0; i < sizeof(jaKeys); i++)
  {
    ok = ok && sendKey(jaKeys[i]);
  }

  Serial.println(ok ? "TEST_END ok" : "TEST_END fail");
  Serial.println(ok ? "OK" : "NG");
}

void loop()
{
  delay(1);
}
