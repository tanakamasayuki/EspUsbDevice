// USB host side of the DFU peer test. Drives the DFU control requests by hand
// over EP0 - there is no DFU host class here, and none is needed: every DFU
// transfer is a control transfer, which is the whole point of the class.
#include "EspUsbHost.h"

EspUsbHost usb;

static uint8_t deviceAddress = 0;
static volatile bool connected = false;
static uint8_t dfuInterface = 0xff;

// DFU 1.1 requests and states.
static constexpr uint8_t DFU_DETACH = 0;
static constexpr uint8_t DFU_DNLOAD = 1;
static constexpr uint8_t DFU_GETSTATUS = 3;
static constexpr uint8_t DFU_CLRSTATUS = 4;
static constexpr uint8_t DFU_ABORT = 6;
static constexpr uint8_t DFU_OUT = 0x21; // host->device, class, interface
static constexpr uint8_t DFU_IN = 0xa1;  // device->host, class, interface

struct DfuStatus
{
  bool ok = false;
  uint8_t status = 0xff;
  uint8_t state = 0xff;
  uint32_t pollTimeout = 0;
};

static bool waitForDevice(uint32_t timeoutMs = 5000)
{
  const uint32_t startedAt = millis();
  while (deviceAddress == 0 && millis() - startedAt < timeoutMs)
  {
    delay(10);
  }
  return deviceAddress != 0;
}

static DfuStatus getStatus()
{
  DfuStatus result;
  uint8_t buffer[6] = {};
  size_t actual = 0;
  if (!usb.vendorControlTransfer(DFU_IN, DFU_GETSTATUS, 0, dfuInterface, buffer,
                                 sizeof(buffer), &actual, deviceAddress) ||
      actual != sizeof(buffer))
  {
    return result;
  }
  result.ok = true;
  result.status = buffer[0];
  result.pollTimeout = static_cast<uint32_t>(buffer[1]) |
                       (static_cast<uint32_t>(buffer[2]) << 8) |
                       (static_cast<uint32_t>(buffer[3]) << 16);
  result.state = buffer[4];
  return result;
}

// One GETSTATUS, then as many more as it takes to reach a state the device will
// stay in, waiting the bwPollTimeout it asked for each time. That polling loop
// is the DFU host's whole job during a download.
//
// dfuMANIFEST (7) belongs in this list as much as dfuDNBUSY (4) does. The device
// enters it and only then runs its verification, so a host that stops polling at
// 7 reads the status from before the answer existed - which is how the first run
// of this test saw "status=0 state=7" for an image that was in fact rejected.
static bool transientState(uint8_t state)
{
  return state == 3 /* dfuDNLOAD_SYNC */ || state == 4 /* dfuDNBUSY */ ||
         state == 6 /* dfuMANIFEST_SYNC */ || state == 7 /* dfuMANIFEST */;
}

static DfuStatus settleStatus(uint32_t attempts = 20)
{
  DfuStatus status = getStatus();
  while (status.ok && transientState(status.state) && attempts > 0)
  {
    delay(status.pollTimeout + 1);
    status = getStatus();
    attempts--;
  }
  return status;
}

// Fill a block with something that looks like the start of an ESP application
// image when `magic` is set: byte 0 is the image magic the OTA writer checks
// before it accepts anything at all.
static void fillBlock(uint8_t *data, size_t length, bool magic)
{
  for (size_t i = 0; i < length; i++)
  {
    data[i] = static_cast<uint8_t>(i & 0xff);
  }
  data[0] = magic ? 0xe9 : 0x00;
}

static void downloadBlock(uint16_t blockNumber, bool magic, size_t length)
{
  static uint8_t block[256];
  if (length > sizeof(block))
  {
    length = sizeof(block);
  }
  fillBlock(block, length, magic);

  const bool sent = usb.vendorControlTransfer(DFU_OUT, DFU_DNLOAD, blockNumber,
                                              dfuInterface, block, length,
                                              nullptr, deviceAddress);
  const DfuStatus status = settleStatus();
  Serial.printf("DFU_DNLOAD sent=%u block=%u len=%u ok=%u status=%u state=%u\n",
                sent ? 1 : 0, blockNumber, static_cast<unsigned>(length),
                status.ok ? 1 : 0, status.status, status.state);
}

static void manifest()
{
  const bool sent = usb.vendorControlTransfer(DFU_OUT, DFU_DNLOAD, 0,
                                              dfuInterface, nullptr, 0,
                                              nullptr, deviceAddress);
  const DfuStatus status = settleStatus();
  Serial.printf("DFU_MANIFEST sent=%u ok=%u status=%u state=%u\n",
                sent ? 1 : 0, status.ok ? 1 : 0, status.status, status.state);
}

static void printInterfaces()
{
  EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
  EspUsbHostEndpointInfo endpoints[ESP_USB_HOST_MAX_ENDPOINTS];

  const size_t interfaceCount = usb.getInterfaces(deviceAddress, interfaces, ESP_USB_HOST_MAX_INTERFACES);
  const size_t endpointCount = usb.getEndpoints(deviceAddress, endpoints, ESP_USB_HOST_MAX_ENDPOINTS);

  dfuInterface = 0xff;
  uint8_t dfuEndpoints = 0xff;
  uint8_t dfuProtocol = 0xff;
  for (size_t i = 0; i < interfaceCount; i++)
  {
    const EspUsbHostInterfaceInfo &itf = interfaces[i];
    Serial.printf("INTERFACE number=%u class=0x%02x subclass=0x%02x protocol=0x%02x endpoints=%u\n",
                  itf.number, itf.interfaceClass, itf.interfaceSubClass,
                  itf.interfaceProtocol, itf.endpointCount);
    if (itf.interfaceClass == 0xfe && itf.interfaceSubClass == 0x01)
    {
      dfuInterface = itf.number;
      dfuEndpoints = itf.endpointCount;
      dfuProtocol = itf.interfaceProtocol;
    }
  }
  for (size_t i = 0; i < endpointCount; i++)
  {
    const EspUsbHostEndpointInfo &ep = endpoints[i];
    Serial.printf("ENDPOINT iface=%u ep=0x%02x attrs=0x%02x mps=%u\n",
                  ep.interfaceNumber, ep.address, ep.attributes, ep.maxPacketSize);
  }
  Serial.printf("DFU_ENUM interface=%u protocol=%u endpoints=%u interfaces=%u\n",
                dfuInterface, dfuProtocol, dfuEndpoints,
                static_cast<unsigned>(interfaceCount));
}

// Read the configuration descriptor with a plain GET_DESCRIPTOR and walk it for
// the DFU functional descriptor (bDescriptorType 0x21 inside a 0xfe/0x01
// interface). Reading the bytes the device actually sent, rather than the host
// driver's parsed view, is what checks wTransferSize and bcdDFUVersion.
static void printFunctional()
{
  uint8_t buffer[256] = {};
  size_t actual = 0;
  const bool ok = usb.vendorControlTransfer(0x80, 6 /* GET_DESCRIPTOR */,
                                            0x0200 /* configuration, index 0 */, 0,
                                            buffer, sizeof(buffer), &actual,
                                            deviceAddress);
  if (!ok || actual < 9)
  {
    Serial.println("DFU_FUNCTIONAL ok=0");
    return;
  }

  bool inDfu = false;
  size_t offset = 0;
  while (offset + 2 <= actual && buffer[offset] > 0)
  {
    const uint8_t length = buffer[offset];
    const uint8_t type = buffer[offset + 1];
    if (type == 0x04 && length >= 9)
    {
      inDfu = buffer[offset + 5] == 0xfe && buffer[offset + 6] == 0x01;
    }
    else if (type == 0x21 && inDfu && length >= 9)
    {
      const uint16_t timeout = static_cast<uint16_t>(buffer[offset + 3]) |
                               (static_cast<uint16_t>(buffer[offset + 4]) << 8);
      const uint16_t transfer = static_cast<uint16_t>(buffer[offset + 5]) |
                                (static_cast<uint16_t>(buffer[offset + 6]) << 8);
      const uint16_t bcd = static_cast<uint16_t>(buffer[offset + 7]) |
                           (static_cast<uint16_t>(buffer[offset + 8]) << 8);
      Serial.printf("DFU_FUNCTIONAL ok=1 attrs=0x%02x timeout=%u transfer=%u bcd=0x%04x total=%u\n",
                    buffer[offset + 2], timeout, transfer, bcd,
                    static_cast<unsigned>(actual));
      return;
    }
    offset += length;
  }
  Serial.println("DFU_FUNCTIONAL ok=0");
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &info)
                        {
                          deviceAddress = info.address;
                          connected = true;
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
    const char command = Serial.read();
    waitForDevice();
    if (command == 'i')
    {
      printInterfaces();
    }
    else if (command == 'f')
    {
      printFunctional();
    }
    else if (command == 's')
    {
      const DfuStatus status = settleStatus();
      Serial.printf("DFU_STATUS ok=%u status=%u state=%u poll=%u\n",
                    status.ok ? 1 : 0, status.status, status.state,
                    static_cast<unsigned>(status.pollTimeout));
    }
    else if (command == 'c')
    {
      const bool sent = usb.vendorControlTransfer(DFU_OUT, DFU_CLRSTATUS, 0,
                                                  dfuInterface, nullptr, 0,
                                                  nullptr, deviceAddress);
      const DfuStatus status = settleStatus();
      Serial.printf("DFU_CLRSTATUS sent=%u status=%u state=%u\n",
                    sent ? 1 : 0, status.status, status.state);
    }
    else if (command == 'x')
    {
      const bool sent = usb.vendorControlTransfer(DFU_OUT, DFU_ABORT, 0,
                                                  dfuInterface, nullptr, 0,
                                                  nullptr, deviceAddress);
      const DfuStatus status = settleStatus();
      Serial.printf("DFU_ABORT sent=%u status=%u state=%u\n",
                    sent ? 1 : 0, status.status, status.state);
    }
    else if (command == '1')
    {
      downloadBlock(0, false, 64);
    }
    else if (command == '2')
    {
      downloadBlock(0, true, 256);
    }
    else if (command == '3')
    {
      downloadBlock(1, true, 256);
    }
    else if (command == 'm')
    {
      manifest();
    }
  }
}
