#include "EspUsbDevice.h"
#include "EspUsbHost.h"

EspUsbDevice device;
EspUsbDeviceHidGamepad gamepad(device);
EspUsbHost usb;

static volatile bool deviceConnected = false;
static volatile uint8_t gamepadStep = 0;

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

static bool waitStep(uint8_t expected, uint32_t timeoutMs = 3000)
{
  const uint32_t start = millis();
  while (gamepadStep < expected && millis() - start < timeoutMs)
  {
    delay(10);
  }
  return gamepadStep >= expected;
}

static bool sendGamepad(int8_t x,
                        int8_t y,
                        int8_t z,
                        int8_t rz,
                        int8_t rx,
                        int8_t ry,
                        uint8_t hat,
                        uint32_t buttons)
{
  const uint32_t start = millis();
  while (millis() - start < 3000)
  {
    if (gamepad.send(x, y, z, rz, rx, ry, hat, buttons))
    {
      return true;
    }
    delay(5);
  }
  Serial.printf("GAMEPAD_SEND_FAILED %s\n", device.lastErrorName());
  return false;
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("TEST_BEGIN loopback_hid_gamepad");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &deviceInfo)
                        {
                          Serial.printf("HOST_DEVICE vid=0x%04x pid=0x%04x\n", deviceInfo.vid, deviceInfo.pid);
                          deviceConnected = true;
                        });

  usb.onGamepad([](const EspUsbHostGamepadEvent &event)
                {
                  Serial.printf("GAMEPAD report=");
                  printBytes(event.reportData, event.reportLength, 16);
                  Serial.printf(" fields=%u changed=%u\n",
                                static_cast<unsigned>(event.fieldCount),
                                event.changed ? 1 : 0);
                  gamepadStep++;
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
                   printBytes(input.data, input.length, 12);
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

  EspUsbDeviceConfig deviceConfig;
  deviceConfig.port = ESP_USB_DEVICE_PORT_FULL_SPEED;
  deviceConfig.speed = ESP_USB_DEVICE_SPEED_FULL;
  deviceConfig.vid = 0x303a;
  deviceConfig.pid = 0x4013;
  deviceConfig.manufacturer = "EspUsb";
  deviceConfig.product = "EspUsbDevice Loopback Gamepad";
  deviceConfig.serialNumber = "espusb-loopback-gamepad";

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
  ok = ok && sendGamepad(10, -10, 20, -20, 30, -30, ESP_USB_DEVICE_GAMEPAD_HAT_RIGHT, 0x00000005) && waitStep(1);
  ok = ok && sendGamepad(0, 0, 0, 0, 0, 0, ESP_USB_DEVICE_GAMEPAD_HAT_CENTER, 0) && waitStep(2);
  ok = ok && sendGamepad(0, 0, 0, 0, 0, 0, ESP_USB_DEVICE_GAMEPAD_HAT_UP, 0) && waitStep(3);
  ok = ok && sendGamepad(0, 0, 0, 0, 0, 0, ESP_USB_DEVICE_GAMEPAD_HAT_CENTER, 0x00007fff) && waitStep(4);

  Serial.println(ok ? "TEST_END ok" : "TEST_END fail");
  Serial.println(ok ? "OK" : "NG");
}

void loop()
{
  delay(1);
}
