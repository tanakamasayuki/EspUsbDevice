#include "EspUsbDevice.h"

#include "cdc_multi_summary.h"

// Host-independent unit test for multi-port CDC.
//
// Covers the three things that decide whether a second serial port is real:
// the descriptor the device emits, the endpoint budget that bounds how many
// ports fit, and the identity (instance index, name strings, device class)
// that lets a host tell the ports apart and bind a driver per function.
//
// The endpoint expectations here are the S3/S2 numbers: 4 non-control IN
// endpoints, and 2 IN per ACM function.

static int passCount = 0;
static int failCount = 0;

static void check(bool condition, const char *name)
{
  if (condition)
  {
    passCount++;
  }
  else
  {
    Serial.print("FAIL ");
    Serial.println(name);
    failCount++;
  }
}

static EspUsbDeviceConfig testConfig()
{
  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4014;
  config.startTinyUsb = false;
  return config;
}

// Walks the configuration descriptor and counts what the host would see.
static ConfigSummary summarize(const uint8_t *descriptor)
{
  ConfigSummary summary;
  if (!descriptor)
  {
    return summary;
  }
  const uint16_t total =
      static_cast<uint16_t>(descriptor[2] | (descriptor[3] << 8));
  for (uint16_t offset = 9; offset + 2 <= total;)
  {
    const uint8_t length = descriptor[offset];
    if (length < 2)
    {
      break;
    }
    switch (descriptor[offset + 1])
    {
    case 0x04: // INTERFACE
      summary.interfaces++;
      break;
    case 0x0b: // INTERFACE ASSOCIATION
      if (summary.associations == 0)
      {
        summary.firstAssociationString = descriptor[offset + 7];
      }
      else if (summary.associations == 1)
      {
        summary.secondAssociationString = descriptor[offset + 7];
      }
      summary.associations++;
      break;
    case 0x05: // ENDPOINT
      if ((descriptor[offset + 2] & 0x80) != 0)
      {
        summary.inEndpoints++;
      }
      else
      {
        summary.outEndpoints++;
      }
      break;
    default:
      break;
    }
    offset = static_cast<uint16_t>(offset + length);
  }
  return summary;
}

static bool stringEquals(EspUsbDevice &device, uint8_t index, const char *expected)
{
  const uint16_t *descriptor = device.stringDescriptor(index, 0x0409);
  if (!descriptor || !expected)
  {
    return false;
  }
  const size_t length = strlen(expected);
  if ((descriptor[0] & 0xff) != 2 + length * 2)
  {
    return false;
  }
  for (size_t i = 0; i < length; i++)
  {
    if (descriptor[1 + i] != static_cast<uint16_t>(expected[i]))
    {
      return false;
    }
  }
  return true;
}

// Two ports is the S3 maximum, and the whole endpoint budget: 4 IN.
static void testTwoPortsBuild()
{
  EspUsbDevice device;
  EspUsbDeviceCdcSerial console(device, "Console");
  EspUsbDeviceCdcSerial data(device, "Data Link");

  check(device.begin(testConfig()), "two_ports_begin");
  check(device.lastError() == ESP_OK, "two_ports_no_error");

  const ConfigSummary summary = summarize(device.configurationDescriptor(0));
  check(summary.interfaces == 4, "two_ports_interface_count");
  check(summary.associations == 2, "two_ports_association_count");
  check(summary.inEndpoints == 4, "two_ports_in_endpoints");
  check(summary.outEndpoints == 2, "two_ports_out_endpoints");

  // Instance index must follow descriptor order: it is the index TinyUSB
  // assigns as it claims the interfaces, and the one tud_cdc_n_*() takes.
  check(console.port() == 0, "first_port_instance");
  check(data.port() == 1, "second_port_instance");

  // Distinct, non-zero iFunction strings are what keep the two identical ACM
  // functions apart in the host's device tree.
  check(summary.firstAssociationString != 0, "first_port_named");
  check(summary.secondAssociationString != 0, "second_port_named");
  check(summary.firstAssociationString != summary.secondAssociationString,
        "port_names_distinct");
  check(stringEquals(device, summary.firstAssociationString, "Console"),
        "first_port_name_value");
  check(stringEquals(device, summary.secondAssociationString, "Data Link"),
        "second_port_name_value");

  device.end();
}

// Endpoint addresses are what a host-side script hardcodes, so pin them.
static void testTwoPortsEndpointAddresses()
{
  EspUsbDevice device;
  EspUsbDeviceCdcSerial first(device, "A");
  EspUsbDeviceCdcSerial second(device, "B");
  check(device.begin(testConfig()), "addresses_begin");

  const uint8_t *descriptor = device.configurationDescriptor(0);
  const uint16_t total =
      static_cast<uint16_t>(descriptor[2] | (descriptor[3] << 8));
  uint8_t addresses[8] = {};
  uint8_t count = 0;
  for (uint16_t offset = 9; offset + 2 <= total;)
  {
    const uint8_t length = descriptor[offset];
    if (length < 2)
    {
      break;
    }
    if (descriptor[offset + 1] == 0x05 && count < sizeof(addresses))
    {
      addresses[count++] = descriptor[offset + 2];
    }
    offset = static_cast<uint16_t>(offset + length);
  }

  // Port 0: notification 0x81, data OUT 0x02 / IN 0x82.
  // Port 1: notification 0x83, data OUT 0x04 / IN 0x84.
  check(count == 6, "address_count");
  check(addresses[0] == 0x81, "port0_notification");
  check(addresses[1] == 0x02, "port0_out");
  check(addresses[2] == 0x82, "port0_in");
  check(addresses[3] == 0x83, "port1_notification");
  check(addresses[4] == 0x04, "port1_out");
  check(addresses[5] == 0x84, "port1_in");

  device.end();
}

// An IAD in the configuration obliges the device descriptor to say so, which
// is what makes Windows load usbccgp and bind a driver per function.
static void testInterfaceAssociationDeviceClass()
{
  EspUsbDevice device;
  EspUsbDeviceCdcSerial first(device, "A");
  EspUsbDeviceCdcSerial second(device, "B");
  check(device.begin(testConfig()), "iad_class_begin");

  const uint8_t *deviceDescriptor = device.deviceDescriptor();
  check(deviceDescriptor[4] == 0xef, "iad_device_class");
  check(deviceDescriptor[5] == 0x02, "iad_device_subclass");
  check(deviceDescriptor[6] == 0x01, "iad_device_protocol");

#if defined(CONFIG_IDF_TARGET_ESP32P4)
  // On a high-speed-capable part the device qualifier has to agree, or a host
  // sees two different devices depending on which descriptor it asked for.
  const uint8_t *qualifier = device.deviceQualifierDescriptor();
  check(qualifier != nullptr, "qualifier_present");
  check(qualifier && qualifier[4] == 0xef, "qualifier_device_class");
  check(qualifier && qualifier[5] == 0x02, "qualifier_device_subclass");
  check(qualifier && qualifier[6] == 0x01, "qualifier_device_protocol");
#else
  // A full-speed-only controller has no other speed to describe.
  check(device.deviceQualifierDescriptor() == nullptr, "no_qualifier_on_fs");
#endif

  device.end();
}

// A function model with no association keeps the plain 0x00 device class.
static void testNoAssociationKeepsZeroClass()
{
  EspUsbDevice device;
  EspUsbDeviceHidKeyboard keyboard(device);
  check(device.begin(testConfig()), "hid_only_begin");

  const uint8_t *deviceDescriptor = device.deviceDescriptor();
  check(deviceDescriptor[4] == 0x00, "hid_only_device_class");
  check(deviceDescriptor[5] == 0x00, "hid_only_device_subclass");
  check(deviceDescriptor[6] == 0x00, "hid_only_device_protocol");

  device.end();
}

// 2 IN per port x 3 ports = 6, over the 4 the S3 controller has.
static void testThirdPortRejected()
{
  EspUsbDevice device;
  EspUsbDeviceCdcSerial first(device, "A");
  EspUsbDeviceCdcSerial second(device, "B");
  EspUsbDeviceCdcSerial third(device, "C");

  check(!device.begin(testConfig()), "third_port_rejected");
  check(device.lastError() == ESP_ERR_INVALID_SIZE, "third_port_error");
}

// Two ports already spend the whole IN budget, so nothing fits beside them.
static void testTwoPortsPlusHidRejected()
{
  EspUsbDevice device;
  EspUsbDeviceHidKeyboard keyboard(device);
  EspUsbDeviceCdcSerial first(device, "A");
  EspUsbDeviceCdcSerial second(device, "B");

  check(!device.begin(testConfig()), "two_ports_plus_hid_rejected");
  check(device.lastError() == ESP_ERR_INVALID_SIZE,
        "two_ports_plus_hid_error");
}

// One port still leaves room for the HID + Vendor pair - the S3 ceiling.
static void testOnePortWithHidAndVendor()
{
  EspUsbDevice device;
  EspUsbDeviceHidKeyboard keyboard(device);
  EspUsbDeviceVendor vendor(device);
  EspUsbDeviceCdcSerial serial(device, "Console");

  check(device.begin(testConfig()), "one_port_hid_vendor_begin");
  check(device.lastError() == ESP_OK, "one_port_hid_vendor_no_error");

  const ConfigSummary summary = summarize(device.configurationDescriptor(0));
  check(summary.inEndpoints == 4, "one_port_hid_vendor_in_endpoints");
  check(serial.port() == 0, "one_port_hid_vendor_instance");

  device.end();
}

// An unnamed port is still legal; it just publishes no string.
static void testUnnamedPort()
{
  EspUsbDevice device;
  EspUsbDeviceCdcSerial serial(device);
  check(device.begin(testConfig()), "unnamed_begin");

  const ConfigSummary summary = summarize(device.configurationDescriptor(0));
  check(summary.firstAssociationString == 0, "unnamed_no_string");

  device.end();
}

static void testPortCapacity()
{
  // S2/S3 have 4 non-control IN endpoints, which is exactly two ACM functions.
  check(EspUsbDevice::maxCdcPorts() == 2, "port_capacity");
}

void setup()
{
  Serial.begin(115200);
  delay(5000);

  Serial.println("TEST_BEGIN cdc_multi");
  testTwoPortsBuild();
  testTwoPortsEndpointAddresses();
  testInterfaceAssociationDeviceClass();
  testNoAssociationKeepsZeroClass();
  testThirdPortRejected();
  testTwoPortsPlusHidRejected();
  testOnePortWithHidAndVendor();
  testUnnamedPort();
  testPortCapacity();
  Serial.printf("TEST_END pass=%d fail=%d\n", passCount, failCount);
  Serial.println(failCount == 0 ? "OK" : "NG");
  Serial.flush();
}

void loop()
{
}
