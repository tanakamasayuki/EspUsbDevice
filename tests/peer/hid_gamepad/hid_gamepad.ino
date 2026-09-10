#include "EspUsbHost.h"

EspUsbHost usb;

// Latched from the connect event so a test can ask whether the peer is attached
// instead of having to be the one that saw it arrive.
static volatile uint8_t deviceAddress = 0;
static uint16_t deviceVid = 0;
static uint16_t devicePid = 0;

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          deviceAddress = device.address;
                          deviceVid = device.vid;
                          devicePid = device.pid;
                          Serial.printf("HOST_CONNECTED vid=%04x pid=%04x\n", device.vid, device.pid);
                        });

  usb.onGamepad([](const EspUsbHostGamepadEvent &event)
                {
                  Serial.printf("GAMEPAD report=");
                  for (size_t i = 0; i < event.reportLength; i++)
                  {
                    Serial.printf("%02x", event.reportData[i]);
                    if (i + 1 < event.reportLength)
                    {
                      Serial.print(" ");
                    }
                  }
                  Serial.printf(" fields=%u", static_cast<unsigned>(event.fieldCount));
                  Serial.println();
                });

  if (!usb.begin())
  {
    Serial.printf("HOST_BEGIN_FAILED %s\n", usb.lastErrorName());
  }
}

// Block until the peer has been enumerated, so every command below answers about
// a device that is actually attached, whatever order the tests run in.
//
// deviceAddress is latched in onDeviceConnected, which fires after the host has
// claimed the interfaces - the right side of the event for anything that reads
// the device's interfaces or endpoints. Waiting here rather than announcing once
// at boot is what lets a test run in any position: a boot announcement is only
// visible to whichever test happens to be first.
static bool waitForDevice(uint32_t timeoutMs = 5000)
{
  const uint32_t startedAt = millis();
  while (deviceAddress == 0 && millis() - startedAt < timeoutMs)
  {
    delay(10);
  }
  return deviceAddress != 0;
}

void loop()
{
  while (Serial.available() > 0)
  {
    const char command = static_cast<char>(Serial.read());
    const bool attached = waitForDevice();
    if (command == '?')
    {
      // Everything else this sketch reports arrives on its own from a callback.
      // This is the one thing a test has to be able to ask for: that the peer is
      // enumerated, and that it is our peer rather than a neighbouring board.
      Serial.printf("HOST_READY %u vid=%04x pid=%04x\n", attached ? 1 : 0, deviceVid, devicePid);
    }
  }
  delay(1);
}
