#include "EspUsbHost.h"
#include <string.h>

// Host side for the HID keyboard + CDC ACM composite test.
// Verifies (1) the device enumerates with both interfaces claimed and no
// duplicate endpoint address, and (2) both classes actually function.

EspUsbHost usb;
EspUsbHostCdcSerial CdcSerial(usb);

// Our composite device's product id. The ESP32-S3 briefly presents its
// built-in USB-Serial/JTAG (pid=0x1001) while booting, before EspUsbDevice
// takes over the OTG port; we ignore that transient device and only latch
// our own pid so the test never races the boot window.
static const uint16_t DEVICE_PID = 0x4020;
static uint8_t deviceAddress = 0;
static uint16_t devicePid = 0;

static void reportEnumeration()
{
  if (deviceAddress == 0)
  {
    Serial.println("HOST_ENUM pid=0000 ifcount=0 eps=0 dup=1 hid=0 cdc=0 claimok=0");
    return;
  }

  EspUsbHostInterfaceInfo interfaces[8];
  const size_t ifCount = usb.getInterfaces(deviceAddress, interfaces, 8);

  uint8_t hidCount = 0;
  uint8_t cdcCount = 0;
  uint8_t claimOk = 1;
  for (size_t i = 0; i < ifCount; i++)
  {
    if (interfaces[i].interfaceClass == 0x03)
    {
      hidCount++;
    }
    else if (interfaces[i].interfaceClass == 0x02 || interfaces[i].interfaceClass == 0x0a)
    {
      cdcCount++;
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

  Serial.printf("HOST_ENUM pid=%04x ifcount=%u eps=%u dup=%u hid=%u cdc=%u claimok=%u\n",
                devicePid,
                static_cast<unsigned>(ifCount),
                static_cast<unsigned>(epCount),
                dup,
                hidCount,
                cdcCount,
                claimOk);
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          if (device.pid != DEVICE_PID)
                          {
                            return; // ignore the transient USB-Serial/JTAG.
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

  CdcSerial.begin(115200);

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
    else if (command == 'h')
    {
      Serial.printf("SERIAL_TX %u\n",
                    CdcSerial.write(reinterpret_cast<const uint8_t *>("host to serial"), 14) == 14 ? 1 : 0);
    }
  }

  if (CdcSerial.available() > 0)
  {
    Serial.print("SERIAL_RX ");
    while (CdcSerial.available() > 0)
    {
      Serial.write(CdcSerial.read());
    }
    Serial.println();
  }
  delay(1);
}
