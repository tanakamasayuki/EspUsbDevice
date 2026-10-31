// Update the application with `dfu-util -D` while the sketch keeps running as
// a USB keyboard. DFU uses one interface and no endpoints (EP0 only).
// Partition requirements and host-side steps: README.md / README.ja.md.
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device); // Present only to show DFU sharing a device.
EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Download, "Firmware");

volatile uint32_t reportedKiB = 0;

void setup() {
  Serial.begin(115200); // Separate debug connection.
  delay(1500);

  if (!EspUsbDeviceFirmwareUpdate::available()) {
    // Under a single-app scheme every upload would fail on its last block;
    // say so once instead.
    Serial.println("NO_OTA_PARTITION - select a partition scheme with two app partitions");
  } else {
    Serial.printf("Update target: %s (%u bytes)\n", EspUsbDeviceFirmwareUpdate::targetLabel(),
                  unsigned(EspUsbDeviceFirmwareUpdate::capacity()));
  }

  EspUsbDfuConfig dfuConfig;
  // Block size, declared to the host as wTransferSize. 4096 matches the flash
  // sector and needs a quarter of the requests of the 1024-byte default.
  dfuConfig.transferBytes = 4096;
  esp_err_t error = dfu.configure(dfuConfig);
  if (error != ESP_OK) { Serial.printf("DFU_CONFIGURE_FAILED %s\n", esp_err_to_name(error)); return; }

  // These run on the USB worker, once per block: keep them short. DFU 1.1
  // never tells the device the image length, so there is no percentage.
  dfu.onProgress([](size_t written) {
    const uint32_t kiB = written / 1024;
    if (kiB != reportedKiB) { reportedKiB = kiB; Serial.printf("DFU %u KiB\n", unsigned(kiB)); }
  });
  // The image is written and verified and the boot partition has moved.
  // Returning false would put it back.
  dfu.onComplete([]() -> bool {
    Serial.println("DFU complete - restarting into the new firmware");
    return true;
  });
  // Write (3, errWRITE): refused before anything was written, usually a file
  // that is not an ESP application. Verify (7, errVERIFY): failed the check at
  // the end. dfu-util prints the same number.
  dfu.onError([](EspUsbDfuStatus status) {
    Serial.printf("DFU failed, status %u\n", unsigned(status));
    reportedKiB = 0;
  });
  // dfu.restartWhenComplete(false) would leave the restart to the sketch.

  if (!device.begin()) {
    Serial.printf("USB_BEGIN_FAILED %s dfu=%s\n", device.lastErrorName(),
                  esp_err_to_name(dfu.startError()));
    return;
  }
  Serial.printf("Ready (wTransferSize=%u). dfu-util -D <firmware.bin>\n", dfu.transferSize());
}

void loop() {
  // Confirm this image once the host has configured the device, cancelling a
  // pending bootloader rollback (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE only).
  static bool confirmed = false;
  if (!confirmed && device.ready()) {
    confirmed = true;
    EspUsbDeviceFirmwareUpdate::markValid();
  }
  delay(20);
}
