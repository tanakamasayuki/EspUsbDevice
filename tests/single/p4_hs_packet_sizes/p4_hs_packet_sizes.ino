// High speed is where the packet sizes this library used to hard-code start to
// matter, and the ESP32-P4 is the only target that reaches it.
//
// Two ceilings move: a bulk endpoint goes from 64 bytes to 512, and an interrupt
// endpoint from 64 to as much as 1024. The full-speed and high-speed
// configurations are separate descriptors for exactly that reason, and the point
// of this test is that each carries the value legal for its own speed - a device
// that answers 512 in a full-speed descriptor is not merely wasteful, it is
// describing an endpoint USB 2.0 does not define.
//
// No host is involved. begin() with startTinyUsb = false builds both
// configurations on the chip and nothing is enumerated.
#include "EspUsbDevice.h"
#include <string.h>

static int passCount = 0;
static int failCount = 0;

static void check(bool condition, const char *name)
{
  if (condition)
  {
    ++passCount;
  }
  else
  {
    Serial.print("FAIL ");
    Serial.println(name);
    ++failCount;
  }
}

static uint16_t le16(const uint8_t *data)
{
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

// One HID function is 41 bytes: 9 configuration + 9 interface + 9 HID + 7 OUT
// endpoint + 7 IN endpoint.
static constexpr uint16_t HID_EP_OUT_OFFSET = 27;
static constexpr uint16_t HID_EP_IN_OFFSET = 34;
// One bulk vendor function is 32 bytes: 9 interface + 7 + 7, after the 9-byte
// configuration header.
static constexpr uint16_t VENDOR_EP_OUT_OFFSET = 18;
static constexpr uint16_t VENDOR_EP_IN_OFFSET = 25;

// The P4 compiles a 512-byte HID endpoint buffer, so a report may be 511 bytes.
// That is 8,000 reports/s x 511 bytes at high speed - about 4 MB/s through a
// class no host needs a driver for.
static void testHidVendorReachesHighSpeedPacketSize()
{
  check(EspUsbDeviceHidVendor::maxReportSize() == 511, "hid_vendor_max_report_size");

  EspUsbDevice device;
  EspUsbDeviceHidVendor hidVendor(device, 511);
  EspUsbDeviceConfig config;
  config.pid = 0x4030;
  config.startTinyUsb = false;
  check(device.begin(config), "hid_vendor_511_begin");

  // Report Count above 255 needs the two-byte item form, so the descriptor is
  // one byte longer than the 31 a one-byte count produces.
  const uint8_t *report = device.hidReportDescriptor(0);
  check(report != nullptr && hidVendor.hidReportDescriptorLength() == 32,
        "hid_vendor_511_report_descriptor_len");
  check(report && report[16] == 0x96 && report[17] == 0xff && report[18] == 0x01,
        "hid_vendor_511_report_count");

  // Full speed caps an interrupt endpoint at 64 whatever the report size is.
  const uint8_t *fullSpeed = device.configurationDescriptor(0);
  check(le16(&fullSpeed[HID_EP_OUT_OFFSET + 4]) == 64 &&
            le16(&fullSpeed[HID_EP_IN_OFFSET + 4]) == 64,
        "hid_vendor_511_fs_mps");
  check(fullSpeed[HID_EP_OUT_OFFSET + 3] == 0x03 &&
            fullSpeed[HID_EP_IN_OFFSET + 3] == 0x03,
        "hid_vendor_511_interrupt");

  // High speed carries the whole report in one packet.
  const uint8_t *highSpeed = device.configurationDescriptorForSpeed(0, true);
  check(le16(&highSpeed[HID_EP_OUT_OFFSET + 4]) == 512 &&
            le16(&highSpeed[HID_EP_IN_OFFSET + 4]) == 512,
        "hid_vendor_511_hs_mps");
  check(hidVendor.hidHighSpeedEndpointSize() == 512, "hid_vendor_511_hs_hint");
}

// A report that already fits one full-speed packet must not be inflated at high
// speed: the endpoint reserves bus bandwidth, and a keyboard asking for 512
// bytes every microframe would take it from everything else on the bus.
static void testSmallReportsKeepTheirPacketSize()
{
  EspUsbDevice device;
  EspUsbDeviceHidVendor hidVendor(device, 63);
  EspUsbDeviceConfig config;
  config.pid = 0x4031;
  config.startTinyUsb = false;
  check(device.begin(config), "hid_vendor_63_begin");
  check(hidVendor.hidHighSpeedEndpointSize() == 0, "hid_vendor_63_no_hs_hint");

  const uint8_t *fullSpeed = device.configurationDescriptor(0);
  const uint8_t *highSpeed = device.configurationDescriptorForSpeed(0, true);
  check(le16(&fullSpeed[HID_EP_IN_OFFSET + 4]) == 64, "hid_vendor_63_fs_mps");
  check(le16(&highSpeed[HID_EP_IN_OFFSET + 4]) == 64, "hid_vendor_63_hs_mps");

  EspUsbDevice keyboardDevice;
  EspUsbDeviceHidKeyboard keyboard(keyboardDevice);
  EspUsbDeviceConfig keyboardConfig;
  keyboardConfig.pid = 0x4032;
  keyboardConfig.startTinyUsb = false;
  check(keyboardDevice.begin(keyboardConfig), "keyboard_begin");
  const uint8_t *keyboardHs =
      keyboardDevice.configurationDescriptorForSpeed(0, true);
  check(le16(&keyboardHs[HID_EP_IN_OFFSET + 4]) == 8, "keyboard_hs_mps_unchanged");
}

// The full-speed configuration is also what OTHER_SPEED_CONFIGURATION returns
// while the device runs at high speed, which is where a bulk endpoint claiming
// 512 was visible to a host.
static void testVendorBulkPerSpeed()
{
  EspUsbDevice device;
  EspUsbDeviceVendor vendor(device, 512);
  EspUsbDeviceConfig config;
  config.pid = 0x4033;
  config.controller = EspUsbController::HighSpeed;
  config.startTinyUsb = false;
  check(device.begin(config), "vendor_512_begin");

  const uint8_t *fullSpeed = device.configurationDescriptor(0);
  check(le16(&fullSpeed[VENDOR_EP_OUT_OFFSET + 4]) == 64 &&
            le16(&fullSpeed[VENDOR_EP_IN_OFFSET + 4]) == 64,
        "vendor_512_fs_mps");

  const uint8_t *highSpeed = device.configurationDescriptorForSpeed(0, true);
  check(le16(&highSpeed[VENDOR_EP_OUT_OFFSET + 4]) == 512 &&
            le16(&highSpeed[VENDOR_EP_IN_OFFSET + 4]) == 512,
        "vendor_512_hs_mps");

  const uint8_t *otherSpeed = device.otherSpeedConfigurationDescriptor(0, true);
  check(otherSpeed != nullptr && otherSpeed[1] == 0x07,
        "vendor_512_other_speed_type");
  check(otherSpeed && le16(&otherSpeed[VENDOR_EP_OUT_OFFSET + 4]) == 64 &&
            le16(&otherSpeed[VENDOR_EP_IN_OFFSET + 4]) == 64,
        "vendor_512_other_speed_mps");
}

// The transmit FIFO the P4 compiles, and the wait that replaces spinning on
// write() returning 0.
static void testVendorTransmitFifo()
{
  EspUsbDevice device;
  EspUsbDeviceVendor vendor(device, 512);
  EspUsbDeviceConfig config;
  config.pid = 0x4034;
  config.startTinyUsb = false;
  check(device.begin(config), "vendor_fifo_begin");

  check(EspUsbDeviceVendor::writeCapacity() == 4096, "vendor_write_capacity");
  // Nothing is mounted, so there is no room and nothing that will ever free any.
  check(vendor.writeAvailable() == 0, "vendor_write_available_unmounted");
  check(!vendor.waitWritable(512, 20), "vendor_wait_writable_unmounted");
  // Asking for nothing is satisfied without a bus.
  check(vendor.waitWritable(0, 0), "vendor_wait_writable_zero");
}

// The observation hook has to see requests the library answers itself, which is
// the whole reason it exists. Without a bus there are no requests, so what is
// checked here is that installing and clearing it is safe and that a device
// still builds its descriptors with one attached.
static void testControlRequestObserverInstalls()
{
  EspUsbDevice device;
  EspUsbDeviceVendor vendor(device);
  EspUsbDeviceConfig config;
  config.pid = 0x4035;
  config.startTinyUsb = false;

  check(!device.hasControlObserver(), "control_observer_absent");
  static uint32_t seen = 0;
  device.onAnyControlRequest([](const EspUsbDeviceControlRequestInfo &) { ++seen; });
  check(device.hasControlObserver(), "control_observer_installed");
  check(device.begin(config), "control_observer_begin");
  check(device.configurationDescriptor(0) != nullptr, "control_observer_descriptor");
  device.onAnyControlRequest(nullptr);
  check(!device.hasControlObserver(), "control_observer_cleared");
}

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("TEST_BEGIN p4_hs_packet_sizes");
  testHidVendorReachesHighSpeedPacketSize();
  testSmallReportsKeepTheirPacketSize();
  testVendorBulkPerSpeed();
  testVendorTransmitFifo();
  testControlRequestObserverInstalls();
  Serial.printf("TEST_END pass=%d fail=%d\n", passCount, failCount);
  Serial.println(failCount == 0 ? "OK" : "NG");
}

void loop()
{
  delay(1000);
}
