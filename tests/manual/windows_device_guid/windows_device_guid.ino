// Does Windows actually record the GUID we asked for, and does changing it
// take effect on a PC that has already seen this device?
//
// The second half is the whole point of MS_OS_20_FEATURE_VENDOR_REVISION.
// Windows caches the registry properties it read the first time it enumerated a
// VID/PID/serial and re-reads them only when the vendor revision changes, so
// the test has to keep the identity fixed and change only the GUID.
//
// Build variants, selected from build_opt.h:
//   (none)                      GUID A, revision derived
//   -DGUID_VARIANT=1            GUID B, revision derived (must take effect)
//   -DGUID_VARIANT=2 -DPINNED_REVISION=<n>
//                               GUID C, revision pinned to variant 1's value
//                               (must NOT take effect - the control)
#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceVendor Vendor(device);
// Added only for the composite variant: a second interface is what makes
// Windows load usbccgp and split the device into children, which is the layout
// change the guide needs an answer for.
#ifndef VAR_COMPOSITE
#define VAR_COMPOSITE 0
#endif
#if VAR_COMPOSITE == 1
EspUsbDeviceHidKeyboard Keyboard(device);
#elif VAR_COMPOSITE == 2
// The same interface count as variant 1, but MI_00 becomes mass storage
// instead of HID. HID functions are always emitted first, so this is the only
// way to change what sits at a given interface number without changing how
// many there are - which is the case that keeps both child instance IDs and
// changes only their compatible IDs.
static uint8_t mscStorage[64 * 1024];
EspUsbDeviceMsc Msc(device);
EspUsbDeviceMscFatRamDisk MscDisk(mscStorage, sizeof(mscStorage));
#endif

#ifndef GUID_VARIANT
#define GUID_VARIANT 0
#endif
#ifndef PINNED_REVISION
#define PINNED_REVISION 0
#endif
// The identity axes. Windows keys a device instance on VID, PID and serial, so
// these are what decide whether it is looking at "this device again" or a new
// one - which is a different question from whether it re-reads the descriptors.
#ifndef VAR_PID
#define VAR_PID 0x4080
#endif
#ifndef VAR_SERIAL
#define VAR_SERIAL "guid-test-1"
#endif
// 1 omits iSerialNumber entirely, which makes Windows fall back to keying the
// instance on where the device is plugged in.
#ifndef VAR_NO_SERIAL
#define VAR_NO_SERIAL 0
#endif

#if GUID_VARIANT == 0
static const char *const kGuid = "{A1A1A1A1-1111-4111-8111-111111111111}";
#elif GUID_VARIANT == 1
static const char *const kGuid = "{B2B2B2B2-2222-4222-8222-222222222222}";
#else
static const char *const kGuid = "{C3C3C3C3-3333-4333-8333-333333333333}";
#endif

void setup()
{
  Serial.begin(115200);
  delay(2000);

  EspUsbDeviceConfig config;
  // The identity stays fixed across every variant: Windows keys its cache on
  // VID, PID and serial, and a changed identity would make it read the
  // descriptors afresh for a new device, which proves nothing.
  config.vid = 0x303a;
  config.pid = VAR_PID;
#if !VAR_NO_SERIAL
  config.serialNumber = VAR_SERIAL;
#endif
  config.manufacturer = "EspUsbDevice";
  config.product = "GUID test";
  config.deviceInterfaceGuid = kGuid;
  config.msOs20VendorRevision = PINNED_REVISION;
#ifndef VAR_CCGP
#define VAR_CCGP 0
#endif
  config.msOs20CcgpDevice = VAR_CCGP != 0;

#if VAR_COMPOSITE == 2
  MscDisk.format("WINGUID");
  MscDisk.attach(Msc);
#endif
  if (!device.begin(config))
  {
    Serial.printf("GUIDTEST_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.printf("GUIDTEST variant=%d pid=0x%04x serial=%s composite=%d\n",
                (int)GUID_VARIANT, (unsigned)VAR_PID,
                VAR_NO_SERIAL ? "(none)" : VAR_SERIAL, (int)VAR_COMPOSITE);
  Serial.printf("GUIDTEST ccgp=%d\n", (int)VAR_CCGP);
  Serial.printf("GUIDTEST guid=%s revision=%u len=%u subsets=%d interfaces=%u\n",
                kGuid, (unsigned)device.microsoftOs20VendorRevision(),
                (unsigned)device.microsoftOs20DescriptorLength(),
                device.microsoftOs20UsesSubsets() ? 1 : 0,
                (unsigned)device.configurationDescriptor(0)[4]);
}

void loop()
{
  delay(1000);
}
