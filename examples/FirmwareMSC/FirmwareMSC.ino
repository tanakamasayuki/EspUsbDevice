// Firmware update by copying a .bin (or .uf2) onto a USB drive. The drive's
// data region is the spare OTA partition; the image is never held in RAM.
// Partition requirements and host-side steps: README.md / README.ja.md.
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceMsc msc(device);

// Volume metadata plus a scratch area, not the image. The scratch area absorbs
// what the host leaves behind (`System Volume Information`, `.fseventsd`,
// `.Spotlight-V100`), so do not make it tight: 16 KiB is comfortable, 8 KiB the floor.
static uint8_t diskStorage[16 * 1024];
EspUsbDeviceMscFirmwareDisk disk(diskStorage, sizeof(diskStorage));

volatile uint32_t reportedKiB = 0;

void setup() {
  Serial.begin(115200); // Separate debug connection.
  delay(1500);

  // Lays the volume out over the partition this firmware would update.
  if (!disk.begin("ESPUSB")) {
    // No second app partition, or diskStorage too small for the metadata.
    Serial.println("DISK_BEGIN_FAILED - check the partition scheme has two app partitions");
    return;
  }
  Serial.printf("Update target: %s (%u bytes), volume %u sectors, %u of them in RAM\n",
                EspUsbDeviceFirmwareUpdate::targetLabel(),
                unsigned(EspUsbDeviceFirmwareUpdate::capacity()),
                unsigned(disk.blockCount()), unsigned(disk.ramSectorCount()));
  Serial.printf("UF2 family ID for uf2conv.py: 0x%08lx\n",
                (unsigned long)EspUsbDeviceMscFirmwareDisk::uf2FamilyId());

  disk.addTextFile("README.TXT",
                   "Copy a firmware .bin onto this drive.\r\n"
                   "The board writes it to the spare OTA partition, checks it,\r\n"
                   "and restarts into it. Eject the drive if nothing happens.\r\n");

  // The callbacks below run on the USB worker: keep them short.
  disk.onProgress([](size_t written) {
    const uint32_t kiB = written / 1024;
    if (kiB != reportedKiB) { reportedKiB = kiB; Serial.printf("%u KiB\n", unsigned(kiB)); }
  });
  // The image is written and verified and the boot partition has moved.
  // Returning false would refuse it and put the boot partition back.
  disk.onComplete([]() -> bool {
    Serial.println("Verified - restarting into the new firmware");
    return true;
  });
  // ESP_ERR_INVALID_STATE: the host wrote a raw .bin out of order. Anything
  // else came from the flash or from image verification.
  disk.onError([](esp_err_t error) {
    Serial.printf("Update failed: %s\n", esp_err_to_name(error));
    reportedKiB = 0;
  });

  EspUsbMscConfig mscConfig;
  mscConfig.name = "Firmware drive";
  // Fewer, larger USB transfers per flash write; a multiple of 512 is valid
  // at FS and at HS (ESP32-P4).
  mscConfig.transferBytes = 4096;
  esp_err_t error = msc.configure(mscConfig);
  if (error != ESP_OK) { Serial.printf("MSC_CONFIGURE_FAILED %s\n", esp_err_to_name(error)); return; }

  // Unlike the RAM disks, this also marks the medium present and writable.
  if (!disk.attach(msc)) { Serial.println("DISK_ATTACH_FAILED"); return; }

  if (!device.begin()) {
    Serial.printf("USB_BEGIN_FAILED %s msc=%s\n", device.lastErrorName(),
                  esp_err_to_name(msc.startError()));
    return;
  }
  Serial.println("Ready - a drive should appear on the host");
}

void loop() {
  // Confirm this image once the host has configured the device, cancelling a
  // pending bootloader rollback. A no-op unless the bootloader was built with
  // CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE. Not an authenticity check.
  static bool confirmed = false;
  if (!confirmed && device.ready()) {
    confirmed = true;
    EspUsbDeviceFirmwareUpdate::markValid();
  }
  delay(20);
}
