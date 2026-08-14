// EspUsbDeviceDescriptorDump - print every descriptor this library emits.
//
// On the host side you read a device's descriptors to find out what it is. On
// the device side you are the one writing them, so the useful question is the
// reverse: what did the library actually build from the classes I registered,
// and does it fit this controller?
//
// This sketch dumps, for the class set selected below:
//   - DEVICE descriptor
//   - CONFIGURATION descriptor (full-speed and high-speed variants), decoded
//     block by block
//   - DEVICE QUALIFIER and OTHER SPEED CONFIGURATION
//   - BOS and Microsoft OS 2.0 descriptors (when WebUSB is enabled)
//   - HID report descriptors
//   - string descriptors
//   - an endpoint budget summary against this target's controller limits
//
// Compare the CONFIGURATION dump with what the host received: on Linux,
// `lsusb -v -d 303a:4051`, or `tests/manual/device_inspect`. They must match
// byte for byte. If the host shows nothing, the descriptor never got out and
// the problem is below this layer.
//
// Change the class set here, rebuild, and read the new budget line. This is the
// fastest way to find out whether a composite fits before writing the sketch
// that needs it.
#define DUMP_ENABLE_KEYBOARD 1
#define DUMP_ENABLE_MOUSE 0
#define DUMP_ENABLE_GAMEPAD 0
#define DUMP_ENABLE_CDC 1
#define DUMP_ENABLE_MIDI 0
#define DUMP_ENABLE_VENDOR 0
#define DUMP_ENABLE_WEBUSB 0

#include "EspUsbDevice.h"

EspUsbDevice device;

// EspUsbDevice holds at most 4 classes (EspUsbDevice::MAX_CLASSES). A fifth
// constructed object registers nothing - its constructor's addClass() fails
// silently - so keep the set below at four or fewer and watch the CLASSES line.
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
EspUsbDeviceCdcSerial cdc(device);
#endif
#if DUMP_ENABLE_MIDI
EspUsbDeviceMidi midi(device);
#endif
#if DUMP_ENABLE_VENDOR
EspUsbDeviceVendor vendor(device);
#endif

// Endpoint numbers and directions seen while walking the configuration
// descriptor, so the budget summary reports what was really emitted rather than
// what the class list implies.
static uint8_t maxEndpointNumber = 0;
static uint8_t inEndpointCount = 0;
static uint8_t outEndpointCount = 0;
// wDescriptorLength from each HID descriptor, in the order they appear.
static uint16_t hidReportLengths[4] = {};
static uint8_t hidDescriptorCount = 0;

static uint16_t le16(const uint8_t *data)
{
  return static_cast<uint16_t>(data[0] | (data[1] << 8));
}

static void hexDump(const uint8_t *data, size_t length)
{
  for (size_t offset = 0; offset < length; offset += 16)
  {
    Serial.printf("  %04x  ", (unsigned)offset);
    for (size_t column = 0; column < 16; column++)
    {
      if (offset + column < length)
      {
        Serial.printf("%02x ", data[offset + column]);
      }
      else
      {
        Serial.print("   ");
      }
    }
    Serial.println();
  }
}

static const char *classCodeName(uint8_t code)
{
  switch (code)
  {
  case 0x00:
    return "per-interface";
  case 0x01:
    return "Audio";
  case 0x02:
    return "CDC control";
  case 0x03:
    return "HID";
  case 0x05:
    return "Physical";
  case 0x07:
    return "Printer";
  case 0x08:
    return "Mass Storage";
  case 0x09:
    return "Hub";
  case 0x0a:
    return "CDC data";
  case 0x0b:
    return "Smart Card (CCID)";
  case 0x0e:
    return "Video";
  case 0xdc:
    return "Diagnostic";
  case 0xef:
    return "Miscellaneous";
  case 0xfe:
    return "Application specific";
  case 0xff:
    return "Vendor specific";
  default:
    return "unknown";
  }
}

static const char *transferTypeName(uint8_t attributes)
{
  switch (attributes & 0x03)
  {
  case 0:
    return "control";
  case 1:
    return "isochronous";
  case 2:
    return "bulk";
  default:
    return "interrupt";
  }
}

// Walks the descriptor block by block the way a host parser does. `collect`
// separates the pass that fills the budget counters (the real, full-speed
// configuration) from the informational high-speed and other-speed dumps.
static void walkConfiguration(const uint8_t *data, uint16_t length, bool collect)
{
  uint16_t offset = 0;
  while (offset + 1 < length)
  {
    const uint8_t blockLength = data[offset];
    const uint8_t blockType = data[offset + 1];
    if (blockLength < 2 || offset + blockLength > length)
    {
      Serial.printf("  %04x  MALFORMED bLength=%u remaining=%u\n", (unsigned)offset,
                    (unsigned)blockLength, (unsigned)(length - offset));
      return;
    }

    Serial.printf("  %04x  ", (unsigned)offset);
    switch (blockType)
    {
    case 0x02: // CONFIGURATION
      Serial.printf("CONFIGURATION total=%u interfaces=%u value=%u attributes=0x%02x power=%umA\n",
                    (unsigned)le16(&data[offset + 2]), (unsigned)data[offset + 4],
                    (unsigned)data[offset + 5], (unsigned)data[offset + 7],
                    (unsigned)data[offset + 8] * 2);
      break;
    case 0x04: // INTERFACE
      Serial.printf("INTERFACE  number=%u alt=%u endpoints=%u class=0x%02x (%s) subclass=0x%02x protocol=0x%02x\n",
                    (unsigned)data[offset + 2], (unsigned)data[offset + 3],
                    (unsigned)data[offset + 4], (unsigned)data[offset + 5],
                    classCodeName(data[offset + 5]), (unsigned)data[offset + 6],
                    (unsigned)data[offset + 7]);
      break;
    case 0x05: // ENDPOINT
    {
      const uint8_t address = data[offset + 2];
      const uint8_t number = address & 0x0f;
      const bool in = (address & 0x80) != 0;
      Serial.printf("ENDPOINT   0x%02x %-3s %-11s mps=%u interval=%u\n", address,
                    in ? "IN" : "OUT", transferTypeName(data[offset + 3]),
                    (unsigned)le16(&data[offset + 4]), (unsigned)data[offset + 6]);
      if (collect)
      {
        if (number > maxEndpointNumber)
        {
          maxEndpointNumber = number;
        }
        if (in)
        {
          inEndpointCount++;
        }
        else
        {
          outEndpointCount++;
        }
      }
      break;
    }
    case 0x0b: // INTERFACE ASSOCIATION
      Serial.printf("IAD        first=%u count=%u class=0x%02x (%s) subclass=0x%02x protocol=0x%02x\n",
                    (unsigned)data[offset + 2], (unsigned)data[offset + 3],
                    (unsigned)data[offset + 4], classCodeName(data[offset + 4]),
                    (unsigned)data[offset + 5], (unsigned)data[offset + 6]);
      break;
    case 0x21: // HID
    {
      const uint16_t reportLength = blockLength >= 9 ? le16(&data[offset + 7]) : 0;
      Serial.printf("HID        bcdHID=0x%04x descriptors=%u report_descriptor=%u bytes\n",
                    (unsigned)le16(&data[offset + 2]), (unsigned)data[offset + 5],
                    (unsigned)reportLength);
      if (collect && hidDescriptorCount < 4)
      {
        hidReportLengths[hidDescriptorCount++] = reportLength;
      }
      break;
    }
    case 0x24: // CS_INTERFACE
      Serial.printf("CS_INTERFACE subtype=0x%02x length=%u\n", (unsigned)data[offset + 2],
                    (unsigned)blockLength);
      break;
    case 0x25: // CS_ENDPOINT
      Serial.printf("CS_ENDPOINT  subtype=0x%02x length=%u\n", (unsigned)data[offset + 2],
                    (unsigned)blockLength);
      break;
    default:
      Serial.printf("TYPE 0x%02x  length=%u\n", (unsigned)blockType, (unsigned)blockLength);
      break;
    }
    offset = static_cast<uint16_t>(offset + blockLength);
  }
}

static void dumpDeviceDescriptor()
{
  const uint8_t *data = device.deviceDescriptor();
  if (!data)
  {
    Serial.println("DEVICE descriptor unavailable");
    return;
  }
  Serial.println("--- DEVICE descriptor (18 bytes) ---");
  hexDump(data, 18);
  Serial.printf("  bcdUSB=0x%04x class=0x%02x (%s) subclass=0x%02x protocol=0x%02x ep0_mps=%u\n",
                (unsigned)le16(&data[2]), (unsigned)data[4], classCodeName(data[4]),
                (unsigned)data[5], (unsigned)data[6], (unsigned)data[7]);
  Serial.printf("  idVendor=0x%04x idProduct=0x%04x bcdDevice=0x%04x configurations=%u\n",
                (unsigned)le16(&data[8]), (unsigned)le16(&data[10]),
                (unsigned)le16(&data[12]), (unsigned)data[17]);
}

static void dumpConfiguration(const char *title, const uint8_t *data, bool collect)
{
  if (!data)
  {
    Serial.printf("--- %s: unavailable ---\n", title);
    return;
  }
  const uint16_t length = le16(&data[2]);
  Serial.printf("--- %s (%u bytes) ---\n", title, (unsigned)length);
  hexDump(data, length);
  walkConfiguration(data, length, collect);
}

static void dumpStringDescriptor(uint8_t index, const char *label)
{
  const uint16_t *data = device.stringDescriptor(index, 0x0409);
  if (!data)
  {
    Serial.printf("  [%u] %-12s (not set)\n", (unsigned)index, label);
    return;
  }
  const uint8_t length = static_cast<uint8_t>(data[0] & 0xff);
  if (index == 0)
  {
    Serial.printf("  [0] %-12s langid=0x%04x\n", label, (unsigned)data[1]);
    return;
  }
  Serial.printf("  [%u] %-12s \"", (unsigned)index, label);
  for (uint8_t i = 1; i < length / 2; i++)
  {
    Serial.print(static_cast<char>(data[i] & 0xff));
  }
  Serial.println("\"");
}

static void dumpEndpointBudget()
{
#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
  const char *controller = "S2/S3 full-speed";
  const uint8_t limitNumber = 5;
  const uint8_t limitIn = 4;
  const uint8_t limitOut = 5;
#elif defined(CONFIG_IDF_TARGET_ESP32P4)
  const bool fullSpeed = device.config().controller == EspUsbController::FullSpeed;
  const char *controller = fullSpeed ? "P4 rhport 0 (full-speed)" : "P4 rhport 1 (high-speed)";
  const uint8_t limitNumber = fullSpeed ? 6 : 15;
  const uint8_t limitIn = fullSpeed ? 4 : 7;
  const uint8_t limitOut = fullSpeed ? 6 : 15;
#else
  const char *controller = "unknown target";
  const uint8_t limitNumber = 15;
  const uint8_t limitIn = 15;
  const uint8_t limitOut = 15;
#endif
  Serial.println("--- endpoint budget ---");
  Serial.printf("  controller           %s\n", controller);
  Serial.printf("  highest ep number    %u / %u\n", (unsigned)maxEndpointNumber,
                (unsigned)limitNumber);
  Serial.printf("  non-control IN       %u / %u\n", (unsigned)inEndpointCount,
                (unsigned)limitIn);
  Serial.printf("  non-control OUT      %u / %u\n", (unsigned)outEndpointCount,
                (unsigned)limitOut);
  if (maxEndpointNumber > limitNumber || inEndpointCount > limitIn ||
      outEndpointCount > limitOut)
  {
    Serial.println("  OVER BUDGET - begin() rejects this set before starting the PHY.");
  }
}

void setup()
{
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
  config.webusbUrl = "example.com/espusbdevice";
#endif
  // The descriptors are built here, so nothing below is available until begin()
  // has run. A begin() failure still leaves the dump readable, which is the
  // point: it shows what was too big.
  const bool started = device.begin(config);
  Serial.printf("BEGIN %s error=%s\n", started ? "ok" : "failed", device.lastErrorName());

  dumpDeviceDescriptor();
  dumpConfiguration("CONFIGURATION descriptor (full-speed)",
                    device.configurationDescriptorForSpeed(0, false), true);
  dumpConfiguration("CONFIGURATION descriptor (high-speed)",
                    device.configurationDescriptorForSpeed(0, true), false);

  const uint8_t *qualifier = device.deviceQualifierDescriptor();
  if (qualifier)
  {
    Serial.println("--- DEVICE QUALIFIER (10 bytes) ---");
    hexDump(qualifier, 10);
    Serial.println("  Returned when the host asks what this device would do at the other");
    Serial.println("  speed. Only a high-speed capable device has to answer it.");
  }

  dumpConfiguration("OTHER SPEED CONFIGURATION (as seen from high-speed)",
                    device.otherSpeedConfigurationDescriptor(0, true), false);

  const uint8_t *bos = device.bosDescriptor();
  if (bos && device.bosDescriptorLength() > 0)
  {
    Serial.printf("--- BOS descriptor (%u bytes) ---\n",
                  (unsigned)device.bosDescriptorLength());
    hexDump(bos, device.bosDescriptorLength());
    const uint8_t *msos = device.microsoftOs20Descriptor();
    if (msos && device.microsoftOs20DescriptorLength() > 0)
    {
      Serial.printf("--- Microsoft OS 2.0 descriptor (%u bytes) ---\n",
                    (unsigned)device.microsoftOs20DescriptorLength());
      hexDump(msos, device.microsoftOs20DescriptorLength());
    }
  }
  else
  {
    Serial.println("--- BOS descriptor: none (WebUSB disabled) ---");
  }

  for (uint8_t instance = 0; instance < hidDescriptorCount; instance++)
  {
    const uint8_t *report = device.hidReportDescriptor(instance);
    if (!report || hidReportLengths[instance] == 0)
    {
      continue;
    }
    Serial.printf("--- HID report descriptor, interface %u (%u bytes) ---\n",
                  (unsigned)instance, (unsigned)hidReportLengths[instance]);
    hexDump(report, hidReportLengths[instance]);
    Serial.println("  Paste these bytes into a HID report descriptor decoder, or read them");
    Serial.println("  back from the host with usbhid-dump / tests/manual/device_inspect.");
  }

  Serial.println("--- string descriptors ---");
  dumpStringDescriptor(0, "langid");
  dumpStringDescriptor(1, "manufacturer");
  dumpStringDescriptor(2, "product");
  dumpStringDescriptor(3, "serial");
  dumpStringDescriptor(4, "class");

  dumpEndpointBudget();
  Serial.println("=== end of dump ===");
}

void loop()
{
  delay(1000);
}
