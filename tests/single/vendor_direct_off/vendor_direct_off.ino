// The default build has no direct transfer path, and says so.
//
// The companion to tests/single/vendor_direct, which builds the same library
// with -DCFG_TUD_VENDOR_TXRX_BUFFERED=0. This one deliberately has no
// build_opt.h: it pins what a sketch sees when it has not opted in, which is
// the state every existing sketch is in.
//
// That matters because the direct path is gated at compile time for a reason:
// in a buffered build the vendor class arms a ZLP of its own after a transfer
// whose length is a multiple of wMaxPacketSize, it takes the endpoint claim,
// and a stream armed from anywhere but the completion callback stops dead.
// Refusing is how that configuration is kept unreachable.
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

void setup()
{
  Serial.begin(115200);
  delay(2000);
  Serial.println("TEST_BEGIN vendor_direct_off");

  check(!EspUsbDeviceVendor::directWriteSupported(), "not_a_direct_build");

  EspUsbDevice device;
  EspUsbDeviceVendor vendor(device);
  EspUsbDeviceConfig config;
  config.pid = 0x4077;
  config.startTinyUsb = false;
  check(device.begin(config), "begin");

  static uint8_t __attribute__((aligned(64))) aligned[256];

  // Every call is refused for the same reason, ahead of any other check: a
  // caller cannot accidentally discover a half-working direct path by getting
  // the buffer right.
  check(!vendor.writeDirect(aligned, 64), "refuses_a_valid_call");
  check(vendor.lastDirectError() == EspUsbDeviceVendorDirectError::NotSupported,
        "reports_not_supported");
  check(!vendor.writeDirect(nullptr, 0), "refuses_a_bad_call");
  check(vendor.lastDirectError() == EspUsbDeviceVendorDirectError::NotSupported,
        "bad_call_also_not_supported");

  Serial.print("TEST_END pass=");
  Serial.print(passCount);
  Serial.print(" fail=");
  Serial.println(failCount);
  Serial.println(failCount == 0 ? "OK" : "NG");
}

void loop()
{
  delay(1000);
}
