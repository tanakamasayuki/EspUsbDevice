// Which bulk IN endpoints get a two-packet transmit FIFO, and why.
//
// The decision is arithmetic over the controller's FIFO, and getting it wrong
// does not slow a device down - it stops an endpoint opening, which is a device
// that fails to enumerate. So this checks the answer against the descriptor it
// was computed from: every bit set must belong to a bulk IN endpoint, and no
// bulk IN endpoint may be missing when the budget allowed them all.
//
// Nothing here starts the PHY, so the serial log stays alive.
#include "EspUsbDevice.h"

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

// The bulk IN endpoint numbers a configuration descriptor actually declares.
// Computed here from the bytes rather than from the library's own bookkeeping,
// so the two have to agree independently.
static uint16_t bulkInMaskOf(const uint8_t *descriptor)
{
  const uint16_t total = static_cast<uint16_t>(descriptor[2] | (descriptor[3] << 8));
  uint16_t mask = 0;
  uint16_t offset = 0;
  while (offset + 2 <= total && descriptor[offset] > 0)
  {
    if (descriptor[offset + 1] == 0x05 && descriptor[offset] >= 7)
    {
      const uint8_t address = descriptor[offset + 2];
      const uint8_t type = static_cast<uint8_t>(descriptor[offset + 3] & 0x03);
      if ((address & 0x80) && type == 0x02)
      {
        mask = static_cast<uint16_t>(mask | (1u << (address & 0x0f)));
      }
    }
    offset = static_cast<uint16_t>(offset + descriptor[offset]);
  }
  return mask;
}

static uint8_t bitCount(uint16_t value)
{
  uint8_t count = 0;
  while (value)
  {
    count = static_cast<uint8_t>(count + (value & 1));
    value = static_cast<uint16_t>(value >> 1);
  }
  return count;
}

// A device with no bulk IN endpoint has nothing to double, and must say so
// rather than setting a bit the controller would apply to an interrupt
// endpoint.
static void testNoBulkIn()
{
  EspUsbDevice device;
  EspUsbDeviceHidKeyboard keyboard(device);
  EspUsbDeviceConfig config;
  config.pid = 0x4080;
  config.startTinyUsb = false;
  check(device.begin(config), "keyboard_begin");
  check(device.bulkInDoubleBuffered() == 0, "keyboard_no_double_buffer");
  check(bulkInMaskOf(device.configurationDescriptor(0)) == 0, "keyboard_has_no_bulk_in");
}

// One bulk IN endpoint fits on every controller this library supports, so this
// is the case that must never come back empty.
static void testSingleBulkIn()
{
  EspUsbDevice device;
  EspUsbDeviceVendor vendor(device);
  EspUsbDeviceConfig config;
  config.pid = 0x4081;
  config.startTinyUsb = false;
  check(device.begin(config), "vendor_begin");

  const uint16_t declared = bulkInMaskOf(device.configurationDescriptor(0));
  check(bitCount(declared) == 1, "vendor_one_bulk_in");
  check(device.bulkInDoubleBuffered() == declared, "vendor_doubled");
}

// Every bit the library sets has to be a bulk IN endpoint of this very
// configuration. A bit outside that set would be applied by the controller to
// an endpoint that never asked for it.
static void testCompositeSubset()
{
  static uint8_t disk[24 * 512];

  EspUsbDevice device;
  EspUsbDeviceCdcSerial serial(device, "Console");
  EspUsbDeviceMsc msc(device);
  EspUsbDeviceMscFatRamDisk ramDisk(disk, sizeof(disk));
  EspUsbDeviceVendor vendor(device);
  EspUsbDeviceConfig config;
  config.pid = 0x4082;
  config.startTinyUsb = false;
  check(ramDisk.format("ESPUSB"), "composite_format");
  check(ramDisk.attach(msc), "composite_attach");
  check(device.begin(config), "composite_begin");

  const uint16_t declared = bulkInMaskOf(device.configurationDescriptor(0));
  const uint16_t applied = device.bulkInDoubleBuffered();
  check(bitCount(declared) == 3, "composite_three_bulk_in");
  check((applied & ~declared) == 0, "composite_only_bulk_in_bits");
  // All or nothing: a rule that doubled some and not others would make
  // throughput depend on registration order.
  check(applied == 0 || applied == declared, "composite_all_or_nothing");
}

// Single is the controller's own behaviour, and has to be reachable without
// arguing with the budget.
static void testSingleRequested()
{
  EspUsbDevice device;
  EspUsbDeviceVendor vendor(device);
  EspUsbDeviceConfig config;
  config.pid = 0x4083;
  config.startTinyUsb = false;
  config.bulkInBuffering = EspUsbBulkInBuffering::Single;
  check(device.begin(config), "single_begin");
  check(device.bulkInDoubleBuffered() == 0, "single_no_double_buffer");
}

// Double is a demand rather than a preference: it either applies to every bulk
// IN endpoint or begin() refuses, because the alternative is an endpoint that
// silently does not open.
static void testDoubleRequested()
{
  EspUsbDevice device;
  EspUsbDeviceVendor vendor(device);
  EspUsbDeviceConfig config;
  config.pid = 0x4084;
  config.startTinyUsb = false;
  config.bulkInBuffering = EspUsbBulkInBuffering::Double;
  const bool started = device.begin(config);
  if (started)
  {
    check(device.bulkInDoubleBuffered() == bulkInMaskOf(device.configurationDescriptor(0)),
          "double_applied_to_all");
  }
  else
  {
    check(device.lastError() == ESP_ERR_INVALID_SIZE, "double_refused_with_size_error");
  }
  // One bulk IN endpoint fits everywhere, so the refusal branch must not be
  // the one taken here.
  check(started, "double_fits_for_one_endpoint");
}

void setup()
{
  Serial.begin(115200);
  delay(2000);
  Serial.println("TEST_BEGIN bulk_in_fifo");

  testNoBulkIn();
  testSingleBulkIn();
  testCompositeSubset();
  testSingleRequested();
  testDoubleRequested();

  Serial.print("PASS ");
  Serial.print(passCount);
  Serial.print(" FAIL ");
  Serial.println(failCount);
  Serial.println("TEST_END");
  Serial.println(failCount == 0 ? "OK" : "NG");
}

void loop()
{
  delay(1000);
}
