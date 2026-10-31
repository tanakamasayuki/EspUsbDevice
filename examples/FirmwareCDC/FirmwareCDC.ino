// Receive a length-prefixed firmware image over a CDC serial port:
//   host "FW <size>\n" -> device "READY\n" -> <size> bytes -> device "OK\n"
// Host script: firmware_cdc.py. Details: README.md / README.ja.md.
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceCdcSerial port(device, "Firmware");
EspUsbDeviceFirmwareUpdate update;

size_t expected = 0, received = 0;
char command[32];
uint8_t commandLength = 0;
uint32_t reportedKiB = 0;

void fail(const char *reason) {
  update.abort();
  expected = received = 0;
  commandLength = 0;
  port.printf("ERR %s\n", reason);
  port.flush();
  Serial.printf("Update failed: %s\n", reason);
}

// The "FW <size>" line, a byte at a time. Nothing here is time critical: the
// image has not started yet.
void readCommand() {
  while (port.available() > 0) {
    const int byte = port.read();
    if (byte < 0) return;
    if (byte != '\n' && byte != '\r') {
      if (commandLength < sizeof(command) - 1) command[commandLength++] = char(byte);
      continue;
    }
    if (commandLength == 0) continue;
    command[commandLength] = '\0';
    commandLength = 0;

    unsigned long size = 0;
    if (sscanf(command, "FW %lu", &size) != 1 || size == 0) { fail("expected FW <size>"); return; }
    if (!EspUsbDeviceFirmwareUpdate::available()) { fail("no OTA partition"); return; }
    // With the real size, an image too big for the partition is refused
    // before the host sends a byte of it.
    if (!update.begin(size)) { fail(update.lastErrorName()); return; }
    expected = size;
    received = reportedKiB = 0;
    Serial.printf("Receiving %lu bytes into %s\n", size, EspUsbDeviceFirmwareUpdate::targetLabel());
    port.print("READY\n");
    port.flush();
    return;
  }
}

// The image itself. This runs in loop(), not in a USB callback: flash erase
// takes milliseconds, and on the USB worker that time is taken from the whole
// device. While loop() writes, the RX ring fills and the host is NAKed.
void readImage() {
  static uint8_t buffer[1024];
  while (port.available() > 0 && received < expected) {
    const size_t want = min(expected - received, sizeof(buffer));
    const size_t got = port.read(buffer, want);
    if (got == 0) return;
    if (!update.write(buffer, got)) { fail(update.lastErrorName()); return; }
    received += got;
    const uint32_t kiB = received / 1024;
    if (kiB != reportedKiB) { reportedKiB = kiB; Serial.printf("%u KiB\n", unsigned(kiB)); }
  }
  if (received < expected) return;

  // end() checks the image header, length and checksum before it moves the
  // boot partition, so a truncated or corrupt upload fails here, not at boot.
  if (!update.end()) { fail(update.lastErrorName()); return; }
  expected = 0;
  port.print("OK\n");
  port.flush(); // Waits (up to the TX timeout) for the reply to leave.
  Serial.println("Verified - restarting into the new firmware");
  delay(100);   // Let the host read the reply before the port disappears.
  esp_restart();
}

void setup() {
  Serial.begin(115200); // Separate debug connection.
  delay(1500);

  if (!EspUsbDeviceFirmwareUpdate::available()) {
    Serial.println("NO_OTA_PARTITION - select a partition scheme with two app partitions");
  } else {
    Serial.printf("Update target: %s (%u bytes)\n", EspUsbDeviceFirmwareUpdate::targetLabel(),
                  unsigned(EspUsbDeviceFirmwareUpdate::capacity()));
  }

  EspUsbBulkStreamConfig serialConfig;
  // A deep RX ring keeps USB moving while loop() is busy erasing flash.
  serialConfig.rxCapacity = 8192;
  serialConfig.txCapacity = 256; // Only short replies go back.
  serialConfig.transferBytes = 64;
  esp_err_t error = port.configure(serialConfig);
  if (error != ESP_OK) { Serial.printf("CDC_CONFIGURE_FAILED %s\n", esp_err_to_name(error)); return; }

  if (!device.begin()) { Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName()); return; }
  Serial.println("Ready. python3 firmware_cdc.py <port> <firmware.bin>");
}

void loop() {
  if (expected == 0) readCommand();
  else readImage();

  // Confirm this image once the host has configured the device, cancelling a
  // pending bootloader rollback (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE only).
  static bool confirmed = false;
  if (!confirmed && device.ready()) {
    confirmed = true;
    EspUsbDeviceFirmwareUpdate::markValid();
  }
  delay(1);
}
