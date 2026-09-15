// What EspUsbDeviceVendor::writeDirect() refuses, and why, on the chip.
//
// Built non-buffered (build_opt.h): the direct transfer path only exists in a
// build that asks for it. The first assertion is directWriteSupported(), which
// is the *library's* view rather than the sketch's - a sketch can see
// CFG_TUD_VENDOR_TXRX_BUFFERED from its own command line while the library was
// compiled without it, which is exactly what a stale build (no --clean) looks
// like. Asserting the library's view is what makes this test immune to it.
//
// Every check here happens before the endpoint is touched, so startTinyUsb
// stays false: no PHY, no host, and the USB Serial/JTAG log stays alive.
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

static void checkError(EspUsbDeviceVendor &vendor, bool result,
                       EspUsbDeviceVendorDirectError expected, const char *name)
{
  char buffer[80];
  snprintf(buffer, sizeof(buffer), "%s_returns_false", name);
  check(!result, buffer);
  snprintf(buffer, sizeof(buffer), "%s_reports_%s", name, vendor.lastDirectErrorName());
  check(vendor.lastDirectError() == expected, buffer);
}

void setup()
{
  Serial.begin(115200);
  delay(2000);
  Serial.println("TEST_BEGIN vendor_direct");

  // The library's own view. If this is false the build did not take, and every
  // assertion below would pass for the wrong reason (all NotSupported).
  check(EspUsbDeviceVendor::directWriteSupported(), "direct_build");

  EspUsbDevice device;
  EspUsbDeviceVendor vendor(device);
  EspUsbDeviceConfig config;
  config.pid = 0x4076;
  config.startTinyUsb = false;
  check(device.begin(config), "begin");

  static uint8_t __attribute__((aligned(64))) aligned[256];

  // Caller mistakes, all caught before the endpoint is claimed.
  checkError(vendor, vendor.writeDirect(nullptr, 64),
             EspUsbDeviceVendorDirectError::BadArgument, "null");
  checkError(vendor, vendor.writeDirect(aligned, 0),
             EspUsbDeviceVendorDirectError::BadArgument, "zero_length");
  // usbd_edpt_xfer() takes a uint16_t, so 65536 cannot be expressed.
  checkError(vendor, vendor.writeDirect(aligned, 0x10000),
             EspUsbDeviceVendorDirectError::BadArgument, "too_long");
  checkError(vendor, vendor.writeDirect(aligned + 1, 64),
             EspUsbDeviceVendorDirectError::NotAligned, "misaligned");

  // PSRAM is not DMA-capable here, and the contract says so rather than
  // silently copying. Skipped where the board has none.
  void *external = ps_malloc(256);
  if (external)
  {
    // ps_malloc does not promise 64-byte alignment; only test the DMA rule when
    // the address passes the alignment rule that is checked first.
    if ((reinterpret_cast<uintptr_t>(external) & 63u) == 0)
    {
      checkError(vendor, vendor.writeDirect(external, 64),
                 EspUsbDeviceVendorDirectError::NotDmaCapable, "psram");
    }
    else
    {
      Serial.println("SKIP psram_not_64_byte_aligned");
    }
    free(external);
  }
  else
  {
    Serial.println("SKIP no_psram");
  }

  // A well-formed call still has nowhere to go: TinyUSB was never started, so
  // this interface has no open endpoint.
  checkError(vendor, vendor.writeDirect(aligned, 64),
             EspUsbDeviceVendorDirectError::NotMounted, "not_mounted");

  // The buffered-only API is what the direct build gives up.
  check(vendor.available() == 0, "available_is_zero");
  uint8_t sink[8];
  check(vendor.read(sink, sizeof(sink)) == 0, "read_is_zero");
  vendor.flush(); // must not crash; there is no FIFO to flush

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
