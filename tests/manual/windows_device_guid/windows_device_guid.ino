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

#ifndef GUID_VARIANT
#define GUID_VARIANT 0
#endif
#ifndef PINNED_REVISION
#define PINNED_REVISION 0
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
  config.pid = 0x4080;
  config.serialNumber = "guid-test-1";
  config.manufacturer = "EspUsbDevice";
  config.product = "GUID test";
  config.deviceInterfaceGuid = kGuid;
  config.msOs20VendorRevision = PINNED_REVISION;

  if (!device.begin(config))
  {
    Serial.printf("GUIDTEST_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.printf("GUIDTEST variant=%d guid=%s revision=%u len=%u subsets=%d\n",
                (int)GUID_VARIANT, kGuid,
                (unsigned)device.microsoftOs20VendorRevision(),
                (unsigned)device.microsoftOs20DescriptorLength(),
                device.microsoftOs20UsesSubsets() ? 1 : 0);
}

void loop()
{
  delay(1000);
}
