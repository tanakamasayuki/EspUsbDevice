#include "EspUsbHost.h"

// Host side for the HID keyboard + MSC composite test.
// Verifies both interfaces enumerate/claim with no duplicate endpoint
// address, and that both classes function (a key report and MSC capacity).

EspUsbHost usb;

static const uint16_t DEVICE_PID = 0x4021;
static uint8_t deviceAddress = 0;
static uint16_t devicePid = 0;

static void reportEnumeration()
{
  if (deviceAddress == 0)
  {
    Serial.println("HOST_ENUM pid=0000 ifcount=0 eps=0 dup=1 hid=0 msc=0 claimok=0");
    return;
  }

  EspUsbHostInterfaceInfo interfaces[8];
  const size_t ifCount = usb.getInterfaces(deviceAddress, interfaces, 8);

  uint8_t hidCount = 0;
  uint8_t mscCount = 0;
  uint8_t claimOk = 1;
  for (size_t i = 0; i < ifCount; i++)
  {
    if (interfaces[i].interfaceClass == 0x03)
    {
      hidCount++;
    }
    else if (interfaces[i].interfaceClass == 0x08)
    {
      mscCount++;
    }
    if (interfaces[i].claimAttempted && interfaces[i].claimResult != ESP_OK)
    {
      claimOk = 0;
    }
  }

  EspUsbHostEndpointInfo endpoints[12];
  const size_t epCount = usb.getEndpoints(deviceAddress, endpoints, 12);

  uint8_t dup = 0;
  for (size_t i = 0; i < epCount; i++)
  {
    for (size_t j = i + 1; j < epCount; j++)
    {
      if (endpoints[i].address == endpoints[j].address)
      {
        dup = 1;
      }
    }
  }

  Serial.printf("HOST_ENUM pid=%04x ifcount=%u eps=%u dup=%u hid=%u msc=%u claimok=%u\n",
                devicePid,
                static_cast<unsigned>(ifCount),
                static_cast<unsigned>(epCount),
                dup,
                hidCount,
                mscCount,
                claimOk);
}

static void waitForMsc()
{
  const uint32_t started = millis();
  while (!usb.mscReady() && millis() - started < 5000)
  {
    delay(10);
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          if (device.pid != DEVICE_PID)
                          {
                            return;
                          }
                          deviceAddress = device.address;
                          devicePid = device.pid;
                          Serial.printf("HOST_CONNECTED vid=%04x pid=%04x ifcount=%u\n",
                                        device.vid, device.pid, device.configurationInterfaceCount);
                        });

  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event)
                 {
                   if (event.pressed && event.ascii)
                   {
                     Serial.printf("KEY %c\n", static_cast<char>(event.ascii));
                   }
                 });

  if (!usb.begin())
  {
    Serial.printf("HOST_BEGIN_FAILED %s\n", usb.lastErrorName());
  }
}

void loop()
{
  if (Serial.available() > 0)
  {
    char command = Serial.read();
    if (command == 'e')
    {
      reportEnumeration();
    }
    else if (command == 'm')
    {
      waitForMsc();
      uint32_t blocks = 0;
      uint32_t blockSize = 0;
      const bool ok = usb.mscCapacity(blocks, blockSize);
      Serial.printf("MSC_CAPACITY ok=%u blocks=%lu block_size=%lu\n",
                    ok ? 1 : 0,
                    static_cast<unsigned long>(blocks),
                    static_cast<unsigned long>(blockSize));
    }
  }
  delay(1);
}
