// Prints every descriptor the library built from the registered Functions and
// compares the endpoints against this controller's budget. No Host is needed.
#define DUMP_ENABLE_KEYBOARD 1
#define DUMP_ENABLE_MOUSE 0
#define DUMP_ENABLE_GAMEPAD 0
#define DUMP_ENABLE_CDC 1
#define DUMP_ENABLE_MIDI 0
#define DUMP_ENABLE_VENDOR 0
#define DUMP_ENABLE_WEBUSB 0
// 1: build descriptors without starting the controller (ready() stays false).
#define DUMP_DESCRIPTORS_ONLY 0

#include <EspUsbDevice.h>

EspUsbDevice device;
#if DUMP_ENABLE_KEYBOARD
EspUsbDeviceHidKeyboard keyboard(device);
#endif
#if DUMP_ENABLE_MOUSE
EspUsbDeviceHidMouse mouse(device);
#endif
#if DUMP_ENABLE_GAMEPAD
EspUsbDeviceHidGamepad gamepad(device);
#endif
#if DUMP_ENABLE_CDC
EspUsbDeviceCdcSerial cdc(device, "Dump CDC");
#endif
#if DUMP_ENABLE_MIDI
EspUsbDeviceMidi midi(device);
#endif
#if DUMP_ENABLE_VENDOR
EspUsbDeviceVendor vendor(device);
#endif

// Counted from the descriptor actually emitted, not from the Function list.
uint8_t maxEndpointNumber = 0, inEndpointCount = 0, outEndpointCount = 0;

uint16_t le16(const uint8_t *data) { return uint16_t(data[0] | (data[1] << 8)); }

void hexDump(const uint8_t *data, size_t length) {
  for (size_t offset = 0; offset < length; offset += 16) {
    Serial.printf("  %04x  ", unsigned(offset));
    for (size_t column = 0; column < 16 && offset + column < length; column++) {
      Serial.printf("%02x ", data[offset + column]);
    }
    Serial.println();
  }
}

const char *classCodeName(uint8_t code) {
  switch (code) {
  case 0x00: return "per-interface";
  case 0x01: return "Audio";
  case 0x02: return "CDC control";
  case 0x03: return "HID";
  case 0x08: return "Mass Storage";
  case 0x0a: return "CDC data";
  case 0x0b: return "Smart Card (CCID)";
  case 0x0e: return "Video";
  case 0xef: return "Miscellaneous";
  case 0xfe: return "Application specific";
  case 0xff: return "Vendor specific";
  default: return "other";
  }
}

const char *transferTypeName(uint8_t attributes) {
  switch (attributes & 0x03) {
  case 0: return "control";
  case 1: return "isochronous";
  case 2: return "bulk";
  default: return "interrupt";
  }
}

// Walks block by block as a Host parser does. `collect` counts endpoints for
// the budget from the full-speed configuration only.
void walkConfiguration(const uint8_t *data, uint16_t length, bool collect) {
  uint16_t offset = 0;
  while (offset + 1 < length) {
    const uint8_t blockLength = data[offset], blockType = data[offset + 1];
    if (blockLength < 2 || offset + blockLength > length) {
      Serial.printf("  %04x  MALFORMED bLength=%u remaining=%u\n", offset, blockLength,
          unsigned(length - offset));
      return;
    }
    const uint8_t *b = data + offset;
    Serial.printf("  %04x  ", offset);
    switch (blockType) {
    case 0x02:
      Serial.printf("CONFIGURATION total=%u interfaces=%u value=%u attributes=0x%02x power=%umA\n",
          le16(b + 2), b[4], b[5], b[7], b[8] * 2);
      break;
    case 0x04:
      Serial.printf("INTERFACE  number=%u alt=%u endpoints=%u class=0x%02x (%s) subclass=0x%02x protocol=0x%02x string=%u\n",
          b[2], b[3], b[4], b[5], classCodeName(b[5]), b[6], b[7], b[8]);
      break;
    case 0x05: {
      const uint8_t address = b[2], number = address & 0x0f;
      const bool in = address & 0x80;
      Serial.printf("ENDPOINT   0x%02x %-3s %-11s mps=%u interval=%u\n", address,
          in ? "IN" : "OUT", transferTypeName(b[3]), le16(b + 4), b[6]);
      if (collect) {
        if (number > maxEndpointNumber) maxEndpointNumber = number;
        in ? inEndpointCount++ : outEndpointCount++;
      }
      break;
    }
    case 0x0b:
      Serial.printf("IAD        first=%u count=%u class=0x%02x (%s) subclass=0x%02x protocol=0x%02x string=%u\n",
          b[2], b[3], b[4], classCodeName(b[4]), b[5], b[6], b[7]);
      break;
    case 0x21:
      Serial.printf("HID        bcdHID=0x%04x descriptors=%u report_descriptor=%u bytes\n",
          le16(b + 2), b[5], blockLength >= 9 ? le16(b + 7) : 0);
      break;
    case 0x24:
      Serial.printf("CS_INTERFACE subtype=0x%02x length=%u\n", b[2], blockLength);
      break;
    case 0x25:
      Serial.printf("CS_ENDPOINT  subtype=0x%02x length=%u\n", b[2], blockLength);
      break;
    default:
      Serial.printf("TYPE 0x%02x  length=%u\n", blockType, blockLength);
      break;
    }
    offset += blockLength;
  }
}

void dumpDeviceDescriptor() {
  const uint8_t *data = device.deviceDescriptor();
  if (!data) {
    Serial.println("DEVICE descriptor unavailable");
    return;
  }
  Serial.println("--- DEVICE descriptor (18 bytes) ---");
  hexDump(data, 18);
  Serial.printf("  bcdUSB=0x%04x class=0x%02x (%s) subclass=0x%02x protocol=0x%02x ep0_mps=%u\n",
      le16(data + 2), data[4], classCodeName(data[4]), data[5], data[6], data[7]);
  Serial.printf("  idVendor=0x%04x idProduct=0x%04x bcdDevice=0x%04x configurations=%u\n",
      le16(data + 8), le16(data + 10), le16(data + 12), data[17]);
}

void dumpConfiguration(const char *title, const uint8_t *data, bool collect) {
  if (!data) {
    Serial.printf("--- %s: unavailable ---\n", title);
    return;
  }
  const uint16_t length = le16(data + 2);
  Serial.printf("--- %s (%u bytes) ---\n", title, length);
  hexDump(data, length);
  walkConfiguration(data, length, collect);
}

// Strings: 1-3 are manufacturer/product/serial, 4 is reserved, then Function
// names and class-specific strings (only the ones that exist).
void dumpStringDescriptors() {
  Serial.println("--- string descriptors ---");
  const uint16_t *language = device.stringDescriptor(0, 0x0409);
  if (language) Serial.printf("  [0] langid=0x%04x\n", language[1]);
  for (unsigned index = 1; index < 256; index++) {
    const uint16_t *data = device.stringDescriptor(uint8_t(index), 0x0409);
    if (!data) continue;
    const uint8_t bytes = uint8_t(data[0] & 0xff);
    Serial.printf("  [%u] \"", index);
    for (uint8_t i = 1; i < bytes / 2; i++) {
      const uint16_t unit = data[i];
      if (unit < 0x80) Serial.print(char(unit));
      else Serial.printf("\\u%04x", unit);  // UTF-16 code unit
    }
    Serial.println("\"");
  }
}

void dumpEndpointBudget() {
  const EspUsbController controller = device.config().controller;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  const bool fullSpeed = controller == EspUsbController::FullSpeed;
  const char *name = fullSpeed ? "P4 full-speed port" : "P4 high-speed port";
  const uint8_t limitNumber = fullSpeed ? 6 : 15, limitIn = fullSpeed ? 4 : 7;
#else
  (void)controller;
  const char *name = "S2/S3 full-speed";
  const uint8_t limitNumber = 6, limitIn = 4;
#endif
  const uint8_t limitOut = limitNumber;
  Serial.println("--- endpoint budget ---");
  Serial.printf("  controller           %s\n", name);
  Serial.printf("  highest ep number    %u / %u\n", maxEndpointNumber, limitNumber);
  Serial.printf("  non-control IN       %u / %u\n", inEndpointCount, limitIn);
  Serial.printf("  non-control OUT      %u / %u\n", outEndpointCount, limitOut);
  Serial.printf("  double-buffered IN   mask=0x%04x\n", device.bulkInDoubleBuffered());
  if (maxEndpointNumber > limitNumber || inEndpointCount > limitIn || outEndpointCount > limitOut) {
    Serial.println("  OVER BUDGET - begin() rejects this set before starting the PHY.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("=== EspUsbDevice descriptor dump ===");

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4051;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice DescriptorDump";
  config.serialNumber = "espusb-descdump";
#if DUMP_ENABLE_WEBUSB
  config.webusbEnabled = true;
  config.webusbUrl = "https://example.com/espusbdevice";
#endif
#if DUMP_DESCRIPTORS_ONLY
  config.startMode = EspUsbDeviceStartMode::DescriptorsOnly;
#endif
  // Descriptors are built inside begin(). After a failure they are read back
  // where possible, which shows what did not fit.
  const bool started = device.begin(config);
  Serial.printf("BEGIN %s error=%s\n", started ? "ok" : "failed", device.lastErrorName());

  dumpDeviceDescriptor();
  dumpConfiguration("CONFIGURATION descriptor (full-speed)",
      device.configurationDescriptorForSpeed(0, false), true);
  dumpConfiguration("CONFIGURATION descriptor (high-speed)",
      device.configurationDescriptorForSpeed(0, true), false);

  const uint8_t *qualifier = device.deviceQualifierDescriptor();
  if (qualifier) {
    Serial.println("--- DEVICE QUALIFIER (10 bytes) ---");
    hexDump(qualifier, 10);
    Serial.println("  Returned when the host asks what this device would do at the other");
    Serial.println("  speed. Only a high-speed capable device has to answer it.");
  }
  dumpConfiguration("OTHER SPEED CONFIGURATION (as seen from high-speed)",
      device.otherSpeedConfigurationDescriptor(0, true), false);

  // BOS exists with WebUSB, or with a Vendor/DFU Function (Microsoft OS 2.0).
  const uint8_t *bos = device.bosDescriptor();
  if (bos && device.bosDescriptorLength()) {
    Serial.printf("--- BOS descriptor (%u bytes) ---\n", device.bosDescriptorLength());
    hexDump(bos, device.bosDescriptorLength());
    const uint8_t *url = device.webUsbUrlDescriptor();
    if (url) {
      Serial.printf("--- WebUSB URL descriptor (%u bytes, scheme=%u) ---\n", url[0], url[2]);
      hexDump(url, url[0]);
    }
    const uint8_t *msos = device.microsoftOs20Descriptor();
    if (msos && device.microsoftOs20DescriptorLength()) {
      Serial.printf("--- Microsoft OS 2.0 descriptor (%u bytes, vendor_revision=0x%04x, subsets=%u) ---\n",
          device.microsoftOs20DescriptorLength(), device.microsoftOs20VendorRevision(),
          device.microsoftOs20UsesSubsets());
      hexDump(msos, device.microsoftOs20DescriptorLength());
    }
  } else {
    Serial.println("--- BOS descriptor: none (no WebUSB, Vendor or DFU) ---");
  }

  // Several HID Functions share interface 0 and one merged report descriptor.
  const uint8_t *report = device.hidReportDescriptor(0);
  const uint16_t reportLength = device.hidReportDescriptorLength(0);
  if (report && reportLength) {
    Serial.printf("--- HID report descriptor, interface 0 (%u bytes) ---\n", reportLength);
    hexDump(report, reportLength);
    Serial.println("  Paste these bytes into a HID report descriptor decoder, or read them");
    Serial.println("  back from the host with usbhid-dump.");
  }

  dumpStringDescriptors();
  dumpEndpointBudget();
  Serial.println("=== end of dump ===");
}

void loop() {
  delay(1000);
}
