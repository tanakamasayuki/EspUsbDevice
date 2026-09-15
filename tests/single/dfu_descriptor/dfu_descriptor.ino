// The DFU function's descriptor bytes, its endpoint cost, and the OTA target it
// would write into - checked on the chip, against the real partition table.
//
// EspUsbDeviceConfig::startTinyUsb stays false throughout: nothing here needs
// the PHY, and leaving it alone keeps the USB Serial/JTAG log alive.
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

static uint16_t le16(const uint8_t *data)
{
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

// Total endpoint descriptors in a configuration. DFU must add none: that is the
// property that lets it be attached to a device whose endpoint budget is spent.
static int countEndpoints(const uint8_t *cfg)
{
  const uint16_t total = le16(&cfg[2]);
  int endpoints = 0;
  uint16_t offset = 0;
  while (offset + 2 <= total && cfg[offset] > 0)
  {
    if (cfg[offset + 1] == 0x05)
    {
      endpoints++;
    }
    offset = static_cast<uint16_t>(offset + cfg[offset]);
  }
  return endpoints;
}

static void checkFunctional(const uint8_t *fn, const char *prefix)
{
  char name[64];
  snprintf(name, sizeof(name), "%s_functional_header", prefix);
  check(fn[0] == 9 && fn[1] == 0x21, name);
  snprintf(name, sizeof(name), "%s_functional_timeout", prefix);
  check(le16(&fn[3]) == 1000, name);
  snprintf(name, sizeof(name), "%s_functional_transfer_size", prefix);
  check(le16(&fn[5]) == EspUsbDeviceDfu::transferSize(), name);
  snprintf(name, sizeof(name), "%s_functional_bcd_dfu", prefix);
  check(le16(&fn[7]) == 0x0110, name);
}

static void testDownloadDescriptor()
{
  EspUsbDevice device;
  EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Download);
  EspUsbDeviceConfig config;
  config.pid = 0x4070;
  config.startTinyUsb = false;

  check(device.begin(config), "download_begin");

  const uint8_t *cfg = device.configurationDescriptor(0);
  check(le16(&cfg[2]) == 9 + 18, "download_config_total_len");
  check(cfg[4] == 1, "download_interface_count");
  check(countEndpoints(cfg) == 0, "download_no_endpoints");

  const uint8_t *itf = &cfg[9];
  check(itf[0] == 9 && itf[1] == 0x04, "download_interface_header");
  check(itf[2] == 0 && itf[3] == 0, "download_interface_number_alt");
  check(itf[4] == 0, "download_interface_no_endpoints");
  check(itf[5] == 0xfe && itf[6] == 0x01, "download_interface_class");
  check(itf[7] == 0x02, "download_interface_protocol_dfu");

  const uint8_t *fn = &cfg[9 + 9];
  // Download mode must not claim manifestation tolerance: the device restarts
  // into the new image, so the host has to expect it to disappear.
  check((fn[2] & 0x01) != 0, "download_attr_can_download");
  check((fn[2] & 0x04) == 0, "download_attr_not_manifestation_tolerant");
  checkFunctional(fn, "download");

  check(dfu.interfaceCount() == 1 && dfu.endpointCount() == 0, "download_class_costs");
  check(dfu.mode() == EspUsbDeviceDfuMode::Download, "download_mode");
}

static void testRuntimeDescriptor()
{
  EspUsbDevice device;
  EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Runtime);
  EspUsbDeviceConfig config;
  config.pid = 0x4071;
  config.startTinyUsb = false;

  check(device.begin(config), "runtime_begin");

  const uint8_t *cfg = device.configurationDescriptor(0);
  check(le16(&cfg[2]) == 9 + 18, "runtime_config_total_len");
  check(countEndpoints(cfg) == 0, "runtime_no_endpoints");

  const uint8_t *itf = &cfg[9];
  check(itf[5] == 0xfe && itf[6] == 0x01, "runtime_interface_class");
  check(itf[7] == 0x01, "runtime_interface_protocol_rt");

  const uint8_t *fn = &cfg[9 + 9];
  // bitWillDetach: this device restarts itself, so the host must not issue the
  // USB reset the standard otherwise expects from it.
  check((fn[2] & 0x08) != 0, "runtime_attr_will_detach");
  checkFunctional(fn, "runtime");
}

// A DFU function next to a full HID + MSC device: the interface count goes up
// by one, the endpoint count does not move, and the HID function keeps the
// interface number it had.
static void testCompositeCost()
{
  static uint8_t disk[24 * 512];

  uint16_t bareLength = 0;
  uint8_t bareInterfaces = 0;
  int bareEndpoints = 0;

  // The MSC class is a singleton on the device side (one TinyUSB instance, one
  // set of tud_msc_* callbacks), so the reference device has to be gone before
  // the second one is built - otherwise the second begin() fails for a reason
  // that has nothing to do with DFU.
  {
    EspUsbDevice bare;
    EspUsbDeviceHidKeyboard bareKeyboard(bare);
    EspUsbDeviceMsc bareMsc(bare);
    EspUsbDeviceMscFatRamDisk bareDisk(disk, sizeof(disk));
    EspUsbDeviceConfig bareConfig;
    bareConfig.pid = 0x4072;
    bareConfig.startTinyUsb = false;
    check(bareDisk.format("ESPUSB"), "composite_format");
    check(bareDisk.attach(bareMsc), "composite_attach");
    check(bare.begin(bareConfig), "composite_bare_begin");
    const uint8_t *bareCfg = bare.configurationDescriptor(0);
    bareLength = le16(&bareCfg[2]);
    bareInterfaces = bareCfg[4];
    bareEndpoints = countEndpoints(bareCfg);
  }

  {
    EspUsbDevice device;
    EspUsbDeviceHidKeyboard keyboard(device);
    EspUsbDeviceMsc msc(device);
    EspUsbDeviceMscFatRamDisk ramDisk(disk, sizeof(disk));
    EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Download, "Firmware");
    EspUsbDeviceConfig config;
    config.pid = 0x4073;
    config.startTinyUsb = false;
    check(ramDisk.format("ESPUSB"), "composite_dfu_format");
    check(ramDisk.attach(msc), "composite_dfu_attach");
    check(device.begin(config), "composite_dfu_begin");

    const uint8_t *cfg = device.configurationDescriptor(0);
    check(le16(&cfg[2]) == bareLength + 18, "composite_dfu_total_len");
    check(cfg[4] == bareInterfaces + 1, "composite_dfu_interface_count");
    check(countEndpoints(cfg) == bareEndpoints, "composite_dfu_endpoint_count");
    // HID is emitted first, so its interface number cannot move.
    check(cfg[9 + 2] == 0, "composite_dfu_hid_interface_number");
  }
}

// A named function publishes a string, and the DFU interface points at it.
static void testFunctionString()
{
  EspUsbDevice device;
  EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Download, "Firmware");
  EspUsbDeviceConfig config;
  config.pid = 0x4074;
  config.startTinyUsb = false;
  check(device.begin(config), "string_begin");

  const uint8_t *cfg = device.configurationDescriptor(0);
  const uint8_t stringIndex = cfg[9 + 8];
  check(stringIndex != 0, "string_index_assigned");

  const uint16_t *descriptor = device.stringDescriptor(stringIndex, 0x0409);
  check(descriptor != nullptr, "string_descriptor_present");
  if (descriptor)
  {
    const uint8_t length = static_cast<uint8_t>(descriptor[0] & 0xff);
    check(length == 2 + 2 * 8, "string_descriptor_length");
    check(descriptor[1] == 'F' && descriptor[8] == 'e', "string_descriptor_text");
  }
}

// Two DFU functions on one device cannot work: they would share one class
// driver and one state machine. begin() has to say so.
static void testSecondFunctionRejected()
{
  EspUsbDevice device;
  EspUsbDeviceDfu first(device, EspUsbDeviceDfuMode::Download);
  EspUsbDeviceDfu second(device, EspUsbDeviceDfuMode::Download);
  EspUsbDeviceConfig config;
  config.pid = 0x4075;
  config.startTinyUsb = false;
  check(!device.begin(config), "second_dfu_rejected");
}

// The partition table this firmware is running under, as the update path sees
// it. A single-app scheme would make every route in docs/ota-over-usb.md
// impossible, so the test states the requirement rather than assuming it.
static void testFirmwareTarget()
{
  check(EspUsbDeviceFirmwareUpdate::available(), "ota_target_available");
  const char *label = EspUsbDeviceFirmwareUpdate::targetLabel();
  check(label != nullptr, "ota_target_label");
  check(EspUsbDeviceFirmwareUpdate::capacity() > 0, "ota_target_capacity");
  if (label)
  {
    Serial.print("OTA_TARGET ");
    Serial.print(label);
    Serial.print(' ');
    Serial.println(static_cast<unsigned long>(EspUsbDeviceFirmwareUpdate::capacity()));
  }

  // Nothing is open, so every operation on a closed update has to refuse
  // rather than reach flash.
  EspUsbDeviceFirmwareUpdate update;
  check(!update.active(), "update_starts_closed");
  check(!update.write("x", 1), "closed_update_refuses_write");
  check(!update.end(), "closed_update_refuses_end");
  check(update.lastError() == ESP_ERR_INVALID_STATE, "closed_update_error");

  // An image larger than the partition is refused at begin(), before any flash
  // is touched.
  check(!update.begin(EspUsbDeviceFirmwareUpdate::capacity() + 1), "oversized_refused");
  check(update.lastError() == ESP_ERR_INVALID_SIZE, "oversized_error");
}

void setup()
{
  Serial.begin(115200);
  delay(2000);
  Serial.println("TEST_BEGIN dfu_descriptor");

  testDownloadDescriptor();
  testRuntimeDescriptor();
  testCompositeCost();
  testFunctionString();
  testSecondFunctionRejected();
  testFirmwareTarget();

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
