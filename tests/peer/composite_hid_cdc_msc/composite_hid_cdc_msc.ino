#include "EspUsbHost.h"

// Host side for the HID+CDC+MSC composite (the max that fits the S3 endpoint
// budget). Verifies all interfaces enumerate/claim with no duplicate endpoint
// address, and that each class functions.

EspUsbHost usb;
EspUsbHostCdcSerial CdcSerial(usb);

static const uint16_t DEVICE_PID = 0x4022;
static uint8_t deviceAddress = 0;
static uint16_t devicePid = 0;

static void reportEnumeration()
{
  if (deviceAddress == 0)
  {
    Serial.println("HOST_ENUM pid=0000 ifcount=0 eps=0 dup=1 hid=0 cdc=0 msc=0 claimok=0");
    return;
  }

  EspUsbHostInterfaceInfo interfaces[12];
  const size_t ifCount = usb.getInterfaces(deviceAddress, interfaces, 12);

  uint8_t hidCount = 0, cdcCount = 0, mscCount = 0, claimOk = 1;
  for (size_t i = 0; i < ifCount; i++)
  {
    switch (interfaces[i].interfaceClass)
    {
    case 0x03:
      hidCount++;
      break;
    case 0x02:
    case 0x0a:
      cdcCount++;
      break;
    case 0x08:
      mscCount++;
      break;
    }
    if (interfaces[i].claimAttempted && interfaces[i].claimResult != ESP_OK)
    {
      claimOk = 0;
    }
    Serial.printf("HOST_IF num=%u class=0x%02x claimed=%u result=0x%x\n",
                  interfaces[i].number, interfaces[i].interfaceClass,
                  interfaces[i].claimed ? 1 : 0, interfaces[i].claimResult);
  }

  EspUsbHostEndpointInfo endpoints[16];
  const size_t epCount = usb.getEndpoints(deviceAddress, endpoints, 16);

  uint8_t dup = 0;
  for (size_t i = 0; i < epCount; i++)
  {
    Serial.printf("HOST_EP addr=0x%02x itf=%u attr=0x%02x mps=%u\n",
                  endpoints[i].address, endpoints[i].interfaceNumber,
                  endpoints[i].attributes, endpoints[i].maxPacketSize);
    for (size_t j = i + 1; j < epCount; j++)
    {
      if (endpoints[i].address == endpoints[j].address)
      {
        dup = 1;
      }
    }
  }

  Serial.printf("HOST_ENUM pid=%04x ifcount=%u eps=%u dup=%u hid=%u cdc=%u msc=%u claimok=%u\n",
                devicePid, static_cast<unsigned>(ifCount), static_cast<unsigned>(epCount),
                dup, hidCount, cdcCount, mscCount, claimOk);
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
                          Serial.printf("HOST_SEEN vid=%04x pid=%04x supported=%u ifcount=%u\n",
                                        device.vid, device.pid, device.supported ? 1 : 0,
                                        device.configurationInterfaceCount);
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
    else if (command == 'm')
    {
      waitForMsc();
      uint32_t blocks = 0, blockSize = 0;
      const bool ok = usb.mscCapacity(blocks, blockSize);
      Serial.printf("MSC_CAPACITY ok=%u blocks=%lu block_size=%lu\n",
                    ok ? 1 : 0, static_cast<unsigned long>(blocks), static_cast<unsigned long>(blockSize));
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
