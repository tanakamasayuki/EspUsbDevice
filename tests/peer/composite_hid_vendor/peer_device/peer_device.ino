#include "EspUsbDevice.h"

// Composite device: HID keyboard + bulk Vendor on one EspUsbDevice.
// Regression guard for the HID + bulk Vendor descriptor-duplication fix: before
// the fix the composite HID loader copied the whole configuration body
// (HID interface + trailing Vendor interface) as the "HID" blob, and the core
// then enabled the Vendor interface a second time, so the Vendor interface (and
// its endpoints/number) was duplicated and the device failed to enumerate.
// See src/EspUsbDevice.cpp (espUsbDeviceLoadHidDescriptor / buildDescriptors)
// and docs/DESIGN_NOTES.ja.md "複合時の HID + bulk Vendor 二重記述".
// Pairs with composite_hid_vendor.ino (host).

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceVendor Vendor(device);

static bool beginOk = false;
static const char *beginError = "ESP_OK";
static volatile uint32_t vendorOnRxCount = 0;
static volatile uint32_t vendorRxTotal = 0;

static void processVendorRx()
{
  size_t available = Vendor.available();
  uint8_t buffer[64];
  while (available > 0)
  {
    const size_t chunk = Vendor.read(buffer, min(available, sizeof(buffer)));
    if (chunk == 0)
    {
      break;
    }
    vendorRxTotal += chunk;
    Vendor.write(reinterpret_cast<const uint8_t *>("echo:"), 5);
    Vendor.write(buffer, chunk);
    Vendor.flush();
    available = Vendor.available();
  }
}

static bool tapKeyWithRetry(char c)
{
  const uint32_t start = millis();
  while (millis() - start < 1000)
  {
    if (keyboard.tapKey(c))
    {
      return true;
    }
    delay(5);
  }
  return false;
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  Vendor.onRx([](size_t)
              {
                vendorOnRxCount++;
                processVendorRx();
              });

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4024;
  config.manufacturer = "EspUsbDevice";
  config.product = "EspUsbDevice HID+Vendor";
  config.serialNumber = "espusb-hid-vendor";

  beginOk = device.begin(config);
  beginError = device.lastErrorName();
  Serial.printf("DEVICE_BEGIN %s %s\n", beginOk ? "ok" : "ng", beginError);
}

// Block until the host has configured us, so every command below answers from a
// usable device whatever order the tests run in.
//
// device.ready() is tud_mounted(): the host completed SET_CONFIGURATION. That is
// the event the tests actually depend on, and it is answered on demand rather
// than announced once at boot - a test that does not run first never sees an
// announcement, which is what made most of this suite fail when its modules were
// run in reverse. tests/peer/usb_msc has had this shape all along and was the
// only peer module that survived that check.
static bool waitForHost(uint32_t timeoutMs = 5000)
{
  const uint32_t startedAt = millis();
  while (!device.ready() && millis() - startedAt < timeoutMs)
  {
    device.task();
    delay(10);
  }
  return device.ready();
}

void loop()
{
  if (Serial.available() > 0)
  {
    char command = Serial.read();
    const bool hostReady = waitForHost();
    if (command == '?')
    {
      Serial.printf("DEVICE_READY %u\n", hostReady ? 1 : 0);
    }
    else if (command == 'b')
    {
      Serial.printf("DEVICE_BEGIN %s %s\n", beginOk ? "ok" : "ng", beginError);
    }
    else if (command == 'k')
    {
      Serial.printf("DEVICE_KEY %u\n", tapKeyWithRetry('a') ? 1 : 0);
    }
    else if (command == 'q')
    {
      // On-demand vendor RX state: did onRx fire (onrx) and how many bytes came
      // in via the callback (rxtotal)?
      Serial.printf("DEVICE_VENDOR_STATE onrx=%lu rxtotal=%lu avail=%d\n",
                    static_cast<unsigned long>(vendorOnRxCount),
                    static_cast<unsigned long>(vendorRxTotal),
                    Vendor.available());
    }
  }

  // No poll-drain: the vendor echo is driven entirely by the onRx callback,
  // matching composite_cdc_msc_vendor.
  delay(1);
}
