#include "EspUsbDevice.h"
#include "EspUsbHost.h"
#include <string.h>

// Three CDC ACM ports on one ESP32-P4, brought up against a real host.
//
// The point of running this on hardware is that the descriptor unit tests stop
// at "the bytes are well formed". Three ports means six non-control IN
// endpoints on the high-speed controller, and every IN endpoint needs its own
// TxFIFO carved out of the controller's 1024-word data FIFO. Whether that
// allocation actually succeeds is decided by dcd_dwc2 at SET_CONFIGURATION,
// long after any descriptor check - so it has to be enumerated to be believed.
//
// Device sits on the high-speed controller because that is where the endpoint
// budget for a third port comes from; the host takes the other controller,
// which is full-speed, so the link itself negotiates full speed. The endpoint
// budget follows the controller, not the negotiated speed, so this exercises
// the six-IN-endpoint allocation. A three-port device on an actual high-speed
// link (512-byte bulk packets) needs a PC host - see tests/manual.

EspUsbDevice device;
EspUsbDeviceCdcSerial Port0(device, "Console");
EspUsbDeviceCdcSerial Port1(device, "Data Link");
EspUsbDeviceCdcSerial Port2(device, "Telemetry");

EspUsbHost usb;
EspUsbHostCdcSerial HostSerial(usb);

static volatile bool deviceConnected = false;
static uint8_t deviceAddress = 0;
static uint8_t deviceClass = 0;
static uint8_t deviceSubClass = 0;
static uint8_t deviceProtocol = 0;

static bool waitFor(volatile bool &flag, uint32_t timeoutMs)
{
  const uint32_t start = millis();
  while (!flag && millis() - start < timeoutMs)
  {
    delay(10);
  }
  return flag;
}

static bool waitHostRx(const char *expected, uint32_t timeoutMs = 3000)
{
  char buffer[64] = {};
  size_t length = 0;
  const uint32_t start = millis();
  while (millis() - start < timeoutMs)
  {
    while (HostSerial.available() > 0 && length + 1 < sizeof(buffer))
    {
      buffer[length++] = static_cast<char>(HostSerial.read());
      buffer[length] = '\0';
    }
    if (strcmp(buffer, expected) == 0)
    {
      Serial.printf("SERIAL_RX %s\n", buffer);
      return true;
    }
    delay(10);
  }
  Serial.printf("SERIAL_RX_TIMEOUT got=%s\n", buffer);
  return false;
}

static bool waitPortRx(EspUsbDeviceCdcSerial &port, const char *label,
                       const char *expected, uint32_t timeoutMs = 3000)
{
  char buffer[64] = {};
  size_t length = 0;
  const uint32_t start = millis();
  while (millis() - start < timeoutMs)
  {
    while (port.available() > 0 && length + 1 < sizeof(buffer))
    {
      buffer[length++] = static_cast<char>(port.read());
      buffer[length] = '\0';
    }
    if (strcmp(buffer, expected) == 0)
    {
      Serial.printf("%s %s\n", label, buffer);
      return true;
    }
    delay(10);
  }
  Serial.printf("%s_TIMEOUT got=%s\n", label, buffer);
  return false;
}

// What the host actually received, counted from the descriptor it parsed
// rather than from what the device believes it sent.
static bool reportEnumeration()
{
  EspUsbHostInterfaceInfo interfaces[16];
  const size_t ifCount = usb.getInterfaces(deviceAddress, interfaces, 16);

  uint8_t controlCount = 0;
  uint8_t dataCount = 0;
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
  }

  EspUsbHostEndpointInfo endpoints[16];
  const size_t epCount = usb.getEndpoints(deviceAddress, endpoints, 16);

  uint8_t dup = 0;
  uint8_t bulk = 0;
  uint8_t interrupt = 0;
  for (size_t i = 0; i < epCount; i++)
  {
    const uint8_t type = endpoints[i].attributes & 0x03;
    if (type == 0x02)
    {
      bulk++;
    }
    else if (type == 0x03)
    {
      interrupt++;
    }
    for (size_t j = i + 1; j < epCount; j++)
    {
      if (endpoints[i].address == endpoints[j].address)
      {
        dup = 1;
      }
    }
  }

  Serial.printf("HOST_ENUM ifcount=%u ctrl=%u data=%u eps=%u bulk=%u intr=%u dup=%u\n",
                static_cast<unsigned>(ifCount), controlCount, dataCount,
                static_cast<unsigned>(epCount), bulk, interrupt, dup);
  Serial.printf("HOST_CLASS class=%02x sub=%02x proto=%02x\n",
                deviceClass, deviceSubClass, deviceProtocol);

  // Three ports: 6 interfaces, 3 notification IN + 6 bulk, no address reused.
  return ifCount == 6 && controlCount == 3 && dataCount == 3 &&
         epCount == 9 && bulk == 6 && interrupt == 3 && dup == 0 &&
         deviceClass == 0xef && deviceSubClass == 0x02 &&
         deviceProtocol == 0x01;
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("TEST_BEGIN loopback_usb_serial_multi");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &info)
                        {
                          Serial.printf("HOST_DEVICE vid=0x%04x pid=0x%04x\n",
                                        info.vid, info.pid);
                          deviceAddress = info.address;
                          deviceClass = info.deviceClass;
                          deviceSubClass = info.deviceSubClass;
                          deviceProtocol = info.deviceProtocol;
                          deviceConnected = true;
                        });

  HostSerial.begin(115200);

  EspUsbHostConfig hostConfig;
  hostConfig.port = ESP_USB_HOST_PORT_FULL_SPEED;
  if (!usb.begin(hostConfig))
  {
    Serial.printf("HOST_BEGIN_FAILED %s\n", usb.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }
  Serial.println("HOST_READY fs");

  EspUsbDeviceConfig deviceConfig;
  deviceConfig.vid = 0x303a;
  deviceConfig.pid = 0x4019;
  deviceConfig.manufacturer = "EspUsb";
  deviceConfig.product = "EspUsbDevice Loopback Triple Serial";
  deviceConfig.serialNumber = "espusb-loopback-triple-serial";
  // Three ports need the high-speed controller's seven non-control IN
  // endpoints; the full-speed controller has four and would refuse this device.
  deviceConfig.controller = EspUsbController::HighSpeed;

  if (!device.begin(deviceConfig))
  {
    Serial.printf("DEVICE_BEGIN_FAILED %s\n", device.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }
  Serial.println("DEVICE_READY hs");

  if (!waitFor(deviceConnected, 30000))
  {
    Serial.printf("DEVICE_TIMEOUT host_error=%s device_error=%s\n",
                  usb.lastErrorName(), device.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }

  delay(500);

  // Reported here rather than at startup: everything printed before the host
  // enumerates the device races the serial capture attaching, which is why the
  // loopback tests only start matching at HOST_DEVICE. Port indices are the
  // TinyUSB instances the objects drive, taken from descriptor order.
  Serial.printf("DEVICE_PORTS max=%u p0=%u p1=%u p2=%u\n",
                EspUsbDevice::maxCdcPorts(),
                Port0.port(), Port1.port(), Port2.port());

  bool ok = reportEnumeration();

  // The host claims the first CDC function, so this is port 0. Both directions
  // on it prove the three-port device is a working device and not merely a
  // well-formed descriptor: the endpoints were opened and the FIFOs allocated.
  const uint8_t devicePayload[] = "port zero to host";
  Serial.printf("DEVICE_TX0 %u\n",
                Port0.write(devicePayload, sizeof(devicePayload) - 1) ==
                        sizeof(devicePayload) - 1
                    ? 1
                    : 0);
  ok = waitHostRx("port zero to host") && ok;

  const uint8_t hostPayload[] = "host to port zero\n";
  Serial.printf("SERIAL_TX %u\n",
                HostSerial.write(hostPayload, sizeof(hostPayload) - 1) ==
                        sizeof(hostPayload) - 1
                    ? 1
                    : 0);
  ok = waitPortRx(Port0, "DEVICE_RX0", "host to port zero\n") && ok;

  // Ports 1 and 2 accept writes - their endpoints are open and their FIFOs are
  // their own - and none of it reaches port 0's stream. Driving them from this
  // side needs a host that binds more than one CDC function per device; until
  // then this is the separation half of the check.
  Serial.printf("DEVICE_TX1 %u\n",
                Port1.write(reinterpret_cast<const uint8_t *>("port one\n"), 9) == 9 ? 1 : 0);
  Serial.printf("DEVICE_TX2 %u\n",
                Port2.write(reinterpret_cast<const uint8_t *>("port two\n"), 9) == 9 ? 1 : 0);
  delay(500);
  const int leaked = HostSerial.available();
  Serial.printf("SERIAL_PENDING %d\n", leaked);
  ok = (leaked == 0) && ok;

  Serial.printf("DEVICE_TX0 %u\n",
                Port0.write(devicePayload, sizeof(devicePayload) - 1) ==
                        sizeof(devicePayload) - 1
                    ? 1
                    : 0);
  ok = waitHostRx("port zero to host") && ok;

  Serial.println(ok ? "TEST_END ok" : "TEST_END fail");
  Serial.println(ok ? "OK" : "NG");
}

void loop()
{
  delay(1);
}
