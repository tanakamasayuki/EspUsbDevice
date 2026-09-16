#include "EspUsbDevice.h"

// Host-independent check that the HID class is found whatever its position in
// the registration order.
//
// TinyUSB asks for the report descriptor by its own instance number, which is 0
// for the one HID interface this build has. The library used to treat that
// number as a position in its class table, so a HID class registered after a
// vendor, CDC or MSC class was never found: hidReportDescriptor(0) returned
// nullptr, the host's GET_DESCRIPTOR(Report) went unanswered, and the HID
// function failed to start (Windows 11: HidUsb Code 10 about 6 s after arrival,
// with a WinUSB sibling held behind it - tests/manual/windows_device_guid).
//
// Each case builds the descriptors without starting TinyUSB and compares what
// the lookup returns against what the configuration descriptor advertises, so
// this runs on any board and needs no host.

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

// wDescriptorLength of the first HID class descriptor in configuration 0, or 0.
static uint16_t advertisedReportLength(EspUsbDevice &device)
{
  const uint8_t *cfg = device.configurationDescriptor(0);
  if (!cfg)
  {
    return 0;
  }
  const uint16_t total = static_cast<uint16_t>(cfg[2] | (cfg[3] << 8));
  for (uint16_t o = 0; o + 2 <= total && cfg[o]; o = static_cast<uint16_t>(o + cfg[o]))
  {
    if (cfg[o + 1] == 0x21 && cfg[o] >= 9)
    {
      return static_cast<uint16_t>(cfg[o + 7] | (cfg[o + 8] << 8));
    }
  }
  return 0;
}

// The lookup TinyUSB's callback goes through must hand back the class's own
// descriptor, at the length the configuration descriptor promised the host.
static void expectHidResolves(const char *name, EspUsbDevice &device,
                              const EspUsbDeviceClass &hid)
{
  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4031;
  config.startTinyUsb = false;
  check(device.begin(config), name);
  const uint8_t *found = device.hidReportDescriptor(0);
  const uint16_t foundLength = device.hidReportDescriptorLength(0);
  check(found != nullptr, name);
  check(found == hid.hidReportDescriptor(), name);
  check(foundLength == hid.hidReportDescriptorLength(), name);
  check(foundLength != 0 && foundLength == advertisedReportLength(device), name);
  // Only one HID interface exists, so nothing answers for instance 1.
  check(device.hidReportDescriptor(1) == nullptr, name);
  check(device.hidReportDescriptorLength(1) == 0, name);
  device.end();
}

// The order that used to work, as the control.
static void testHidFirst()
{
  EspUsbDevice device;
  EspUsbDeviceHidKeyboard keyboard(device);
  EspUsbDeviceVendor vendor(device);
  expectHidResolves("hid_then_vendor", device, keyboard);
}

// The order that failed: the vendor class occupied slot 0 and was not HID.
static void testVendorFirst()
{
  EspUsbDevice device;
  EspUsbDeviceVendor vendor(device);
  EspUsbDeviceHidKeyboard keyboard(device);
  expectHidResolves("vendor_then_hid", device, keyboard);
}

static void testCdcFirst()
{
  EspUsbDevice device;
  EspUsbDeviceCdcSerial cdc(device);
  EspUsbDeviceHidMouse mouse(device);
  expectHidResolves("cdc_then_hid", device, mouse);
}

// Two non-HID classes ahead of it, so slot 2 - further from 0 than any single
// mistake would land.
static void testHidLast()
{
  EspUsbDevice device;
  EspUsbDeviceVendor vendor(device);
  EspUsbDeviceCdcSerial cdc(device);
  EspUsbDeviceHidGamepad gamepad(device);
  expectHidResolves("vendor_cdc_then_hid", device, gamepad);
}

// Merged HID: the answer is the merged descriptor, not any one class's own,
// and the position of the non-HID class must not change that.
static void testCompositeHidAfterVendor()
{
  const char *name = "vendor_then_composite_hid";
  EspUsbDevice device;
  EspUsbDeviceVendor vendor(device);
  EspUsbDeviceHidKeyboard keyboard(device);
  EspUsbDeviceHidMouse mouse(device);
  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4031;
  config.startTinyUsb = false;
  check(device.begin(config), name);
  const uint8_t *found = device.hidReportDescriptor(0);
  const uint16_t foundLength = device.hidReportDescriptorLength(0);
  check(found != nullptr, name);
  check(found != keyboard.hidReportDescriptor() && found != mouse.hidReportDescriptor(), name);
  check(foundLength > keyboard.hidReportDescriptorLength(), name);
  check(foundLength == advertisedReportLength(device), name);
  device.end();
}

// No HID at all: the lookup must say so rather than hand back another class.
static void testNoHid()
{
  const char *name = "no_hid";
  EspUsbDevice device;
  EspUsbDeviceVendor vendor(device);
  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4031;
  config.startTinyUsb = false;
  check(device.begin(config), name);
  check(device.hidReportDescriptor(0) == nullptr, name);
  check(device.hidReportDescriptorLength(0) == 0, name);
  device.end();
}

void setup()
{
  Serial.begin(115200);
  delay(2000);
  Serial.println("TEST_BEGIN hid_registration_order");

  testHidFirst();
  testVendorFirst();
  testCdcFirst();
  testHidLast();
  testCompositeHidAfterVendor();
  testNoHid();

  Serial.printf("PASS %d FAIL %d\n", passCount, failCount);
  Serial.println("TEST_END");
  Serial.println(failCount == 0 ? "OK" : "NG");
}

void loop()
{
  delay(1000);
}
