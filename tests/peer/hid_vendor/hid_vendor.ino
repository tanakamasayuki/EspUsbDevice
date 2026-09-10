#include "EspUsbHost.h"

EspUsbHost usb;

static const uint8_t OUTPUT_REPORT[63] = {
    'h', 'o', 's', 't', ' ', 'o', 'u', 't', 'p', 'u', 't'};
static const uint8_t FEATURE_REPORT[63] = {
    'h', 'o', 's', 't', ' ', 'f', 'e', 'a', 't', 'u', 'r', 'e'};

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

  usb.onHIDVendorInput([](const EspUsbHostHIDVendorInput &input)
                       {
                         Serial.print("VENDOR ");
                         for (size_t i = 0; i < input.reportLength && input.reportData[i] != 0; i++)
                         {
                           Serial.write(input.reportData[i]);
                         }
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
  if (Serial.available() > 0)
  {
    const char command = static_cast<char>(Serial.read());
    const bool attached = waitForDevice();
    if (command == '?')
    {
      Serial.printf("HOST_READY %u vid=%04x pid=%04x\n", attached ? 1 : 0, deviceVid, devicePid);
    }
    else if (command == 'o')
    {
      Serial.printf("SEND_OUTPUT %u\n", usb.sendHIDVendorOutput(OUTPUT_REPORT, sizeof(OUTPUT_REPORT)) ? 1 : 0);
    }
    else if (command == 'f')
    {
      Serial.printf("SEND_FEATURE %u\n", usb.sendHIDVendorFeature(FEATURE_REPORT, sizeof(FEATURE_REPORT)) ? 1 : 0);
    }
  }
  delay(1);
}
