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

// -DHID_STREAM_COMPOSITE=1 adds a second HID class, which merges both into one
// interface with a Report ID each. That is the only way to exercise the
// host-to-device path on a merged descriptor against a conforming host:
// TinyUSB reports interrupt OUT data with a report ID of 0 because its HID
// driver does not parse report descriptors, so the ID has to come from the
// payload's first byte - which a conforming host puts there, and EspUsbHost
// does not, so the loopback tests cannot reach this.
#ifndef HID_STREAM_COMPOSITE
#define HID_STREAM_COMPOSITE 0
#endif

EspUsbDevice device;
EspUsbDeviceHidVendor hid(device, HID_STREAM_COMPOSITE ? 15 : 511);
#if HID_STREAM_COMPOSITE
EspUsbDeviceHidKeyboard keyboard(device);
#endif

static uint8_t report[511];
static volatile uint32_t outputReports = 0;
// Echoed back as an input report so the check needs no serial connection: on a
// bench where the device connector reaches the PC through usbip, opening the
// board's serial port resets the chip and drops the attachment.
static volatile bool echoPending = false;
static uint8_t echoPayload[16];
static volatile uint8_t echoLength = 0;
static volatile uint8_t echoReportId = 0;
static volatile uint32_t controlSetReports = 0;
static volatile uint8_t lastControlReportId = 0;
static uint32_t sequence = 0;
static uint32_t sent = 0;
static uint32_t lastReportMs = 0;

void setup()
{
  Serial.begin(115200);
  delay(1500);

  device.onAnyControlRequest(
      [](const EspUsbDeviceControlRequestInfo &info)
      {
        if (info.stage == ESP_USB_DEVICE_CONTROL_STAGE_ACK &&
            (info.bmRequestType & 0x60) == 0x20 && info.bRequest == 0x09)
        {
          ++controlSetReports;
          lastControlReportId = static_cast<uint8_t>(info.wValue & 0xff);
        }
      });

  hid.onOutputReport([](const EspUsbDeviceHidReport &r)
                     {
                       ++outputReports;
                       echoReportId = r.reportId;
                       echoLength = r.length < sizeof(echoPayload)
                                        ? static_cast<uint8_t>(r.length)
                                        : static_cast<uint8_t>(sizeof(echoPayload));
                       if (r.data)
                       {
                         memcpy(echoPayload, r.data, echoLength);
                       }
                       echoPending = true;
                       Serial.printf("HID_OUTPUT id=%u len=%u first=%02x\n",
                                     r.reportId, r.length,
                                     r.length > 0 && r.data ? r.data[0] : 0);
                     });

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
  Serial.printf("HID_READY report_size=%u max_report_size=%u descriptor_len=%u composite=%d\n",
                hid.reportSize(), EspUsbDeviceHidVendor::maxReportSize(),
                device.hidReportDescriptorLength(0), HID_STREAM_COMPOSITE);
}

void loop()
{
#if HID_STREAM_COMPOSITE
  // The point here is the host-to-device path, so keep a slow heartbeat rather
  // than saturating the endpoint, and answer whatever the host sent by echoing
  // it straight back as an input report.
  memset(report, 0, hid.reportSize());
  if (echoPending)
  {
    // A two-byte marker the heartbeat below cannot produce, so the host cannot
    // mistake one for the other.
    report[0] = 'E';
    report[1] = 'C';
    report[2] = echoReportId;
    report[3] = echoLength;
    const uint8_t room = static_cast<uint8_t>(hid.reportSize() - 4);
    memcpy(&report[4], echoPayload, echoLength < room ? echoLength : room);
    echoPending = false;
  }
  else
  {
    report[4] = static_cast<uint8_t>(sequence);
    // Carried in every heartbeat so the host can tell "no report arrived" from
    // "a report arrived and was routed nowhere", without a serial connection.
    report[5] = static_cast<uint8_t>(outputReports);
    report[6] = static_cast<uint8_t>(controlSetReports);
    report[7] = static_cast<uint8_t>(lastControlReportId);
  }
  if (hid.sendInput(report, hid.reportSize(), 0))
  {
    ++sequence;
  }
  delay(20);
  return;
#endif
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
