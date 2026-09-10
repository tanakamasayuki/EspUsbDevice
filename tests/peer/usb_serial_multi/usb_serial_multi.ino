#include "EspUsbHost.h"

// Host side for the two-port CDC test.
//
// Checks the parts of a multi-port device that only a real host can see: that
// the device declares itself an IAD composite, that both ACM functions come up
// as their own port with their own endpoints, that each port carries data in
// both directions, and that the two pipes stay separate.
//
// One EspUsbHostCdcSerial is bound per port. Leaving the port unset would make
// an object follow the device's first ready port, which is what a single-port
// device gives; naming the port is what makes the two ACM functions - identical
// in every respect but their interface numbers - individually addressable.

EspUsbHost usb;
EspUsbHostCdcSerial Port0Serial(usb);
EspUsbHostCdcSerial Port1Serial(usb);

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

// The mapping from a host-side port index onto the interfaces and endpoints the
// device actually published. This is what says the host and the device agree on
// which function is port 0 and which is port 1, rather than both merely having
// two of something.
static void reportPorts()
{
  Serial.printf("HOST_PORTS count=%u\n",
                usb.serialPortCount(deviceAddress));
  for (uint8_t port = 0; port < 2; port++)
  {
    EspUsbHostSerialPortInfo info;
    if (!usb.getSerialPortInfo(info, deviceAddress, port))
    {
      Serial.printf("HOST_PORT %u missing\n", port);
      continue;
    }
    Serial.printf("HOST_PORT %u ctrl=%u data=%u in=%02x out=%02x ready=%u\n",
                  port,
                  info.controlInterfaceNumber,
                  info.dataInterfaceNumber,
                  info.inEndpointAddress,
                  info.outEndpointAddress,
                  info.ready ? 1 : 0);
  }
}

static void drain(EspUsbHostCdcSerial &port, const char *label)
{
  if (port.available() <= 0)
  {
    return;
  }
  Serial.print(label);
  Serial.print(' ');
  while (port.available() > 0)
  {
    Serial.write(port.read());
  }
  Serial.println();
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
                          Port0Serial.setAddress(device.address);
                          Port1Serial.setAddress(device.address);
                          while (Port0Serial.available() > 0)
                          {
                            Port0Serial.read();
                          }
                          while (Port1Serial.available() > 0)
                          {
                            Port1Serial.read();
                          }
                          Serial.printf("HOST_CONNECTED vid=%04x pid=%04x ifcount=%u\n",
                                        device.vid, device.pid,
                                        device.configurationInterfaceCount);
                        });

  // Bound before any device exists: the port index is a property of the
  // descriptor layout this test expects, not of the device that turns up.
  Port0Serial.setPort(0);
  Port1Serial.setPort(1);
  Port0Serial.begin(115200);
  Port1Serial.begin(115200);

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
    const char command = Serial.read();
    waitForDevice();
    if (command == 'e')
    {
      reportEnumeration();
    }
    else if (command == 'k')
    {
      reportDeviceClass();
    }
    else if (command == 'p')
    {
      reportPorts();
    }
    else if (command == 'h')
    {
      Serial.printf("SERIAL_TX0 %u\n",
                    Port0Serial.write(reinterpret_cast<const uint8_t *>("host to port zero"), 17) == 17 ? 1 : 0);
    }
    else if (command == 'H')
    {
      Serial.printf("SERIAL_TX1 %u\n",
                    Port1Serial.write(reinterpret_cast<const uint8_t *>("host to port one"), 16) == 16 ? 1 : 0);
    }
    else if (command == 'b')
    {
      // Line coding is a control request on that port's own control interface,
      // so this is the control path being per-port, not just the data path.
      Serial.printf("SERIAL_BAUD1 %u\n", Port1Serial.setBaudRate(57600) ? 1 : 0);
    }
    else if (command == 'q')
    {
      // Printed as counts so the test can assert silence rather than wait for a
      // timeout.
      Serial.printf("SERIAL_PENDING p0=%d p1=%d\n",
                    Port0Serial.available(), Port1Serial.available());
    }
  }

  drain(Port0Serial, "SERIAL_RX0");
  drain(Port1Serial, "SERIAL_RX1");
  delay(1);
}
