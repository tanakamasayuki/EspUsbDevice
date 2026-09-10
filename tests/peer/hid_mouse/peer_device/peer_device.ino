#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceHidMouse mouse(device);

static bool sendMove(int8_t x, int8_t y, int8_t wheel = 0, uint8_t buttons = 0)
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
  return false;
}

static bool sendClick(uint8_t button)
{
  if (!sendMove(0, 0, 0, button))
  {
    return false;
  }
  delay(50);
  return sendMove(0, 0, 0, 0);
}

void setup()
{
  Serial.begin(115200);
  delay(5000);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4002;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Mouse";
  config.serialNumber = "espusb-mouse";

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
    char command = static_cast<char>(Serial.read());
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

    bool ok = true;
    switch (command)
    {
    case 'r':
      ok = sendMove(40, 0, 0);
      break;
    case 'l':
      ok = sendMove(-40, 0, 0);
      break;
    case 'u':
      ok = sendMove(0, -40, 0);
      break;
    case 'd':
      ok = sendMove(0, 40, 0);
      break;
    case 'w':
      ok = sendMove(0, 0, 1);
      break;
    case 'm':
      ok = sendClick(ESP_USB_DEVICE_MOUSE_LEFT);
      break;
    case 'R':
      ok = sendClick(ESP_USB_DEVICE_MOUSE_RIGHT);
      break;
    case 'M':
      ok = sendClick(ESP_USB_DEVICE_MOUSE_MIDDLE);
      break;
    case 'b':
      ok = sendClick(ESP_USB_DEVICE_MOUSE_BACK);
      break;
    case 'f':
      ok = sendClick(ESP_USB_DEVICE_MOUSE_FORWARD);
      break;
    default:
      ok = false;
      break;
    }
    Serial.printf("CMD %c %u\n", command, ok ? 1 : 0);
    if (!ok)
    {
      Serial.printf("SEND_FAILED %c\n", command);
    }
  }
  delay(1);
}
