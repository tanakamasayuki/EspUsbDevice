// What does each identity field of EspUsbDeviceConfig do to a Windows PC that
// has already seen the device - and which of them make it a *new* device?
//
// The sketch is a fixed identity with every field on a build_opt.h switch, so
// one thing can be changed at a time and the Windows side read back with
// windows_identity.py. Nothing here is a test in itself; the measurements and
// what they mean are in README.md and in the user guide.
//
// Functions, in registration order, as VAR_F1..VAR_F3 (0 = none):
//   1 vendor   2 CDC ACM   3 HID keyboard   4 mass storage
#include "EspUsbDevice.h"

#ifndef VAR_VID
#define VAR_VID 0x303a
#endif
#ifndef VAR_PID
#define VAR_PID 0x4090
#endif
#ifndef VAR_SERIAL
#define VAR_SERIAL "ident-1"
#endif
#ifndef VAR_NO_SERIAL
#define VAR_NO_SERIAL 0
#endif
#ifndef VAR_PRODUCT
#define VAR_PRODUCT "Identity test A"
#endif
#ifndef VAR_MANUFACTURER
#define VAR_MANUFACTURER "EspUsbDevice"
#endif
#ifndef VAR_DEVICE_VERSION
#define VAR_DEVICE_VERSION 0x0100
#endif
#ifndef VAR_GUID
#define VAR_GUID "{D4D4D4D4-4444-4444-8444-444444444444}"
#endif
#ifndef VAR_REVISION
#define VAR_REVISION 0
#endif
#ifndef VAR_CCGP
#define VAR_CCGP 0
#endif
// 0 auto, 1 flat, 2 subsets
#ifndef VAR_LAYOUT
#define VAR_LAYOUT 0
#endif
#ifndef VAR_WEBUSB
#define VAR_WEBUSB 0
#endif
#ifndef VAR_MAX_POWER
#define VAR_MAX_POWER 100
#endif
#ifndef VAR_SELF_POWERED
#define VAR_SELF_POWERED 0
#endif
// CDC function name (iFunction / iInterface). Empty string = no name.
#ifndef VAR_CDC_NAME
#define VAR_CDC_NAME "Console"
#endif
#ifndef VAR_F1
#define VAR_F1 1
#endif
#ifndef VAR_F2
#define VAR_F2 0
#endif
#ifndef VAR_F3
#define VAR_F3 0
#endif

EspUsbDevice device;

#define HAS_FUNC(k) (VAR_F1 == (k) || VAR_F2 == (k) || VAR_F3 == (k))

#if HAS_FUNC(4)
static uint8_t mscStorage[64 * 1024];
#endif

// Registration order is declaration order, so each slot is expanded in turn;
// the preprocessor cannot nest #if inside a macro, hence three blocks.

#if VAR_F1 == 1
EspUsbDeviceVendor Vendor(device);
#elif VAR_F1 == 2
EspUsbDeviceCdcSerial Cdc(device, VAR_CDC_NAME[0] ? VAR_CDC_NAME : nullptr);
#elif VAR_F1 == 3
EspUsbDeviceHidKeyboard Keyboard(device);
#elif VAR_F1 == 4
EspUsbDeviceMsc Msc(device);
EspUsbDeviceMscFatRamDisk MscDisk(mscStorage, sizeof(mscStorage));
#endif

#if VAR_F2 == 1
EspUsbDeviceVendor Vendor(device);
#elif VAR_F2 == 2
EspUsbDeviceCdcSerial Cdc(device, VAR_CDC_NAME[0] ? VAR_CDC_NAME : nullptr);
#elif VAR_F2 == 3
EspUsbDeviceHidKeyboard Keyboard(device);
#elif VAR_F2 == 4
EspUsbDeviceMsc Msc(device);
EspUsbDeviceMscFatRamDisk MscDisk(mscStorage, sizeof(mscStorage));
#endif

#if VAR_F3 == 1
EspUsbDeviceVendor Vendor(device);
#elif VAR_F3 == 2
EspUsbDeviceCdcSerial Cdc(device, VAR_CDC_NAME[0] ? VAR_CDC_NAME : nullptr);
#elif VAR_F3 == 3
EspUsbDeviceHidKeyboard Keyboard(device);
#elif VAR_F3 == 4
EspUsbDeviceMsc Msc(device);
EspUsbDeviceMscFatRamDisk MscDisk(mscStorage, sizeof(mscStorage));
#endif

void setup()
{
  Serial.begin(115200);
  delay(2000);

  EspUsbDeviceConfig config;
  config.vid = VAR_VID;
  config.pid = VAR_PID;
#if !VAR_NO_SERIAL
  config.serialNumber = VAR_SERIAL;
#endif
  config.manufacturer = VAR_MANUFACTURER;
  config.product = VAR_PRODUCT;
  config.deviceVersion = VAR_DEVICE_VERSION;
  config.deviceInterfaceGuid = VAR_GUID;
  config.msOs20VendorRevision = VAR_REVISION;
  config.msOs20CcgpDevice = VAR_CCGP != 0;
  config.msOs20Layout = VAR_LAYOUT == 1   ? ESP_USB_DEVICE_MS_OS_20_FLAT
                        : VAR_LAYOUT == 2 ? ESP_USB_DEVICE_MS_OS_20_SUBSETS
                                          : ESP_USB_DEVICE_MS_OS_20_AUTO;
  config.webusbEnabled = VAR_WEBUSB != 0;
  config.webusbUrl = VAR_WEBUSB ? "example.com/espusbdevice" : nullptr;
  config.maxPowerMilliamps = VAR_MAX_POWER;
  config.selfPowered = VAR_SELF_POWERED != 0;

#if HAS_FUNC(4)
  MscDisk.format("IDENT");
  MscDisk.attach(Msc);
#endif
  if (!device.begin(config))
  {
    Serial.printf("IDENT_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  // Everything the build chose, then what the descriptors actually say, so the
  // Windows-side reading can be checked against the bytes rather than the
  // intent.
  Serial.printf("IDENT funcs=%d,%d,%d vid=0x%04x pid=0x%04x serial=%s product=\"%s\" manufacturer=\"%s\"\n",
                (int)VAR_F1, (int)VAR_F2, (int)VAR_F3, (unsigned)VAR_VID, (unsigned)VAR_PID,
                VAR_NO_SERIAL ? "(none)" : VAR_SERIAL, VAR_PRODUCT, VAR_MANUFACTURER);
  Serial.printf("IDENT version=0x%04x guid=%s revision=%u ccgp=%d layout=%d webusb=%d power=%u self=%d cdcname=\"%s\"\n",
                (unsigned)VAR_DEVICE_VERSION, VAR_GUID, (unsigned)device.microsoftOs20VendorRevision(),
                (int)VAR_CCGP, (int)VAR_LAYOUT, (int)VAR_WEBUSB, (unsigned)VAR_MAX_POWER,
                (int)VAR_SELF_POWERED, VAR_CDC_NAME);
  const uint8_t *dev = device.deviceDescriptor();
  Serial.printf("IDENT devdesc bcdUSB=%02x%02x class=%02x/%02x/%02x bcdDevice=%02x%02x iMan=%u iProd=%u iSer=%u\n",
                dev[3], dev[2], dev[4], dev[5], dev[6], dev[13], dev[12], dev[14], dev[15], dev[16]);
  const uint8_t *cfg = device.configurationDescriptor(0);
  const uint16_t total = static_cast<uint16_t>(cfg[2] | (cfg[3] << 8));
  Serial.printf("IDENT config interfaces=%u attributes=0x%02x maxpower=%umA\n", cfg[4], cfg[7],
                (unsigned)cfg[8] * 2);
  for (uint16_t o = 0; o + 2 <= total && cfg[o]; o = static_cast<uint16_t>(o + cfg[o]))
  {
    if (cfg[o + 1] == 0x0b)
    {
      Serial.printf("IDENT iad first=%u count=%u class=%02x iFunction=%u\n", cfg[o + 2], cfg[o + 3],
                    cfg[o + 4], cfg[o + 7]);
    }
    else if (cfg[o + 1] == 0x04)
    {
      Serial.printf("IDENT itf num=%u class=%02x/%02x/%02x iInterface=%u\n", cfg[o + 2], cfg[o + 5],
                    cfg[o + 6], cfg[o + 7], cfg[o + 8]);
    }
  }
  Serial.printf("IDENT msos20 len=%u subsets=%d\n", (unsigned)device.microsoftOs20DescriptorLength(),
                device.microsoftOs20UsesSubsets() ? 1 : 0);
}

void loop()
{
  delay(1000);
}
