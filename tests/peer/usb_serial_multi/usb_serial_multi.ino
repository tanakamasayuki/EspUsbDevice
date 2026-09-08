#include "EspUsbHost.h"

// Host side for the two-port CDC test.
//
// Checks the parts of a multi-port device that only a real host can see: that
// the device declares itself an IAD composite, that both ACM functions appear
// as their own association with their own name, and that the two data pipes
// are genuinely separate.
//
// EspUsbHost binds one CDC function per device - the first one, since its
// data-interface match requires no data interface to have been taken yet - so
// CdcSerial here is port 0. That is what makes the separation check meaningful:
// bytes the device writes to port 1 must not appear on this stream.


EspUsbHost usb;
EspUsbHostCdcSerial CdcSerial(usb);

// The S3 presents its built-in USB-Serial/JTAG (pid=0x1001) while booting,
// before EspUsbDevice takes over the OTG port. Latch only our own pid.
static const uint16_t DEVICE_PID = 0x4015;
static uint8_t deviceAddress = 0;
static uint16_t devicePid = 0;
static uint8_t deviceClass = 0;
static uint8_t deviceSubClass = 0;
static uint8_t deviceProtocol = 0;

static void reportEnumeration()
{
  if (deviceAddress == 0)
  {
    Serial.println("HOST_ENUM pid=0000 ifcount=0 eps=0 dup=1 ctrl=0 data=0 claimok=0");
    return;
  }

  EspUsbHostInterfaceInfo interfaces[8];
  const size_t ifCount = usb.getInterfaces(deviceAddress, interfaces, 8);

  uint8_t controlCount = 0;
  uint8_t dataCount = 0;
  uint8_t claimOk = 1;
  for (size_t i = 0; i < ifCount; i++)
  {
    if (interfaces[i].interfaceClass == 0x02 &&
        interfaces[i].interfaceSubClass == 0x02)
    {
      controlCount++;
    }
    else if (interfaces[i].interfaceClass == 0x0a)
    {
      dataCount++;
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

  Serial.printf("HOST_ENUM pid=%04x ifcount=%u eps=%u dup=%u ctrl=%u data=%u claimok=%u\n",
                devicePid,
                static_cast<unsigned>(ifCount),
                static_cast<unsigned>(epCount),
                dup,
                controlCount,
                dataCount,
                claimOk);
}

static void reportDeviceClass()
{
  Serial.printf("HOST_CLASS class=%02x sub=%02x proto=%02x\n",
                deviceClass, deviceSubClass, deviceProtocol);
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
                          deviceClass = device.deviceClass;
                          deviceSubClass = device.deviceSubClass;
                          deviceProtocol = device.deviceProtocol;
                          CdcSerial.setAddress(device.address);
                          while (CdcSerial.available() > 0)
                          {
                            CdcSerial.read();
                          }
                          Serial.printf("HOST_CONNECTED vid=%04x pid=%04x ifcount=%u\n",
                                        device.vid, device.pid,
                                        device.configurationInterfaceCount);
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
    const char command = Serial.read();
    if (command == 'e')
    {
      reportEnumeration();
    }
    else if (command == 'k')
    {
      reportDeviceClass();
    }
    else if (command == 'h')
    {
      Serial.printf("SERIAL_TX %u\n",
                    CdcSerial.write(reinterpret_cast<const uint8_t *>("host to port zero"), 17) == 17 ? 1 : 0);
    }
    else if (command == 'q')
    {
      // Nothing should be waiting. Printed as a count so the test can assert
      // silence rather than wait for a timeout.
      Serial.printf("SERIAL_PENDING %d\n", CdcSerial.available());
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
