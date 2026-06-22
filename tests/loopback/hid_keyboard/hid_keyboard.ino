#include "EspUsbDevice.h"
#include "EspUsbHost.h"
#include <string.h>

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbHost usb;

static volatile bool deviceConnected = false;
static volatile bool textReceived = false;
static volatile bool ledReceived = false;
static char textBuffer[32] = {};
static size_t textLength = 0;
static uint8_t lastLeds = 0;

static bool waitFor(volatile bool &flag, uint32_t timeoutMs)
{
  const uint32_t start = millis();
  while (!flag && millis() - start < timeoutMs)
  {
    delay(10);
  }
  return flag;
}

static bool sendText(const char *text)
{
  const uint32_t start = millis();
  while (millis() - start < 3000)
  {
    if (keyboard.write(text))
    {
      return true;
    }
    delay(5);
  }
  return false;
}

static bool waitLed(uint8_t expected)
{
  ledReceived = false;
  const uint32_t start = millis();
  while (millis() - start < 3000)
  {
    if (ledReceived && lastLeds == expected)
    {
      return true;
    }
    delay(10);
  }
  return false;
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("TEST_BEGIN loopback_hid_keyboard");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &deviceInfo)
                        {
                          Serial.printf("HOST_DEVICE vid=0x%04x pid=0x%04x\n", deviceInfo.vid, deviceInfo.pid);
                          deviceConnected = true;
                        });

  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event)
                 {
                   if (event.pressed && event.ascii)
                   {
                     if (textLength + 1 < sizeof(textBuffer))
                     {
                       textBuffer[textLength++] = static_cast<char>(event.ascii);
                       textBuffer[textLength] = '\0';
                     }
                     if (strcmp(textBuffer, "hello, keyboard") == 0)
                     {
                       Serial.println(textBuffer);
                       textReceived = true;
                     }
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
  keyboard.onOutputReport([](const EspUsbDeviceHidKeyboardOutputReport &report)
                          {
                            lastLeds = report.leds;
                            ledReceived = true;
                            Serial.printf("LED numlock=%u capslock=%u scrolllock=%u\n",
                                          report.numLock ? 1 : 0,
                                          report.capsLock ? 1 : 0,
                                          report.scrollLock ? 1 : 0);
                          });

  EspUsbDeviceConfig deviceConfig;
  deviceConfig.port = ESP_USB_DEVICE_PORT_FULL_SPEED;
  deviceConfig.speed = ESP_USB_DEVICE_SPEED_FULL;
  deviceConfig.vid = 0x303a;
  deviceConfig.pid = 0x4010;
  deviceConfig.manufacturer = "EspUsb";
  deviceConfig.product = "EspUsbDevice Loopback Keyboard";
  deviceConfig.serialNumber = "espusb-loopback-keyboard";

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
  if (!sendText("hello, keyboard"))
  {
    Serial.printf("SEND_FAILED %s\n", device.lastErrorName());
  }
  if (!waitFor(textReceived, 5000))
  {
    Serial.println("TEXT_TIMEOUT");
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }

  bool ok = true;
  Serial.printf("LED_TX %u\n", usb.setKeyboardLeds(true, false, false) ? 1 : 0);
  ok = ok && waitLed(ESP_USB_DEVICE_KEYBOARD_LED_NUM_LOCK);
  Serial.printf("LED_TX %u\n", usb.setKeyboardLeds(false, true, false) ? 1 : 0);
  ok = ok && waitLed(ESP_USB_DEVICE_KEYBOARD_LED_CAPS_LOCK);
  Serial.printf("LED_TX %u\n", usb.setKeyboardLeds(false, false, true) ? 1 : 0);
  ok = ok && waitLed(ESP_USB_DEVICE_KEYBOARD_LED_SCROLL_LOCK);
  Serial.printf("LED_TX %u\n", usb.setKeyboardLeds(false, false, false) ? 1 : 0);
  ok = ok && waitLed(0);

  if (!ok)
  {
    Serial.println("LED_TIMEOUT");
  }

  Serial.println(ok ? "TEST_END ok" : "TEST_END fail");
  Serial.println(ok ? "OK" : "NG");
}

void loop()
{
  delay(1);
}
