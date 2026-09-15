#include "EspUsbDevice.h"

// Firmware update over a vendor-specific interface: control requests for the
// commands, a bulk OUT endpoint for the image.
//
//   python3 firmware_vendor.py build/FirmwareVendor.ino.bin
//
// This is the fastest route in the library. Bulk OUT on an ESP32-P4 high-speed
// link moves an image in a fraction of the time DFU's EP0 transfers take, and
// nothing about the protocol is negotiated - it is three control requests and a
// stream. The cost is that the host side is yours to write and to ship;
// examples/FirmwareDFU is the same job with a standard tool instead.
//
// The protocol:
//
//   control OUT  0x40 0x01  wValue=size_lo wIndex=size_hi   start
//   bulk OUT                                                 the image
//   control OUT  0x40 0x02                                   commit and restart
//   control OUT  0x40 0x03                                   abort
//   control IN   0xc0 0x04  -> 8 bytes                       state and progress
//
// Needs a partition scheme with two application partitions.

EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
EspUsbDeviceFirmwareUpdate update;

static constexpr uint8_t REQUEST_START = 0x01;
static constexpr uint8_t REQUEST_COMMIT = 0x02;
static constexpr uint8_t REQUEST_ABORT = 0x03;
static constexpr uint8_t REQUEST_STATUS = 0x04;

// State the control requests set and loop() acts on. Control requests and
// onRx() both run on the usbd task, so nothing here may touch flash.
static volatile bool startRequested = false;
static volatile bool commitRequested = false;
static volatile bool abortRequested = false;
static volatile uint32_t requestedSize = 0;
static volatile uint32_t lastError = 0;

static size_t expected = 0;
static uint32_t reportedKiB = 0;

static void statusReply(uint8_t reply[8])
{
  const uint32_t written = static_cast<uint32_t>(update.written());
  reply[0] = update.active() ? 1 : 0;
  reply[1] = static_cast<uint8_t>(lastError & 0xff);
  reply[2] = 0;
  reply[3] = 0;
  reply[4] = static_cast<uint8_t>(written & 0xff);
  reply[5] = static_cast<uint8_t>((written >> 8) & 0xff);
  reply[6] = static_cast<uint8_t>((written >> 16) & 0xff);
  reply[7] = static_cast<uint8_t>((written >> 24) & 0xff);
}

void setup()
{
  Serial.begin(115200);
  delay(1500);

  if (!EspUsbDeviceFirmwareUpdate::available())
  {
    Serial.println("NO_OTA_PARTITION - select a partition scheme with two app partitions");
  }

  vendor.onControlRequest(
      [](const EspUsbDeviceVendorControlRequest &request) -> bool
      {
        // Only the setup stage carries a decision; the data and ack stages are
        // the stack finishing what was decided here.
        if (request.stage != ESP_USB_DEVICE_CONTROL_STAGE_SETUP)
        {
          return true;
        }
        switch (request.bRequest)
        {
        case REQUEST_START:
          requestedSize = static_cast<uint32_t>(request.wValue) |
                          (static_cast<uint32_t>(request.wIndex) << 16);
          startRequested = true;
          return vendor.sendControlResponse(request);
        case REQUEST_COMMIT:
          commitRequested = true;
          return vendor.sendControlResponse(request);
        case REQUEST_ABORT:
          abortRequested = true;
          return vendor.sendControlResponse(request);
        case REQUEST_STATUS:
        {
          static uint8_t reply[8];
          statusReply(reply);
          return vendor.sendControlResponse(request, reply, sizeof(reply));
        }
        default:
          return false; // stall anything else
        }
      });

  if (!device.begin())
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("Ready. python3 firmware_vendor.py <firmware.bin>");
}

// Everything that touches flash happens here, on the sketch's own task. The
// bulk endpoint NAKs while this runs, which is the host slowing down rather
// than the device stalling.
static void drainBulk()
{
  uint8_t buffer[512];
  while (vendor.available() > 0)
  {
    const size_t got = vendor.read(buffer, sizeof(buffer));
    if (got == 0)
    {
      return;
    }
    if (!update.active())
    {
      continue; // data with no START in front of it
    }
    if (!update.write(buffer, got))
    {
      lastError = static_cast<uint32_t>(update.lastError());
      Serial.printf("Write failed: %s\n", update.lastErrorName());
      return;
    }
    const uint32_t kiB = update.written() / 1024;
    if (kiB != reportedKiB)
    {
      reportedKiB = kiB;
      Serial.printf("%u KiB\n", static_cast<unsigned>(kiB));
    }
  }
}

void loop()
{
  if (startRequested)
  {
    startRequested = false;
    expected = requestedSize;
    reportedKiB = 0;
    lastError = 0;
    if (!update.begin(expected))
    {
      lastError = static_cast<uint32_t>(update.lastError());
      Serial.printf("Start failed: %s\n", update.lastErrorName());
    }
    else
    {
      Serial.printf("Receiving %u bytes into %s\n", static_cast<unsigned>(expected),
                    EspUsbDeviceFirmwareUpdate::targetLabel());
    }
  }

  if (abortRequested)
  {
    abortRequested = false;
    update.abort();
    Serial.println("Aborted");
  }

  drainBulk();

  if (commitRequested)
  {
    commitRequested = false;
    if (update.end())
    {
      Serial.println("Verified - restarting into the new firmware");
      delay(100); // let the control transfer's ack reach the host
      esp_restart();
    }
    lastError = static_cast<uint32_t>(update.lastError());
    Serial.printf("Commit failed: %s\n", update.lastErrorName());
  }

  static bool confirmed = false;
  if (!confirmed && device.ready())
  {
    confirmed = true;
    EspUsbDeviceFirmwareUpdate::markValid();
  }
  delay(1);
}
