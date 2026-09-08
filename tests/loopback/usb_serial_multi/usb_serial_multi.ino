#include "EspUsbDevice.h"
#include "EspUsbHost.h"
#include <string.h>

// Three CDC ACM ports on one ESP32-P4, driven individually.
//
// The point of running this on hardware is that the descriptor unit tests stop
// at "the bytes are well formed". Three ports means six non-control IN
// endpoints on the device's high-speed controller, and every IN endpoint needs
// its own TxFIFO carved out of the controller's 1024-word data FIFO. Whether
// that allocation actually succeeds is decided by dcd_dwc2 at
// SET_CONFIGURATION, long after any descriptor check - so it has to be
// enumerated to be believed.
//
// Device sits on the high-speed controller because that is where the endpoint
// budget for a third port comes from; the host takes the other controller,
// which is full-speed, so the link itself negotiates full speed. The endpoint
// budget follows the controller, not the negotiated speed, so this exercises
// the six-IN-endpoint allocation. A three-port device on an actual high-speed
// link (512-byte bulk packets) needs a PC host - see tests/manual.
//
// On the host side three ports fit the full-speed controller's eight channels
// exactly: EP0 takes one and each port takes two (bulk IN + bulk OUT), which is
// seven. That is only true because the host does not claim the CDC control
// interface - its class requests go over EP0 - so no channel is spent on a
// notification endpoint nothing transfers on.

EspUsbDevice device;
EspUsbDeviceCdcSerial Port0(device, "Console");
EspUsbDeviceCdcSerial Port1(device, "Data Link");
EspUsbDeviceCdcSerial Port2(device, "Telemetry");

EspUsbHost usb;
EspUsbHostCdcSerial HostPort0(usb);
EspUsbHostCdcSerial HostPort1(usb);
EspUsbHostCdcSerial HostPort2(usb);

static EspUsbDeviceCdcSerial *const devicePorts[] = {&Port0, &Port1, &Port2};
static EspUsbHostCdcSerial *const hostPorts[] = {&HostPort0, &HostPort1, &HostPort2};
static const uint8_t PORT_COUNT = 3;

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

static bool waitHostRx(uint8_t port, const char *expected, uint32_t timeoutMs = 3000)
{
  char buffer[64] = {};
  size_t length = 0;
  const uint32_t start = millis();
  while (millis() - start < timeoutMs)
  {
    while (hostPorts[port]->available() > 0 && length + 1 < sizeof(buffer))
    {
      buffer[length++] = static_cast<char>(hostPorts[port]->read());
      buffer[length] = '\0';
    }
    if (strcmp(buffer, expected) == 0)
    {
      Serial.printf("SERIAL_RX%u %s\n", port, buffer);
      return true;
    }
    delay(10);
  }
  Serial.printf("SERIAL_RX%u_TIMEOUT got=%s\n", port, buffer);
  return false;
}

static bool waitDeviceRx(uint8_t port, const char *expected, uint32_t timeoutMs = 3000)
{
  char buffer[64] = {};
  size_t length = 0;
  const uint32_t start = millis();
  while (millis() - start < timeoutMs)
  {
    while (devicePorts[port]->available() > 0 && length + 1 < sizeof(buffer))
    {
      buffer[length++] = static_cast<char>(devicePorts[port]->read());
      buffer[length] = '\0';
    }
    if (strcmp(buffer, expected) == 0)
    {
      Serial.printf("DEVICE_RX%u %s\n", port, buffer);
      return true;
    }
    delay(10);
  }
  Serial.printf("DEVICE_RX%u_TIMEOUT got=%s\n", port, buffer);
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

// The mapping from a host-side port index onto the interfaces and endpoints the
// device published. This is what says the two sides agree on which function is
// port 0, rather than both merely having three of something.
static bool reportPorts()
{
  const uint8_t count = usb.serialPortCount(deviceAddress);
  Serial.printf("HOST_PORTS count=%u\n", count);
  bool ok = count == PORT_COUNT;
  for (uint8_t port = 0; port < PORT_COUNT; port++)
  {
    EspUsbHostSerialPortInfo info;
    if (!usb.getSerialPortInfo(info, deviceAddress, port))
    {
      Serial.printf("HOST_PORT %u missing\n", port);
      ok = false;
      continue;
    }
    Serial.printf("HOST_PORT %u ctrl=%u data=%u in=%02x out=%02x ready=%u\n",
                  port,
                  info.controlInterfaceNumber,
                  info.dataInterfaceNumber,
                  info.inEndpointAddress,
                  info.outEndpointAddress,
                  info.ready ? 1 : 0);
    ok = ok && info.ready;
  }
  return ok;
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

  // Bound before any device exists: the port index is a property of the
  // descriptor layout this test expects, not of the device that turns up.
  for (uint8_t port = 0; port < PORT_COUNT; port++)
  {
    hostPorts[port]->setPort(port);
    hostPorts[port]->begin(115200);
  }

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
  ok = reportPorts() && ok;

  // Every port, both directions, one at a time. A port that were silently
  // aliased onto another's endpoints would surface here as a timeout or as the
  // wrong port answering.
  for (uint8_t port = 0; port < PORT_COUNT; port++)
  {
    char payload[32];
    const int deviceLength = snprintf(payload, sizeof(payload), "port %u to host", port);
    Serial.printf("DEVICE_TX%u %u\n", port,
                  devicePorts[port]->write(reinterpret_cast<const uint8_t *>(payload),
                                           deviceLength) == static_cast<size_t>(deviceLength)
                      ? 1
                      : 0);
    ok = waitHostRx(port, payload) && ok;

    const int hostLength = snprintf(payload, sizeof(payload), "host to port %u\n", port);
    Serial.printf("SERIAL_TX%u %u\n", port,
                  hostPorts[port]->write(reinterpret_cast<const uint8_t *>(payload),
                                         hostLength) == static_cast<size_t>(hostLength)
                      ? 1
                      : 0);
    ok = waitDeviceRx(port, payload) && ok;
  }

  // Every exchange above was matched exactly, so anything left anywhere is
  // traffic that reached a port it was not addressed to.
  delay(300);
  int hostPending = 0;
  int devicePending = 0;
  for (uint8_t port = 0; port < PORT_COUNT; port++)
  {
    hostPending += hostPorts[port]->available();
    devicePending += devicePorts[port]->available();
  }
  Serial.printf("PENDING host=%d device=%d\n", hostPending, devicePending);
  ok = (hostPending == 0 && devicePending == 0) && ok;

  // SET_LINE_CODING is a control request carrying one port's own control
  // interface in wIndex, so this is the control path being per-port rather than
  // only the data path. The host never claimed those interfaces; the request
  // goes over EP0.
  Serial.printf("SERIAL_BAUD2 %u\n", HostPort2.setBaudRate(57600) ? 1 : 0);
  delay(200);
  Serial.printf("DEVICE_LINE_CODING p0=%lu p1=%lu p2=%lu\n",
                static_cast<unsigned long>(Port0.lineCoding().baud),
                static_cast<unsigned long>(Port1.lineCoding().baud),
                static_cast<unsigned long>(Port2.lineCoding().baud));
  ok = (Port0.lineCoding().baud == 115200 &&
        Port1.lineCoding().baud == 115200 &&
        Port2.lineCoding().baud == 57600) &&
       ok;

  Serial.println(ok ? "TEST_END ok" : "TEST_END fail");
  Serial.println(ok ? "OK" : "NG");
}

void loop()
{
  delay(1);
}
