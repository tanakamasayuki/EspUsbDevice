#include "EspUsbHost.h"

EspUsbHost usb;

static void printBytes(const uint8_t *data, size_t length, size_t maxLength)
{
  const size_t count = length < maxLength ? length : maxLength;
  for (size_t i = 0; i < count; i++)
  {
    Serial.printf("%s%02x", i == 0 ? "" : " ", data[i]);
  }
}

// Latched from the connect event so a test can ask whether the peer is attached
// instead of having to be the one that saw it arrive.
static volatile uint8_t deviceAddress = 0;
static uint16_t deviceVid = 0;
static uint16_t devicePid = 0;
static char hidDescriptorLine[128] = "HID_DESC none";
static volatile bool hidDescriptorSeen = false;

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

  usb.onMouse([](const EspUsbHostMouseEvent &event)
              {
                Serial.printf("MOUSE x=%d y=%d wheel=%d buttons=%u previous=%u moved=%u changed=%u\n",
                              event.x,
                              event.y,
                              event.wheel,
                              event.buttons,
                              event.previousButtons,
                              event.moved ? 1 : 0,
                              event.buttonsChanged ? 1 : 0);
              });

  usb.onHIDReportDescriptor([](const EspUsbHostHIDReportDescriptor &descriptor)
                            {
                              // Kept as well as printed: the descriptor arrives
                              // once, at enumeration, and a test that runs later
                              // has to be able to ask for it.
                              snprintf(hidDescriptorLine, sizeof(hidDescriptorLine),
                                       "HID_DESC iface=%u reported=%u len=%u first=%02x last=%02x",
                                       descriptor.interfaceNumber,
                                       descriptor.reportedLength,
                                       descriptor.length,
                                       descriptor.length > 0 ? descriptor.data[0] : 0,
                                       descriptor.length > 0 ? descriptor.data[descriptor.length - 1] : 0);
                              hidDescriptorSeen = true;
                              Serial.println(hidDescriptorLine);
                            });

  usb.onHIDInput([](const EspUsbHostHIDInput &input)
                 {
                   Serial.printf("HID_INPUT iface=%u subclass=%u protocol=%u len=%u data=",
                                 input.interfaceNumber,
                                 input.subclass,
                                 input.protocol,
                                 static_cast<unsigned>(input.length));
                   printBytes(input.data, input.length, 8);
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
    else if (command == 'D')
    {
      // The report descriptor is fetched after the connect event, so an address
      // being latched does not yet mean this line has been filled in.
      const uint32_t startedAt = millis();
      while (!hidDescriptorSeen && millis() - startedAt < 3000)
      {
        delay(10);
      }
      Serial.println(hidDescriptorLine);
    }
  }
  delay(1);
}
