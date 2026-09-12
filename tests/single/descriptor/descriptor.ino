#include "EspUsbDevice.h"
#include <string.h>

struct HidWalk
{
  bool wellFormed = false;
  // Collection (Application) only. A mouse nests a Physical collection inside
  // its Application one, so counting every Collection item counts functions
  // wrong.
  uint8_t collections = 0;
  uint8_t reportIds[8] = {};
  uint8_t reportIdCount = 0;
  // Report IDs seen before any Collection item, which would apply to nothing.
  uint8_t strayReportIds = 0;
};


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

// Length of one HID item, or 0 if it is malformed or runs past the end. Mirrors
// the walker the library uses to merge descriptors, so a merged descriptor that
// only *looks* right cannot pass this.
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

static HidWalk walkHidReportDescriptor(const uint8_t *descriptor, uint16_t length)
{
  HidWalk walk;
  if (!descriptor || length == 0)
  {
    return walk;
  }
  uint16_t offset = 0;
  while (offset < length)
  {
    const uint16_t itemLength = hidItemLen(&descriptor[offset], static_cast<uint16_t>(length - offset));
    if (itemLength == 0)
    {
      return walk; // wellFormed stays false
    }
    const uint8_t prefix = descriptor[offset];
    if ((prefix & 0xfc) == 0xa0 && itemLength >= 2 && descriptor[offset + 1] == 0x01)
    {
      walk.collections++;
    }
    else if ((prefix & 0xfc) == 0x84)
    {
      if (walk.collections == 0)
      {
        walk.strayReportIds++;
      }
      if (walk.reportIdCount < 8 && itemLength >= 2)
      {
        walk.reportIds[walk.reportIdCount++] = descriptor[offset + 1];
      }
    }
    offset = static_cast<uint16_t>(offset + itemLength);
  }
  walk.wellFormed = (offset == length);
  return walk;
}


static void testKeyboardDescriptor()
{
  EspUsbDevice device;
  EspUsbDeviceHidKeyboard keyboard(device);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4001;
  config.manufacturer = "EspUsb";
  config.product = "Keyboard";
  config.serialNumber = "kbd-1";
  config.maxPowerMilliamps = 100;
  config.startTinyUsb = false;

  check(device.begin(config), "keyboard_begin");

  const uint8_t *dev = device.deviceDescriptor();
  check(dev[0] == 18 && dev[1] == 0x01, "device_descriptor_header");
  check(le16(&dev[2]) == 0x0200, "device_usb_version");
  check(dev[7] == 64, "device_ep0_mps");
  check(le16(&dev[8]) == 0x303a && le16(&dev[10]) == 0x4001, "device_vid_pid");
  check(dev[14] == 1 && dev[15] == 2 && dev[16] == 3, "device_string_indexes");

  const uint8_t *cfg = device.configurationDescriptor(0);
  check(cfg[0] == 9 && cfg[1] == 0x02, "config_descriptor_header");
  check(le16(&cfg[2]) == 41, "keyboard_config_total_len");
  check(cfg[4] == 1, "keyboard_interface_count");
  check(cfg[7] == 0x80 && cfg[8] == 50, "keyboard_power");

  const uint8_t *itf = &cfg[9];
  check(itf[0] == 9 && itf[1] == 0x04 && itf[2] == 0 && itf[4] == 2, "keyboard_interface");
  check(itf[5] == 0x03 && itf[6] == 0x01 && itf[7] == 0x01, "keyboard_boot_protocol");

  const uint8_t *hid = &cfg[18];
  check(hid[0] == 9 && hid[1] == 0x21 && le16(&hid[7]) == keyboard.hidReportDescriptorLength(), "keyboard_hid_descriptor");

  const uint8_t *epOut = &cfg[27];
  const uint8_t *epIn = &cfg[34];
  check(epOut[0] == 7 && epOut[1] == 0x05 && epOut[2] == 0x01, "keyboard_ep_out_addr");
  check(epIn[0] == 7 && epIn[1] == 0x05 && epIn[2] == 0x81, "keyboard_ep_in_addr");
  check(le16(&epOut[4]) == 8 && le16(&epIn[4]) == 8, "keyboard_ep_mps");

  check(device.hidReportDescriptor(0) == keyboard.hidReportDescriptor(), "keyboard_report_descriptor_ptr");
  check(keyboard.hidReportDescriptorLength() > 50, "keyboard_report_descriptor_len");
}

static void testMouseDescriptor()
{
  EspUsbDevice device;
  EspUsbDeviceHidMouse mouse(device);
  EspUsbDeviceConfig config;
  config.pid = 0x4002;
  config.selfPowered = true;
  config.maxPowerMilliamps = 2;
  config.startTinyUsb = false;

  check(device.begin(config), "mouse_begin");

  const uint8_t *cfg = device.configurationDescriptor(0);
  check(le16(&cfg[2]) == 34, "mouse_config_total_len");
  check(cfg[4] == 1, "mouse_interface_count");
  check(cfg[7] == 0xc0 && cfg[8] == 1, "mouse_power");

  const uint8_t *itf = &cfg[9];
  check(itf[4] == 1 && itf[5] == 0x03 && itf[7] == 0x02, "mouse_boot_protocol");
  const uint8_t *epIn = &cfg[27];
  check(epIn[2] == 0x81 && le16(&epIn[4]) == 8, "mouse_ep_in");
  check(mouse.hidReportDescriptorLength() > 40, "mouse_report_descriptor_len");
}

static void testCompositeDescriptor()
{
  EspUsbDevice device;
  EspUsbDeviceHidKeyboard keyboard(device);
  EspUsbDeviceHidMouse mouse(device);
  EspUsbDeviceConfig config;
  config.pid = 0x4003;
  config.startTinyUsb = false;

  check(device.begin(config), "composite_begin");

  const uint8_t *cfg = device.configurationDescriptor(0);
  check(le16(&cfg[2]) == 41, "composite_config_total_len");
  check(cfg[4] == 1, "composite_interface_count");
  check(cfg[9 + 2] == 0, "composite_interface_number");
  check(cfg[9 + 5] == 0x03 && cfg[9 + 6] == 0x00 && cfg[9 + 7] == 0x00, "composite_hid_no_boot_protocol");
  check(cfg[27 + 2] == 0x01 && cfg[34 + 2] == 0x81, "composite_eps");
  check(le16(&cfg[27 + 4]) == 16 && le16(&cfg[34 + 4]) == 16, "composite_ep_mps");

  const uint8_t *report = device.hidReportDescriptor(0);
  check(report != nullptr, "composite_report_descriptor_ptr");
  check(report[6] == 0x85 && report[7] == 0x01, "composite_keyboard_report_id");
  bool foundMouseReportId = false;
  for (uint16_t i = 0; i + 1 < le16(&cfg[18 + 7]); i++)
  {
    if (report[i] == 0x85 && report[i + 1] == 0x02)
    {
      foundMouseReportId = true;
    }
  }
  check(foundMouseReportId, "composite_mouse_report_id");
  check(device.hidReportDescriptor(1) == nullptr, "composite_single_runtime_hid_instance");
}

static void testVendorDescriptor()
{
  EspUsbDevice device;
  EspUsbDeviceVendor vendor(device);
  EspUsbDeviceConfig config;
  config.pid = 0x4019;
  config.startTinyUsb = false;

  check(device.begin(config), "vendor_begin");

  const uint8_t *cfg = device.configurationDescriptor(0);
  check(le16(&cfg[2]) == 32, "vendor_config_total_len");
  check(cfg[4] == 1, "vendor_interface_count");

  const uint8_t *itf = &cfg[9];
  check(itf[0] == 9 && itf[1] == 0x04 && itf[2] == 0 && itf[4] == 2, "vendor_interface");
  check(itf[5] == 0xff && itf[6] == 0x00 && itf[7] == 0x00, "vendor_class");

  const uint8_t *epOut = &cfg[18];
  const uint8_t *epIn = &cfg[25];
  check(epOut[0] == 7 && epOut[1] == 0x05 && epOut[2] == 0x01, "vendor_ep_out_addr");
  check(epIn[0] == 7 && epIn[1] == 0x05 && epIn[2] == 0x81, "vendor_ep_in_addr");
  check(epOut[3] == 0x02 && epIn[3] == 0x02, "vendor_ep_bulk");
  check(le16(&epOut[4]) == 64 && le16(&epIn[4]) == 64, "vendor_ep_mps");

  const uint8_t *highSpeed =
      device.configurationDescriptorForSpeed(0, true);
  check(le16(&highSpeed[18 + 4]) == 512 &&
            le16(&highSpeed[25 + 4]) == 512,
        "vendor_hs_ep_mps");
}

// A bulk endpoint may not exceed 64 bytes at full speed (USB 2.0 table 9-13),
// whatever the sketch asked for. The high-speed configuration is where 512
// belongs, and the two are separate descriptors.
static void testVendorPerSpeedEndpointSize()
{
  EspUsbDevice device;
  EspUsbDeviceVendor vendor(device, 512);
  EspUsbDeviceConfig config;
  config.pid = 0x401f;
  config.startTinyUsb = false;

  check(device.begin(config), "vendor_hs_size_begin");
  check(vendor.endpointSize() == 512, "vendor_hs_size_reported");

  const uint8_t *cfg = device.configurationDescriptor(0);
  check(le16(&cfg[18 + 4]) == 64 && le16(&cfg[25 + 4]) == 64,
        "vendor_fs_ep_mps_clamped");

  const uint8_t *highSpeed = device.configurationDescriptorForSpeed(0, true);
  check(le16(&highSpeed[18 + 4]) == 512 && le16(&highSpeed[25 + 4]) == 512,
        "vendor_hs_ep_mps_512");
}

// A sketch that asks for less than the speed allows still gets what it asked
// for at full speed - the per-speed value is a ceiling, not a replacement.
//
// Its own function because only one EspUsbDeviceVendor may be registered at a
// time: a second one begins false while the first is still alive.
static void testVendorSmallEndpointSize()
{
  EspUsbDevice device;
  EspUsbDeviceVendor vendor(device, 32);
  EspUsbDeviceConfig config;
  config.pid = 0x4020;
  config.startTinyUsb = false;
  check(device.begin(config), "vendor_small_begin");
  const uint8_t *cfg = device.configurationDescriptor(0);
  check(le16(&cfg[18 + 4]) == 32 && le16(&cfg[25 + 4]) == 32,
        "vendor_fs_ep_mps_small");
}

// The report descriptor has to declare the report size the endpoint actually
// carries: a host sizes its reads from Report Count, so a descriptor frozen at
// 63 while the endpoint carries more makes the host read the wrong number of
// bytes.
static void testHidVendorReportDescriptor()
{
  EspUsbDevice device;
  EspUsbDeviceHidVendor hidVendor(device, 32);
  EspUsbDeviceConfig config;
  config.pid = 0x4021;
  config.startTinyUsb = false;

  check(device.begin(config), "hid_vendor_begin");
  check(hidVendor.reportSize() == 32, "hid_vendor_report_size");

  const uint8_t *report = device.hidReportDescriptor(0);
  const uint16_t reportLength = 31; // one-byte Report Count
  check(report != nullptr, "hid_vendor_report_descriptor");
  check(report && report[0] == 0x06 && report[1] == 0x00 && report[2] == 0xff,
        "hid_vendor_usage_page");
  check(report && report[7] == 0x85 &&
            report[8] == ESP_USB_DEVICE_HID_REPORT_ID_VENDOR,
        "hid_vendor_report_id");
  // Report Size (8 bits) then Report Count, which must be reportSize().
  check(report && report[14] == 0x75 && report[15] == 0x08,
        "hid_vendor_report_bits");
  check(report && report[16] == 0x95 && report[17] == 32,
        "hid_vendor_report_count");
  check(report && report[reportLength - 1] == 0xc0, "hid_vendor_end_collection");

  // The ceiling follows CFG_TUD_HID_EP_BUFSIZE rather than a hard-coded 63,
  // because the report ID takes the first byte of the endpoint buffer.
  EspUsbDevice tooBig;
  EspUsbDeviceHidVendor oversize(
      tooBig, static_cast<uint16_t>(EspUsbDeviceHidVendor::maxReportSize() + 1));
  EspUsbDeviceConfig tooBigConfig;
  tooBigConfig.pid = 0x4022;
  tooBigConfig.startTinyUsb = false;
  check(!tooBig.begin(tooBigConfig), "hid_vendor_oversize_rejected");

  EspUsbDevice atLimit;
  EspUsbDeviceHidVendor limitVendor(atLimit,
                                    EspUsbDeviceHidVendor::maxReportSize());
  EspUsbDeviceConfig limitConfig;
  limitConfig.pid = 0x4023;
  limitConfig.startTinyUsb = false;
  check(atLimit.begin(limitConfig), "hid_vendor_limit_accepted");
}

// Merging two HID classes means giving each its own Report ID, immediately
// after that class's Collection (Application) item. Finding that point by
// counting six bytes works only for a descriptor that opens with a one-byte
// Usage Page; EspUsbDeviceHidVendor opens with a vendor-defined one, which is a
// three-byte item, so the cut landed inside the Collection item and everything
// after it shifted by one.
static void testCompositeHidReportDescriptorMerge()
{
  {
    EspUsbDevice device;
    EspUsbDeviceHidKeyboard keyboard(device);
    EspUsbDeviceHidVendor hidVendor(device, 32);
    EspUsbDeviceConfig config;
    config.pid = 0x4028;
    config.startTinyUsb = false;
    check(device.begin(config), "merge_vendor_begin");

    const uint8_t *merged = device.hidReportDescriptor(0);
    const HidWalk walk = walkHidReportDescriptor(merged, device.hidReportDescriptorLength(0));
    check(walk.wellFormed, "merge_vendor_well_formed");
    check(walk.collections == 2, "merge_vendor_two_collections");
    check(walk.strayReportIds == 0, "merge_vendor_no_stray_report_id");
    check(walk.reportIdCount == 2, "merge_vendor_two_report_ids");
    check(walk.reportIdCount == 2 &&
              walk.reportIds[0] == ESP_USB_DEVICE_HID_REPORT_ID_KEYBOARD &&
              walk.reportIds[1] == ESP_USB_DEVICE_HID_REPORT_ID_VENDOR,
          "merge_vendor_report_id_values");
    // The vendor function's three-byte usage page must survive intact.
    check(merged && device.hidReportDescriptorLength(0) > 40, "merge_vendor_length");
  }

  // A class that already declares a Report ID has it replaced, not duplicated.
  {
    EspUsbDevice device;
    EspUsbDeviceHidKeyboard keyboard(device);
    EspUsbDeviceHidConsumerControl consumer(device);
    EspUsbDeviceConfig config;
    config.pid = 0x4029;
    config.startTinyUsb = false;
    check(device.begin(config), "merge_consumer_begin");

    const HidWalk walk = walkHidReportDescriptor(
        device.hidReportDescriptor(0), device.hidReportDescriptorLength(0));
    check(walk.wellFormed, "merge_consumer_well_formed");
    check(walk.collections == 2, "merge_consumer_two_collections");
    check(walk.reportIdCount == 2, "merge_consumer_no_duplicate_report_id");
    check(walk.reportIdCount == 2 &&
              walk.reportIds[0] == ESP_USB_DEVICE_HID_REPORT_ID_KEYBOARD &&
              walk.reportIds[1] == ESP_USB_DEVICE_HID_REPORT_ID_CONSUMER_CONTROL,
          "merge_consumer_report_id_values");
  }

  // Three classes, one of which opens with the three-byte usage page.
  {
    EspUsbDevice device;
    EspUsbDeviceHidKeyboard keyboard(device);
    EspUsbDeviceHidMouse mouse(device);
    EspUsbDeviceHidVendor hidVendor(device, 16);
    EspUsbDeviceConfig config;
    config.pid = 0x402a;
    config.startTinyUsb = false;
    check(device.begin(config), "merge_three_begin");

    const HidWalk walk = walkHidReportDescriptor(
        device.hidReportDescriptor(0), device.hidReportDescriptorLength(0));
    check(walk.wellFormed, "merge_three_well_formed");
    check(walk.collections == 3, "merge_three_collections");
    check(walk.reportIdCount == 3 &&
              walk.reportIds[0] == ESP_USB_DEVICE_HID_REPORT_ID_KEYBOARD &&
              walk.reportIds[1] == ESP_USB_DEVICE_HID_REPORT_ID_MOUSE &&
              walk.reportIds[2] == ESP_USB_DEVICE_HID_REPORT_ID_VENDOR,
          "merge_three_report_ids");
  }
}

// A report that arrives on the interrupt OUT endpoint carries no report ID in
// any request field: TinyUSB's HID driver does not parse report descriptors, so
// it passes 0 and leaves the payload alone (hid_device.c). A merged descriptor
// declares report IDs, so the ID is the payload's first byte, and without
// reading it there a composite HID device routes every such report nowhere.
//
// Driven directly rather than through a host, because neither host on this
// bench can send one: usbip does not deliver interrupt OUT at all, and
// EspUsbHost has no raw endpoint write. What the device is responsible for is
// the routing, and that is what this checks.
static void testCompositeOutReportRouting()
{
  EspUsbDevice device;
  EspUsbDeviceHidKeyboard keyboard(device);
  EspUsbDeviceHidVendor hidVendor(device, 15);
  EspUsbDeviceConfig config;
  config.pid = 0x402c;
  config.startTinyUsb = false;
  check(device.begin(config), "out_routing_begin");

  static uint8_t seenReportId = 0;
  static uint16_t seenLength = 0;
  static uint8_t seenFirstByte = 0;
  static uint8_t keyboardReports = 0;
  hidVendor.onOutputReport([](const EspUsbDeviceHidReport &report)
                           {
                             seenReportId = report.reportId;
                             seenLength = report.length;
                             seenFirstByte = report.length > 0 && report.data ? report.data[0] : 0;
                           });
  keyboard.onOutputReport([](const EspUsbDeviceHidKeyboardOutputReport &)
                          { keyboardReports++; });

  // What the endpoint delivers: report ID 6 followed by 15 payload bytes, and a
  // report ID of 0 because the class driver does not know any better.
  uint8_t interruptOut[16] = {ESP_USB_DEVICE_HID_REPORT_ID_VENDOR, 'o', 'u', 't'};
  device.handleHidSetReport(0, 0, ESP_USB_DEVICE_HID_REPORT_TYPE_OUTPUT,
                            interruptOut, sizeof(interruptOut));
  check(seenReportId == ESP_USB_DEVICE_HID_REPORT_ID_VENDOR, "out_routing_report_id");
  check(seenLength == 15, "out_routing_length");
  check(seenFirstByte == 'o', "out_routing_payload");
  check(keyboardReports == 0, "out_routing_not_keyboard");

  // The keyboard's ID goes to the keyboard, from the same shared endpoint.
  seenReportId = 0xff;
  uint8_t ledOut[2] = {ESP_USB_DEVICE_HID_REPORT_ID_KEYBOARD, 0x01};
  device.handleHidSetReport(0, 0, ESP_USB_DEVICE_HID_REPORT_TYPE_OUTPUT,
                            ledOut, sizeof(ledOut));
  check(keyboardReports == 1, "out_routing_keyboard");
  check(seenReportId == 0xff, "out_routing_keyboard_not_vendor");

  // A first byte that names no class is left alone rather than eaten, so a
  // device whose classes declare no IDs keeps its payload intact.
  seenReportId = 0xff;
  seenLength = 0xffff;
  uint8_t strangeOut[4] = {0x7f, 'x', 'y', 'z'};
  device.handleHidSetReport(0, 0, ESP_USB_DEVICE_HID_REPORT_TYPE_OUTPUT,
                            strangeOut, sizeof(strangeOut));
  check(seenReportId == 0xff && seenLength == 0xffff, "out_routing_unknown_id_dropped");
  check(keyboardReports == 1, "out_routing_unknown_id_not_keyboard");

  // Control SET_REPORT still arrives with its ID in hand and must not be
  // re-parsed out of the payload.
  device.handleHidSetReport(0, ESP_USB_DEVICE_HID_REPORT_ID_VENDOR,
                            ESP_USB_DEVICE_HID_REPORT_TYPE_OUTPUT,
                            interruptOut, sizeof(interruptOut));
  check(seenReportId == ESP_USB_DEVICE_HID_REPORT_ID_VENDOR && seenLength == 16,
        "out_routing_control_untouched");
}

static void testCompositeWithVendorDescriptor()
{
  EspUsbDevice device;
  EspUsbDeviceHidKeyboard keyboard(device);
  EspUsbDeviceHidMouse mouse(device);
  EspUsbDeviceVendor vendor(device);
  EspUsbDeviceConfig config;
  config.pid = 0x401a;
  config.startTinyUsb = false;

  check(device.begin(config), "composite_vendor_begin");

  const uint8_t *cfg = device.configurationDescriptor(0);
  check(le16(&cfg[2]) == 64, "composite_vendor_config_total_len");
  check(cfg[4] == 2, "composite_vendor_interface_count");
  check(cfg[9 + 2] == 0, "composite_vendor_hid_interface_number");
  check(cfg[41 + 2] == 1, "composite_vendor_interface_number");
  check(cfg[41 + 5] == 0xff, "composite_vendor_class");
  // HID (keyboard+mouse merged) now uses a single duplex endpoint on EP1, so
  // the following bulk vendor interface advances to EP2 (0x02 OUT / 0x82 IN)
  // instead of EP3. See docs/DESIGN_NOTES.ja.md "複合時の endpoint 採番衝突".
  check(cfg[50 + 2] == 0x02 && cfg[57 + 2] == 0x82, "composite_vendor_eps");
}

static void testWebUsbAndMicrosoftOs20Descriptors()
{
  EspUsbDevice device;
  EspUsbDeviceHidKeyboard keyboard(device);
  EspUsbDeviceVendor vendor(device);
  EspUsbDeviceConfig config;
  config.webusbEnabled = true;
  config.webusbUrl = "https://example.com/espusbdevice";
  config.startTinyUsb = false;

  check(device.begin(config), "webusb_ms_os_begin");
  check(le16(&device.deviceDescriptor()[2]) == 0x0201,
        "webusb_device_usb_version");

  const uint8_t *bos = device.bosDescriptor();
  check(bos != nullptr && device.bosDescriptorLength() == 57,
        "webusb_bos_length");
  check(bos && bos[0] == 5 && bos[1] == 0x0f &&
            le16(&bos[2]) == 57 && bos[4] == 2,
        "webusb_bos_header");
  check(bos && bos[5] == 24 && bos[6] == 0x10 &&
            bos[7] == 0x05 && bos[27] == 0x01 && bos[28] == 0x01,
        "webusb_platform_capability");
  check(bos && bos[29] == 28 && bos[30] == 0x10 &&
            bos[31] == 0x05 && le16(&bos[53]) == 178 &&
            bos[55] == 0x02,
        "ms_os_20_platform_capability");

  const uint8_t *ms = device.microsoftOs20Descriptor();
  check(ms != nullptr && device.microsoftOs20DescriptorLength() == 178,
        "ms_os_20_length");
  check(ms && le16(&ms[0]) == 10 && le16(&ms[2]) == 0 &&
            le16(&ms[8]) == 178,
        "ms_os_20_set_header");
  check(ms && le16(&ms[10]) == 8 && le16(&ms[12]) == 1 &&
            le16(&ms[16]) == 168,
        "ms_os_20_configuration_subset");
  check(ms && le16(&ms[18]) == 8 && le16(&ms[20]) == 2 &&
            ms[22] == 1 && le16(&ms[24]) == 160,
        "ms_os_20_vendor_interface");
  check(ms && le16(&ms[26]) == 20 && le16(&ms[28]) == 3 &&
            memcmp(&ms[30], "WINUSB", 6) == 0,
        "ms_os_20_winusb_id");
  check(ms && le16(&ms[46]) == 132 && le16(&ms[48]) == 4 &&
            le16(&ms[50]) == 7 && le16(&ms[52]) == 42 &&
            le16(&ms[96]) == 80,
        "ms_os_20_registry_property");

  EspUsbDevice webUsbOnly;
  EspUsbDeviceHidKeyboard webUsbKeyboard(webUsbOnly);
  check(webUsbOnly.begin(config), "webusb_without_vendor_begin");
  const uint8_t *webUsbOnlyBos = webUsbOnly.bosDescriptor();
  check(webUsbOnlyBos && webUsbOnly.bosDescriptorLength() == 29 &&
            le16(&webUsbOnlyBos[2]) == 29 && webUsbOnlyBos[4] == 1,
        "webusb_without_vendor_bos");
  check(webUsbOnly.microsoftOs20Descriptor() == nullptr &&
            webUsbOnly.microsoftOs20DescriptorLength() == 0,
        "ms_os_20_without_vendor_absent");
}

// Windows resolves a Microsoft OS 2.0 function subset through usbccgp.sys, which
// it loads only for a composite device. On a single-interface device the subsets
// leave the compatible ID attached to nothing, and Device Manager answers with
// CM_PROB_FAILED_INSTALL.
static EspUsbDeviceConfig webUsbConfig(uint16_t pid,
                                      EspUsbDeviceMsOs20Layout layout)
{
  EspUsbDeviceConfig config;
  config.webusbEnabled = true;
  config.webusbUrl = "https://example.com/espusbdevice";
  config.startTinyUsb = false;
  config.pid = pid;
  config.msOs20Layout = layout;
  return config;
}

static void testMicrosoftOs20LayoutFollowsInterfaceCount()
{
  // One interface: no usbccgp, so the compatible ID sits directly under the set
  // header.
  {
    EspUsbDevice single;
    EspUsbDeviceVendor singleVendor(single);
    check(single.begin(webUsbConfig(0x4024, ESP_USB_DEVICE_MS_OS_20_AUTO)),
          "ms_os_20_single_begin");
    check(single.configurationDescriptor(0)[4] == 1, "ms_os_20_single_interface");
    check(!single.microsoftOs20UsesSubsets(), "ms_os_20_single_is_flat");
    const uint8_t *flat = single.microsoftOs20Descriptor();
    check(flat && single.microsoftOs20DescriptorLength() == 162,
          "ms_os_20_flat_length");
    check(flat && le16(&flat[0]) == 10 && le16(&flat[2]) == 0 &&
              le16(&flat[8]) == 162,
          "ms_os_20_flat_set_header");
    check(flat && le16(&flat[10]) == 20 && le16(&flat[12]) == 3 &&
              memcmp(&flat[14], "WINUSB", 6) == 0,
          "ms_os_20_flat_compatible_id");
    check(flat && le16(&flat[30]) == 132 && le16(&flat[32]) == 4,
          "ms_os_20_flat_registry_property");
    // The BOS capability must publish the same total length.
    const uint8_t *flatBos = single.bosDescriptor();
    check(flatBos && le16(&flatBos[53]) == 162, "ms_os_20_flat_bos_total_length");
  }

  // Two interfaces means usbccgp, which is where the subsets belong.
  {
    EspUsbDevice composite;
    EspUsbDeviceHidKeyboard compositeKeyboard(composite);
    EspUsbDeviceVendor compositeVendor(composite);
    check(composite.begin(webUsbConfig(0x4025, ESP_USB_DEVICE_MS_OS_20_AUTO)),
          "ms_os_20_composite_begin");
    check(composite.microsoftOs20UsesSubsets(),
          "ms_os_20_composite_uses_subsets");
    check(composite.microsoftOs20DescriptorLength() == 178,
          "ms_os_20_composite_length");
  }

  // And the sketch can say so explicitly either way.
  {
    EspUsbDevice forcedSubsets;
    EspUsbDeviceVendor forcedSubsetsVendor(forcedSubsets);
    check(forcedSubsets.begin(
              webUsbConfig(0x4026, ESP_USB_DEVICE_MS_OS_20_SUBSETS)),
          "ms_os_20_forced_subsets_begin");
    check(forcedSubsets.microsoftOs20UsesSubsets(), "ms_os_20_forced_subsets");
    check(forcedSubsets.microsoftOs20DescriptorLength() == 178,
          "ms_os_20_forced_subsets_length");
    const uint8_t *forced = forcedSubsets.microsoftOs20Descriptor();
    check(forced && le16(&forced[10]) == 8 && le16(&forced[12]) == 1 &&
              le16(&forced[16]) == 168,
          "ms_os_20_forced_configuration_subset");
    check(forced && le16(&forced[18]) == 8 && le16(&forced[20]) == 2 &&
              forced[22] == 0 && le16(&forced[24]) == 160,
          "ms_os_20_forced_function_subset");
  }

  {
    EspUsbDevice forcedFlat;
    EspUsbDeviceHidKeyboard forcedFlatKeyboard(forcedFlat);
    EspUsbDeviceVendor forcedFlatVendor(forcedFlat);
    check(forcedFlat.begin(webUsbConfig(0x4027, ESP_USB_DEVICE_MS_OS_20_FLAT)),
          "ms_os_20_forced_flat_begin");
    check(!forcedFlat.microsoftOs20UsesSubsets(), "ms_os_20_forced_flat");
    check(forcedFlat.microsoftOs20DescriptorLength() == 162,
          "ms_os_20_forced_flat_length");
  }
}

static void testStringDescriptors()
{
  EspUsbDevice device;
  EspUsbDeviceConfig config;
  config.manufacturer = "EspUsb";
  config.product = "Device";
  config.serialNumber = nullptr;
  config.startTinyUsb = false;
  check(device.begin(config), "string_begin");

  const uint16_t *lang = device.stringDescriptor(0, 0);
  check((lang[0] & 0xff) == 4 && (lang[0] >> 8) == 0x03 && lang[1] == 0x0409, "string_lang");

  const uint16_t *manufacturer = device.stringDescriptor(1, 0x0409);
  check((manufacturer[0] & 0xff) == 14 && manufacturer[1] == 'E' && manufacturer[6] == 'b', "string_manufacturer");
  check(device.stringDescriptor(3, 0x0409) == nullptr, "string_serial_null");
}

static void testClassLifecycle()
{
  EspUsbDeviceConfig config;
  config.startTinyUsb = false;

  EspUsbDevice first;
  EspUsbDeviceCdcSerial firstCdc(first);
  check(first.begin(config), "lifecycle_first_begin");
  first.end();

  EspUsbDevice second;
  EspUsbDeviceCdcSerial secondCdc(second);
  check(second.begin(config), "lifecycle_reuse_after_end");
  second.end();

  // An invalid MSC class registers no callback target and rolls back classes
  // that were already begun in the same device.
  EspUsbDevice invalid;
  EspUsbDeviceCdcSerial invalidCdc(invalid);
  EspUsbDeviceMsc invalidMsc(invalid);
  check(!invalid.begin(config), "lifecycle_partial_begin_fails");

  EspUsbDevice recovered;
  EspUsbDeviceCdcSerial recoveredCdc(recovered);
  check(recovered.begin(config), "lifecycle_partial_begin_rollback");
  recovered.end();
}

static void testRuntimeLifecycle()
{
  EspUsbDeviceConfig config;
  config.startTinyUsb = true;

  EspUsbDevice active;
  EspUsbDeviceCdcSerial activeCdc(active);
  check(active.begin(config), "runtime_active_begin");
  delay(20);

  // HID begins first, then CDC is rejected because activeCdc owns the global
  // callback target. The failed begin must roll HID back without disturbing
  // the currently running device.
  EspUsbDevice blocked;
  EspUsbDeviceHidKeyboard blockedKeyboard(blocked);
  EspUsbDeviceCdcSerial blockedCdc(blocked);
  check(!blocked.begin(config), "runtime_partial_begin_fails");
  check(blocked.lastError() == ESP_FAIL, "runtime_partial_begin_error");

  active.end();
  delay(20);

  // The same instance that failed above must be reusable after the owner ends.
  check(blocked.begin(config), "runtime_partial_begin_recovers");
  delay(20);
  blocked.end();

  // Exercise TinyUSB task, controller, PHY, and callback teardown repeatedly
  // on the same object rather than relying on its destructor.
  for (uint8_t cycle = 0; cycle < 100; ++cycle)
  {
    check(blocked.begin(config), "runtime_repeated_begin");
    check(blocked.begin(config), "runtime_begin_idempotent");
    delay(20);
    blocked.end();
    blocked.end();
  }
}

void setup()
{
  Serial.begin(115200);
  delay(5000);

  Serial.println("TEST_BEGIN descriptor");
  testKeyboardDescriptor();
  testMouseDescriptor();
  testCompositeDescriptor();
  testVendorDescriptor();
  testVendorPerSpeedEndpointSize();
  testVendorSmallEndpointSize();
  testHidVendorReportDescriptor();
  testCompositeHidReportDescriptorMerge();
  testCompositeOutReportRouting();
  testCompositeWithVendorDescriptor();
  testWebUsbAndMicrosoftOs20Descriptors();
  testMicrosoftOs20LayoutFollowsInterfaceCount();
  testStringDescriptors();
  testClassLifecycle();
  testRuntimeLifecycle();
  Serial.printf("TEST_END pass=%d fail=%d\n", passCount, failCount);
  Serial.println(failCount == 0 ? "OK" : "NG");
  Serial.flush();
}

void loop()
{
}
