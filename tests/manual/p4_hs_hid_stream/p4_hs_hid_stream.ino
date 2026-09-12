// EspUsbDeviceHidVendor at the packet size high speed actually allows.
//
// HID is the only class no host OS needs a driver for, which is what makes its
// bandwidth worth having: an interrupt endpoint also *reserves* that bandwidth
// rather than taking what bulk leaves over. The 64 bytes people associate with
// HID is a full-speed fact - 64 every millisecond is 64 KB/s. A high-speed
// interrupt endpoint carries up to 1024 bytes every 125 us, so a 511-byte
// report at 8,000 reports/s is about 4 MB/s.
//
// The report size is the only thing this sketch varies, and it needs no
// build_opt.h on ESP32-P4, where CFG_TUD_HID_EP_BUFSIZE is 512 and so
// EspUsbDeviceHidVendor::maxReportSize() is 511.
//
// Host side: uv run --with pyusb python manual/p4_hs_hid_stream/p4_hs_hid_stream.py
#include "EspUsbDevice.h"

#if !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "p4_hs_hid_stream requires ESP32-P4"
#endif

EspUsbDevice device;
EspUsbDeviceHidVendor hid(device, 511);

static uint8_t report[511];
static uint32_t sequence = 0;
static uint32_t sent = 0;
static uint32_t lastReportMs = 0;

void setup()
{
  Serial.begin(115200);
  delay(1500);

  EspUsbDeviceConfig config;
  // Same device instance as the other experiments on this bench, so the usbipd
  // share already granted to it keeps working across reflashes.
  config.vid = 0x1209;
  config.pid = 0x0008;
  config.manufacturer = "Open Embedded Probe (TEST ONLY)";
  config.product = "EspUsbDevice P4 HS HID Stream";
  config.serialNumber = "E069-A";
  config.controller = EspUsbController::HighSpeed;

  if (!device.begin(config))
  {
    Serial.printf("DEVICE_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.printf("HID_READY report_size=%u max_report_size=%u descriptor_len=%u\n",
                hid.reportSize(), EspUsbDeviceHidVendor::maxReportSize(),
                hid.hidReportDescriptorLength());
}

void loop()
{
  // The payload carries its own sequence number, so the host can tell a report
  // it never received from one that arrived late.
  report[0] = static_cast<uint8_t>(sequence);
  report[1] = static_cast<uint8_t>(sequence >> 8);
  report[2] = static_cast<uint8_t>(sequence >> 16);
  report[3] = static_cast<uint8_t>(sequence >> 24);
  for (size_t i = 4; i < sizeof(report); i++)
  {
    report[i] = static_cast<uint8_t>(i);
  }
  if (hid.sendInput(report, sizeof(report), 0))
  {
    ++sequence;
    ++sent;
  }

  const uint32_t now = millis();
  if (now - lastReportMs >= 1000)
  {
    Serial.printf("HID_SENT reports=%lu per_second=%lu\n",
                  static_cast<unsigned long>(sequence),
                  static_cast<unsigned long>(sent));
    sent = 0;
    lastReportMs = now;
  }
}
