// Two HID classes on one interface, seen by a real host.
//
// A composite HID device merges its classes' report descriptors into one and
// gives each class a Report ID, which has to go immediately after that class's
// Collection (Application) item. Finding that point by counting six bytes holds
// only for a descriptor that opens with a one-byte Usage Page.
// EspUsbDeviceHidVendor opens with a vendor-defined one - a three-byte item -
// so the cut landed inside the Collection item and every later item shifted by
// one. The device still enumerated; the descriptor was simply wrong.
//
// tests/single/descriptor checks the bytes the library builds. This checks the
// bytes a host actually fetched, and that reports under both IDs arrive - which
// is the part a descriptor test cannot reach.
#include "EspUsbDevice.h"
#include "EspUsbHost.h"
#include <string.h>

// 15 + the report ID is exactly the composite HID endpoint, so nothing here
// depends on how a longer report would be split across packets.
static constexpr uint16_t VENDOR_REPORT_SIZE = 15;

EspUsbDevice device;
EspUsbDeviceHidKeyboard Keyboard(device);
EspUsbDeviceHidVendor HidVendor(device, VENDOR_REPORT_SIZE);
EspUsbHost usb;

static volatile bool deviceConnected = false;
static volatile bool descriptorSeen = false;
static volatile bool keyboardReportSeen = false;
static volatile bool vendorReportSeen = false;
static volatile bool featureSeen = false;
static volatile bool ledSeen = false;

static bool waitFor(volatile bool &flag, uint32_t timeoutMs)
{
  const uint32_t started = millis();
  while (!flag && millis() - started < timeoutMs)
  {
    delay(10);
  }
  return flag;
}

// Length of one HID item, or 0 if it is malformed or runs past the end.
static uint16_t hidItemLen(const uint8_t *item, uint16_t available)
{
  if (available == 0)
  {
    return 0;
  }
  if (item[0] == 0xfe)
  {
    if (available < 3)
    {
      return 0;
    }
    const uint16_t length = static_cast<uint16_t>(3 + item[1]);
    return length <= available ? length : 0;
  }
  static const uint8_t dataSize[4] = {0, 1, 2, 4};
  const uint16_t length = static_cast<uint16_t>(1 + dataSize[item[0] & 0x03]);
  return length <= available ? length : 0;
}

static void reportMergedDescriptor(const uint8_t *data, uint16_t length)
{
  uint16_t offset = 0;
  uint8_t collections = 0;
  uint8_t stray = 0;
  char ids[32] = {};
  size_t idsLength = 0;

  while (offset < length)
  {
    const uint16_t itemLength = hidItemLen(&data[offset], static_cast<uint16_t>(length - offset));
    if (itemLength == 0)
    {
      break;
    }
    const uint8_t prefix = data[offset];
    // Collection (Application) only: a mouse nests a Physical collection inside
    // its Application one, so counting every Collection counts functions wrong.
    if ((prefix & 0xfc) == 0xa0 && itemLength >= 2 && data[offset + 1] == 0x01)
    {
      collections++;
    }
    else if ((prefix & 0xfc) == 0x84 && itemLength >= 2)
    {
      if (collections == 0)
      {
        stray++;
      }
      if (idsLength + 3 < sizeof(ids))
      {
        if (idsLength > 0)
        {
          ids[idsLength++] = ',';
        }
        idsLength += snprintf(&ids[idsLength], sizeof(ids) - idsLength, "%02x", data[offset + 1]);
      }
    }
    offset = static_cast<uint16_t>(offset + itemLength);
  }

  const bool wellFormed = (offset == length) && length > 0;
  Serial.printf("MERGED well_formed=%u collections=%u report_ids=%s stray=%u\n",
                wellFormed ? 1U : 0U, collections, ids, stray);
  descriptorSeen = wellFormed && collections == 2 && stray == 0 &&
                   strcmp(ids, "01,06") == 0;
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("TEST_BEGIN loopback_composite_hid_report_ids");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &deviceInfo)
                        {
                          Serial.printf("HOST_DEVICE vid=0x%04x pid=0x%04x\n",
                                        deviceInfo.vid, deviceInfo.pid);
                          deviceConnected = true;
                        });

  usb.onHIDReportDescriptor([](const EspUsbHostHIDReportDescriptor &descriptor)
                            { reportMergedDescriptor(descriptor.data, descriptor.length); });

  // Raw reports rather than a class callback, because what is being checked is
  // the report ID prefix the merge is responsible for.
  usb.onHIDInput([](const EspUsbHostHIDInput &input)
                 {
                   if (input.length == 0)
                   {
                     return;
                   }
                   if (input.data[0] == ESP_USB_DEVICE_HID_REPORT_ID_KEYBOARD)
                   {
                     Serial.printf("KEYBOARD_REPORT id=%u len=%u\n", input.data[0],
                                   static_cast<unsigned>(input.length));
                     keyboardReportSeen = true;
                   }
                   else if (input.data[0] == ESP_USB_DEVICE_HID_REPORT_ID_VENDOR)
                   {
                     Serial.printf("VENDOR_REPORT id=%u len=%u\n", input.data[0],
                                   static_cast<unsigned>(input.length));
                     vendorReportSeen = true;
                   }
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

  // The host-to-device direction is checked over the control path, which is the
  // one that carries a report ID. Reports on the interrupt OUT endpoint do not:
  // TinyUSB passes 0 because its HID driver does not parse report descriptors,
  // and the ID is supposed to be the payload's first byte instead. EspUsbHost
  // does not prefix it (tests/loopback/hid_vendor records the same host sending
  // "id=0 len=63" to a single-class device, where it happens not to matter), so
  // that path cannot be driven from here.
  HidVendor.onFeatureReport([](const EspUsbDeviceHidReport &report)
                            {
                              Serial.printf("DEVICE_FEATURE id=%u len=%u\n",
                                            report.reportId, report.length);
                              featureSeen = report.reportId == ESP_USB_DEVICE_HID_REPORT_ID_VENDOR &&
                                            report.length == VENDOR_REPORT_SIZE;
                            });

  Keyboard.onOutputReport([](const EspUsbDeviceHidKeyboardOutputReport &report)
                          {
                            Serial.printf("DEVICE_LED numlock=%u capslock=%u\n",
                                          report.numLock ? 1U : 0U,
                                          report.capsLock ? 1U : 0U);
                            ledSeen = true;
                          });

  EspUsbDeviceConfig deviceConfig;
  deviceConfig.vid = 0x303a;
  deviceConfig.pid = 0x402b;
  deviceConfig.manufacturer = "EspUsbDevice";
  deviceConfig.product = "EspUsbDevice Composite HID Report IDs";
  deviceConfig.serialNumber = "espusb-loopback-composite-hid-ids";

  if (!device.begin(deviceConfig))
  {
    Serial.printf("DEVICE_BEGIN_FAILED %s\n", device.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }
  Serial.println("DEVICE_READY fs");

  bool ok = waitFor(deviceConnected, 30000);
  ok = ok && waitFor(descriptorSeen, 5000);

  // Report ID 1 through the merged descriptor.
  const uint32_t keyboardDeadline = millis() + 3000;
  while (!keyboardReportSeen && millis() < keyboardDeadline)
  {
    Keyboard.tapUsage(ESP_USB_HID_KEY_A);
    delay(50);
  }
  ok = ok && keyboardReportSeen;

  // Report ID 6 through the same descriptor.
  uint8_t payload[VENDOR_REPORT_SIZE] = {};
  memcpy(payload, "vendor", 6);
  const uint32_t vendorDeadline = millis() + 3000;
  while (!vendorReportSeen && millis() < vendorDeadline)
  {
    HidVendor.sendInput(payload, sizeof(payload));
    delay(50);
  }
  ok = ok && vendorReportSeen;

  // And back the other way. Both of these reach the device as control
  // SET_REPORTs carrying a report ID, which is what the merged device has to
  // demultiplex on: ID 6 must land on the vendor function and ID 1 on the
  // keyboard, from one shared interface.
  uint8_t feature[VENDOR_REPORT_SIZE] = {};
  memcpy(feature, "host feature", 12);
  const bool sent = usb.sendHIDVendorFeature(feature, sizeof(feature));
  Serial.printf("SEND_FEATURE %u\n", sent ? 1U : 0U);
  ok = ok && sent && waitFor(featureSeen, 5000);

  const bool ledSent = usb.setKeyboardLeds(true, false, false);
  Serial.printf("SEND_LED %u\n", ledSent ? 1U : 0U);
  ok = ok && ledSent && waitFor(ledSeen, 5000);

  Serial.println(ok ? "TEST_END ok" : "TEST_END fail");
  Serial.println(ok ? "OK" : "NG");
}

void loop()
{
  delay(1);
}
