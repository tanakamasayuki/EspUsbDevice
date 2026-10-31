// Firmware update over a vendor interface: control requests carry the
// commands, bulk OUT carries the image. Host script: firmware_vendor.py.
// Protocol and host-side steps: README.md / README.ja.md.
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
EspUsbDeviceFirmwareUpdate update;

constexpr uint8_t REQUEST_START = 0x01;  // wValue = size low 16, wIndex = size high 16
constexpr uint8_t REQUEST_COMMIT = 0x02;
constexpr uint8_t REQUEST_ABORT = 0x03;
constexpr uint8_t REQUEST_STATUS = 0x04; // IN, 8 bytes

// Set by control requests on the USB worker, acted on in loop(). Nothing on
// the worker may touch flash: an erase there stalls the whole device.
volatile bool startRequested = false, commitRequested = false, abortRequested = false;
volatile uint32_t requestedSize = 0;
volatile uint32_t lastError = 0;
uint32_t reportedKiB = 0;

// [0] active, [1..3] last esp_err_t (low 24 bits, little endian),
// [4..7] bytes written (little endian).
void statusReply(uint8_t reply[8]) {
  const uint32_t error = lastError;
  const uint32_t written = uint32_t(update.written());
  reply[0] = update.active() ? 1 : 0;
  for (int i = 0; i < 3; ++i) reply[1 + i] = uint8_t(error >> (8 * i));
  for (int i = 0; i < 4; ++i) reply[4 + i] = uint8_t(written >> (8 * i));
}

void setup() {
  Serial.begin(115200); // Separate debug connection.
  delay(1500);

  if (!EspUsbDeviceFirmwareUpdate::available()) {
    Serial.println("NO_OTA_PARTITION - select a partition scheme with two app partitions");
  }

  EspUsbVendorConfig vendorConfig;
  // A deep RX ring keeps bulk OUT moving while loop() erases flash; when it is
  // full the host is NAKed, which slows it down rather than losing data.
  vendorConfig.rxCapacity = 16384;
  vendorConfig.txCapacity = 0;   // Replies go over EP0; no bulk IN data.
  vendorConfig.transferBytes = 0;
  esp_err_t error = vendor.configure(vendorConfig);
  if (error != ESP_OK) { Serial.printf("VENDOR_CONFIGURE_FAILED %s\n", esp_err_to_name(error)); return; }

  vendor.onControlRequest([](const EspUsbDeviceVendorControlRequest &request) -> bool {
    if ((request.bmRequestType & 0x60) != 0x40) return false; // Vendor requests only.
    // The decision is made at SETUP; DATA and ACK finish what was decided.
    if (request.stage != ESP_USB_DEVICE_CONTROL_STAGE_SETUP) return true;
    switch (request.bRequest) {
    case REQUEST_START:
      requestedSize = uint32_t(request.wValue) | (uint32_t(request.wIndex) << 16);
      startRequested = true;
      return vendor.sendControlResponse(request);
    case REQUEST_COMMIT:
      commitRequested = true;
      return vendor.sendControlResponse(request);
    case REQUEST_ABORT:
      abortRequested = true;
      return vendor.sendControlResponse(request);
    case REQUEST_STATUS: {
      static uint8_t reply[8]; // Must outlive this callback.
      statusReply(reply);
      return vendor.sendControlResponse(request, reply, sizeof(reply));
    }
    default:
      return false; // Stall anything else.
    }
  });

  if (!device.begin()) { Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName()); return; }
  Serial.println("Ready. python3 firmware_vendor.py <firmware.bin>");
}

// Everything that touches flash runs here, on the sketch's task.
void drainBulk() {
  static uint8_t buffer[1024];
  while (vendor.available() > 0) {
    const size_t got = vendor.read(buffer, sizeof(buffer));
    if (got == 0) return;
    if (!update.active()) continue; // Data with no START in front of it.
    if (!update.write(buffer, got)) {
      lastError = uint32_t(update.lastError());
      Serial.printf("Write failed: %s\n", update.lastErrorName());
      return;
    }
    const uint32_t kiB = update.written() / 1024;
    if (kiB != reportedKiB) { reportedKiB = kiB; Serial.printf("%u KiB\n", unsigned(kiB)); }
  }
}

void loop() {
  if (startRequested) {
    startRequested = false;
    reportedKiB = 0;
    lastError = 0;
    const size_t expected = requestedSize;
    if (!update.begin(expected)) {
      lastError = uint32_t(update.lastError());
      Serial.printf("Start failed: %s\n", update.lastErrorName());
    } else {
      Serial.printf("Receiving %u bytes into %s\n", unsigned(expected),
                    EspUsbDeviceFirmwareUpdate::targetLabel());
    }
  }
  if (abortRequested) {
    abortRequested = false;
    update.abort();
    Serial.println("Aborted");
  }

  drainBulk();

  if (commitRequested) {
    commitRequested = false;
    // end() verifies the image before moving the boot partition.
    if (update.end()) {
      Serial.println("Verified - restarting into the new firmware");
      delay(100); // Let the control transfer's status stage reach the host.
      esp_restart();
    }
    lastError = uint32_t(update.lastError());
    Serial.printf("Commit failed: %s\n", update.lastErrorName());
  }

  // Confirm this image once the host has configured the device, cancelling a
  // pending bootloader rollback (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE only).
  static bool confirmed = false;
  if (!confirmed && device.ready()) {
    confirmed = true;
    EspUsbDeviceFirmwareUpdate::markValid();
  }
  delay(1);
}
