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

static void printBytes(const uint8_t *data, size_t length, size_t maxLength)
{
  const size_t count = length < maxLength ? length : maxLength;
  for (size_t i = 0; i < count; i++)
  {
    Serial.printf("%s%02x", i == 0 ? "" : " ", data[i]);
  }
}

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

static bool sendKeyboardLeds(bool numLock, bool capsLock, bool scrollLock, uint8_t expected)
{
  ledReceived = false;
  const bool sent = usb.setKeyboardLeds(numLock, capsLock, scrollLock);
  Serial.printf("LED_TX %u\n", sent ? 1 : 0);
  return sent && waitLed(expected);
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

  usb.onHIDReportDescriptor([](const EspUsbHostHIDReportDescriptor &descriptor)
                            {
                              Serial.printf("HID_DESC iface=%u reported=%u len=%u first=%02x last=%02x\n",
                                            descriptor.interfaceNumber,
                                            descriptor.reportedLength,
                                            descriptor.length,
                                            descriptor.length > 0 ? descriptor.data[0] : 0,
                                            descriptor.length > 0 ? descriptor.data[descriptor.length - 1] : 0);
                            });

  usb.onHIDInput([](const EspUsbHostHIDInput &input)
                 {
                   Serial.printf("HID_INPUT iface=%u subclass=%u protocol=%u len=%u data=",
                                 input.interfaceNumber,
                                 input.subclass,
                                 input.protocol,
                                 static_cast<unsigned>(input.length));
                   printBytes(input.data, input.length, 9);
                   Serial.println();
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
  ok = ok && sendKeyboardLeds(true, false, false, ESP_USB_DEVICE_KEYBOARD_LED_NUM_LOCK);
  ok = ok && sendKeyboardLeds(false, true, false, ESP_USB_DEVICE_KEYBOARD_LED_CAPS_LOCK);
  ok = ok && sendKeyboardLeds(false, false, true, ESP_USB_DEVICE_KEYBOARD_LED_SCROLL_LOCK);
  ok = ok && sendKeyboardLeds(false, false, false, 0);

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
