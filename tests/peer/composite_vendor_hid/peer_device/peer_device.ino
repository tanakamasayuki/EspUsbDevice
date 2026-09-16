#include "EspUsbDevice.h"

// Composite device: bulk Vendor registered BEFORE the HID keyboard.
//
// composite_hid_vendor has the same two functions the other way round, and the
// descriptors are byte-for-byte the same either way because HID is always
// emitted first. What changes is the class table: here the keyboard sits in
// slot 1, and the library used to hand TinyUSB's HID instance number (always 0)
// straight to that table. hidReportDescriptor(0) landed on the Vendor class and
// returned nullptr, so GET_DESCRIPTOR(Report) was neither answered nor stalled
// and the host timed out; tapKey() went to tud_hid_n_report(1), an instance this
// build does not have. On Windows 11 that read as HidUsb "Code 10" some 6 s
// after arrival with the WinUSB sibling held behind it (measured, see
// tests/manual/windows_device_guid). Pairs with composite_vendor_hid.ino (host).

EspUsbDevice device;
EspUsbDeviceVendor Vendor(device);
EspUsbDeviceHidKeyboard keyboard(device);

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
  config.pid = 0x4026;
  config.manufacturer = "EspUsbDevice";
  config.product = "EspUsbDevice Vendor+HID";
  config.serialNumber = "espusb-vendor-hid";

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
