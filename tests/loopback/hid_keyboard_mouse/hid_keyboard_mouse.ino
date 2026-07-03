#include "EspUsbDevice.h"
#include "EspUsbHost.h"

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceHidMouse mouse(device);
EspUsbHost usb;

static volatile bool deviceConnected = false;
static volatile bool keyReceived = false;
static volatile uint8_t mouseStep = 0;

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

static bool waitMouseStep(uint8_t expected, uint32_t timeoutMs = 3000)
{
  const uint32_t start = millis();
  while (mouseStep < expected && millis() - start < timeoutMs)
  {
    delay(10);
  }
  return mouseStep >= expected;
}

static bool sendMouseMove(int8_t x, int8_t y, int8_t wheel = 0, uint8_t buttons = 0)
{
  const uint32_t start = millis();
  while (millis() - start < 3000)
  {
    if (mouse.move(x, y, wheel, buttons))
    {
      return true;
    }
    delay(5);
  }
  Serial.printf("MOUSE_MOVE_FAILED %s\n", device.lastErrorName());
  return false;
}

static bool sendMouseClick(uint8_t button)
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
  Serial.printf("MOUSE_CLICK_FAILED %s\n", device.lastErrorName());
  return false;
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("TEST_BEGIN loopback_hid_keyboard_mouse");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &deviceInfo)
                        {
                          Serial.printf("HOST_DEVICE vid=0x%04x pid=0x%04x\n", deviceInfo.vid, deviceInfo.pid);
                          deviceConnected = true;
                        });

  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event)
                 {
                   if (event.pressed && event.ascii)
                   {
                     Serial.printf("KEY %c\n", static_cast<char>(event.ascii));
                     if (event.ascii == 'k')
                     {
                       keyReceived = true;
                     }
                   }
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
                  mouseStep++;
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
                   printBytes(input.data, input.length, 10);
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

  EspUsbDeviceConfig deviceConfig;
  deviceConfig.vid = 0x303a;
  deviceConfig.pid = 0x4012;
  deviceConfig.manufacturer = "EspUsb";
  deviceConfig.product = "EspUsbDevice Loopback Keyboard Mouse";
  deviceConfig.serialNumber = "espusb-loopback-keyboard-mouse";

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

  bool ok = keyboard.tapKey('k') && waitFor(keyReceived, 3000);
  delay(500);
  ok = ok && sendMouseMove(40, 0) && waitMouseStep(1);
  delay(100);
  ok = ok && sendMouseClick(ESP_USB_DEVICE_MOUSE_LEFT) && waitMouseStep(3);

  Serial.println(ok ? "TEST_END ok" : "TEST_END fail");
  Serial.println(ok ? "OK" : "NG");
}

void loop()
{
  delay(1);
}
