#include "EspUsbDevice.h"

// Device side of the keyboard peer test. Every printable byte received on Serial
// is typed to the host; three control bytes are commands instead, so that the
// LED-state assertions do not collide with text input:
//   0x01 -> print keyboard.ledState() (the polled host LED state)
//   0x02 -> drop the onOutputReport() callback (simulates an integration layer
//           that owns the single slot, or a sketch that never installed one)
//   0x03 -> reinstall the callback, so suites stay order-independent

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

static void installOutputReportCallback()
{
  keyboard.onOutputReport([](const EspUsbDeviceHidKeyboardOutputReport &report)
                          {
                            Serial.printf("LED numlock=%u capslock=%u scrolllock=%u\n",
                                          report.numLock ? 1 : 0,
                                          report.capsLock ? 1 : 0,
                                          report.scrollLock ? 1 : 0);
                          });
}

static void printLedState()
{
  // Polled state, not the callback. Must track the host even with no callback.
  const EspUsbDeviceHidKeyboardOutputReport &state = keyboard.ledState();
  Serial.printf("LED_STATE numlock=%u capslock=%u scrolllock=%u raw=0x%02x\n",
                state.numLock ? 1 : 0,
                state.capsLock ? 1 : 0,
                state.scrollLock ? 1 : 0,
                state.leds);
}

static bool sendAscii(char c)
{
  const uint32_t start = millis();
  while (millis() - start < 1000)
  {
    if (keyboard.tapKey(c))
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

  installOutputReportCallback();
  keyboard.onProtocol([](const EspUsbDeviceHidProtocolEvent &event)
                      {
                        Serial.printf("PROTOCOL instance=%u protocol=%u\n", event.instance, event.protocol);
                      });

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4001;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Keyboard";
  config.serialNumber = "espusb-kbd";

  Serial.printf("DEVICE_BEGIN %u\n", device.begin(config) ? 1 : 0);
}

void loop()
{
  while (Serial.available() > 0)
  {
    char c = static_cast<char>(Serial.read());
    if (c == '\x01')
    {
      printLedState();
      continue;
    }
    if (c == '\x02')
    {
      keyboard.onOutputReport(nullptr);
      Serial.println("LED_CALLBACK_CLEARED");
      continue;
    }
    if (c == '\x03')
    {
      installOutputReportCallback();
      Serial.println("LED_CALLBACK_INSTALLED");
      continue;
    }
    if (!sendAscii(c))
    {
      Serial.printf("SEND_FAILED %d\n", static_cast<int>(c));
    }
  }
  delay(1);
}
