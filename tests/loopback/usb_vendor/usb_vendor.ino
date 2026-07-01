#include "EspUsbDevice.h"
#include "EspUsbHost.h"
#include <string.h>

EspUsbDevice device;
EspUsbDeviceVendor DeviceVendor(device);
EspUsbHost usb;

static volatile bool deviceConnected = false;
static uint8_t deviceAddress = 0;
static volatile bool vendorDataSeen = false;
static char vendorData[64] = {};
static volatile uint32_t rxCount = 0;
static volatile uint32_t controlCount = 0;

static void pollDeviceVendorRx()
{
  size_t available = DeviceVendor.available();
  uint8_t buffer[64];
  while (available > 0)
  {
    const size_t chunk = DeviceVendor.read(buffer, min(available, sizeof(buffer)));
    if (chunk == 0)
    {
      break;
    }
    rxCount += chunk;
    DeviceVendor.write(reinterpret_cast<const uint8_t *>("echo:"), 5);
    DeviceVendor.write(buffer, chunk);
    DeviceVendor.flush();
    available = DeviceVendor.available();
  }
}

static bool waitFor(volatile bool &flag, uint32_t timeoutMs)
{
  const uint32_t start = millis();
  while (!flag && millis() - start < timeoutMs)
  {
    pollDeviceVendorRx();
    delay(10);
  }
  return flag;
}

static void printAscii(const uint8_t *data, size_t length)
{
  for (size_t i = 0; i < length; i++)
  {
    Serial.write(data[i]);
  }
}

static bool printVendorInfo()
{
  EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
  EspUsbHostEndpointInfo endpoints[ESP_USB_HOST_MAX_ENDPOINTS];

  const size_t interfaceCount = usb.getInterfaces(deviceAddress, interfaces, ESP_USB_HOST_MAX_INTERFACES);
  const size_t endpointCount = usb.getEndpoints(deviceAddress, endpoints, ESP_USB_HOST_MAX_ENDPOINTS);

  bool vendorInterface = false;
  bool bulkOut = false;
  bool bulkIn = false;
  uint8_t vendorInterfaceNumber = 0xff;

  for (size_t i = 0; i < interfaceCount; i++)
  {
    const EspUsbHostInterfaceInfo &itf = interfaces[i];
    Serial.printf("INTERFACE number=%u class=0x%02x subclass=0x%02x protocol=0x%02x endpoints=%u claimed=%u attempted=%u claim=%s\n",
                  itf.number,
                  itf.interfaceClass,
                  itf.interfaceSubClass,
                  itf.interfaceProtocol,
                  itf.endpointCount,
                  itf.claimed ? 1 : 0,
                  itf.claimAttempted ? 1 : 0,
                  esp_err_to_name(itf.claimResult));
    if (itf.interfaceClass == 0xff && itf.endpointCount == 2)
    {
      vendorInterface = true;
      vendorInterfaceNumber = itf.number;
    }
  }

  for (size_t i = 0; i < endpointCount; i++)
  {
    const EspUsbHostEndpointInfo &ep = endpoints[i];
    const bool isBulk = (ep.attributes & 0x03) == 0x02;
    Serial.printf("ENDPOINT iface=%u ep=0x%02x attrs=0x%02x mps=%u interval=%u\n",
                  ep.interfaceNumber,
                  ep.address,
                  ep.attributes,
                  ep.maxPacketSize,
                  ep.interval);
    if (ep.interfaceNumber == vendorInterfaceNumber && isBulk)
    {
      if (ep.address & 0x80)
      {
        bulkIn = true;
      }
      else
      {
        bulkOut = true;
      }
    }
  }

  Serial.printf("VENDOR_ENUM interface=%u bulk_out=%u bulk_in=%u interfaces=%u endpoints=%u claimed_channels=%u managed=%u\n",
                vendorInterface ? 1 : 0,
                bulkOut ? 1 : 0,
                bulkIn ? 1 : 0,
                static_cast<unsigned>(interfaceCount),
                static_cast<unsigned>(endpointCount),
                static_cast<unsigned>(usb.endpointChannelCount(deviceAddress)),
                static_cast<unsigned>(usb.managedEndpointCount(deviceAddress)));
  return vendorInterface && bulkOut && bulkIn;
}

static bool waitVendorData(const char *expected, uint32_t timeoutMs = 3000)
{
  uint8_t buffer[64] = {};
  size_t readLength = 0;
  const uint32_t start = millis();
  while (!vendorDataSeen && readLength == 0 && millis() - start < timeoutMs)
  {
    pollDeviceVendorRx();
    readLength = usb.vendorRead(buffer, sizeof(buffer) - 1, deviceAddress);
    if (readLength > 0)
    {
      buffer[readLength] = 0;
      const size_t copyLength = readLength < sizeof(vendorData) - 1 ? readLength : sizeof(vendorData) - 1;
      memcpy(vendorData, buffer, copyLength);
      vendorData[copyLength] = '\0';
      vendorDataSeen = true;
      break;
    }
    delay(1);
  }
  Serial.printf("VENDOR_DATA seen=%u data=%s\n", vendorDataSeen ? 1 : 0, vendorData);
  return vendorDataSeen && strcmp(vendorData, expected) == 0;
}

static bool readVendorData(const char *expected)
{
  uint8_t buffer[64];
  size_t length = usb.vendorRead(buffer, sizeof(buffer) - 1, deviceAddress);
  buffer[length] = 0;
  if (length == 0 && vendorDataSeen)
  {
    length = strlen(vendorData);
    Serial.printf("VENDOR_READ len=%u data=%s\n", static_cast<unsigned>(length), vendorData);
    return strcmp(vendorData, expected) == 0;
  }

  Serial.printf("VENDOR_READ len=%u data=%s\n", static_cast<unsigned>(length), reinterpret_cast<const char *>(buffer));
  return length == strlen(expected) && strcmp(reinterpret_cast<const char *>(buffer), expected) == 0;
}

static bool controlIn()
{
  uint8_t buffer[64];
  size_t actualLength = 0;
  const bool ok = usb.vendorControlIn(0x10, 0, 0, buffer, sizeof(buffer), &actualLength, deviceAddress);
  Serial.printf("VENDOR_CONTROL_IN ok=%u len=%u data=", ok ? 1 : 0, static_cast<unsigned>(actualLength));
  printAscii(buffer, actualLength);
  Serial.println();
  return ok && actualLength == 18 && memcmp(buffer, "EspUsbDeviceVendor", 18) == 0;
}

static bool webUsbUrl()
{
  uint8_t buffer[64] = {};
  size_t actualLength = 0;
  const bool ok = usb.vendorControlIn(0x01, 0, 0, buffer, sizeof(buffer), &actualLength, deviceAddress);
  bool found = false;
  static const char expected[] = "example.com/espusbdevice";
  for (size_t i = 0; ok && i + sizeof(expected) - 1 <= actualLength; i++)
  {
    if (memcmp(buffer + i, expected, sizeof(expected) - 1) == 0)
    {
      found = true;
      break;
    }
  }
  Serial.printf("WEBUSB_URL ok=%u len=%u found=%u\n",
                ok ? 1 : 0,
                static_cast<unsigned>(actualLength),
                found ? 1 : 0);
  return ok && found;
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("TEST_BEGIN loopback_usb_vendor");

  usb.onVendorData([](const EspUsbHostVendorData &data)
                   {
                     const size_t copyLength = data.length < sizeof(vendorData) - 1 ? data.length : sizeof(vendorData) - 1;
                     memcpy(vendorData, data.data, copyLength);
                     vendorData[copyLength] = '\0';
                     vendorDataSeen = true;
                   });

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &deviceInfo)
                        {
                          deviceAddress = deviceInfo.address;
                          deviceConnected = true;
                          Serial.printf("HOST_DEVICE address=%u vid=0x%04x pid=0x%04x supported=%u interfaces=%u\n",
                                        deviceInfo.address,
                                        deviceInfo.vid,
                                        deviceInfo.pid,
                                        deviceInfo.supported ? 1 : 0,
                                        deviceInfo.configurationInterfaceCount);
                        });

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

  DeviceVendor.onRx([](size_t)
                    { pollDeviceVendorRx(); });

  DeviceVendor.onControlRequest([](const EspUsbDeviceVendorControlRequest &request)
                                {
                                  controlCount++;
                                  static const char info[] = "EspUsbDeviceVendor";
                                  if ((request.bmRequestType & 0x80) && request.bRequest == 0x10)
                                  {
                                    return DeviceVendor.sendControlResponse(request, info, min(static_cast<size_t>(request.wLength), sizeof(info) - 1));
                                  }
                                  if (!(request.bmRequestType & 0x80) && request.bRequest == 0x11)
                                  {
                                    return DeviceVendor.sendControlResponse(request);
                                  }
                                  return false;
                                });

  EspUsbDeviceConfig deviceConfig;
  deviceConfig.port = ESP_USB_DEVICE_PORT_FULL_SPEED;
  deviceConfig.speed = ESP_USB_DEVICE_SPEED_FULL;
  deviceConfig.vid = 0x303a;
  deviceConfig.pid = 0x4019;
  deviceConfig.manufacturer = "EspUsbDevice";
  deviceConfig.product = "EspUsbDevice Loopback USB Vendor";
  deviceConfig.serialNumber = "espusb-loopback-usb-vendor";
  deviceConfig.webusbEnabled = true;
  deviceConfig.webusbUrl = "example.com/espusbdevice";

  if (!device.begin(deviceConfig))
  {
    Serial.printf("DEVICE_BEGIN_FAILED %s\n", device.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }
  Serial.println("DEVICE_READY fs");

  if (!waitFor(deviceConnected, 30000))
  {
    Serial.printf("DEVICE_TIMEOUT host_error=%s device_error=%s\n", usb.lastErrorName(), device.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }

  delay(500);

  bool ok = true;
  ok = ok && printVendorInfo();

  const bool opened = usb.vendorOpen(deviceAddress);
  Serial.printf("VENDOR_OPEN %u\n", opened ? 1 : 0);
  ok = ok && opened;

  vendorDataSeen = false;
  memset(vendorData, 0, sizeof(vendorData));
  static const uint8_t payload[] = {'p', 'i', 'n', 'g'};
  const bool written = usb.vendorWrite(payload, sizeof(payload), deviceAddress);
  Serial.printf("VENDOR_WRITE %u\n", written ? 1 : 0);
  ok = ok && written;
  ok = ok && waitVendorData("echo:ping");
  ok = ok && readVendorData("echo:ping");

  ok = ok && controlIn();

  const bool controlOutOk = usb.vendorControlOut(0x11, 0, 0, nullptr, 0, deviceAddress);
  Serial.printf("VENDOR_CONTROL_OUT %u\n", controlOutOk ? 1 : 0);
  ok = ok && controlOutOk;

  ok = ok && webUsbUrl();

  Serial.printf("DEVICE_STATUS rx=%lu control=%lu\n",
                static_cast<unsigned long>(rxCount),
                static_cast<unsigned long>(controlCount));
  ok = ok && rxCount == 4 && controlCount > 0;

  Serial.println(ok ? "TEST_END ok" : "TEST_END fail");
  Serial.println(ok ? "OK" : "NG");
}

void loop()
{
  pollDeviceVendorRx();
  delay(1);
}
