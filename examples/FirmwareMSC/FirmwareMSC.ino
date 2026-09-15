#include "EspUsbDevice.h"

// Firmware update by dragging a file onto a drive.
//
// The board appears as a small USB drive. Copy a firmware .bin onto it and the
// device writes it straight into the spare OTA partition, verifies it, and
// restarts into it. Nothing to install on the host; the file manager is the
// update tool.
//
// The image is never held in RAM. Only the volume's metadata and a small
// scratch area live in `diskStorage`; everything past that is the OTA partition
// seen through a FAT. That is what lets a board with 320 KB of RAM accept a
// 1.25 MB image.
//
// Needs a partition scheme with two application partitions - the Arduino
// "Default" one has them, "Huge APP" does not.
//
// The host must write the file in ascending order, which every mainstream file
// manager does when copying onto an empty volume. A write that jumps backwards
// is refused rather than half-applied; see docs/ota-over-usb.md section 6.1.

EspUsbDevice device;
EspUsbDeviceMsc msc(device);

// Metadata plus scratch. The scratch area is what absorbs the files a host
// leaves behind - `System Volume Information` on Windows, `.fseventsd` and
// `.Spotlight-V100` on macOS - so do not make it tight.
static uint8_t diskStorage[16 * 1024];
EspUsbDeviceMscFirmwareDisk disk(diskStorage, sizeof(diskStorage));

static uint32_t reportedKiB = 0;

void setup()
{
  Serial.begin(115200);
  delay(1500);

  if (!disk.begin("ESPUSB"))
  {
    // Either there is no second application partition, or diskStorage is too
    // small for the volume this partition would need.
    Serial.println("DISK_BEGIN_FAILED - check the partition scheme has two app partitions");
    return;
  }
  Serial.printf("Update target: %s (%u bytes), volume %u sectors, %u of them in RAM\n",
                EspUsbDeviceFirmwareUpdate::targetLabel(),
                static_cast<unsigned>(EspUsbDeviceFirmwareUpdate::capacity()),
                static_cast<unsigned>(disk.blockCount()),
                static_cast<unsigned>(disk.ramSectorCount()));

  disk.addTextFile("README.TXT",
                   "Copy a firmware .bin onto this drive.\r\n"
                   "The board writes it to the spare OTA partition, checks it,\r\n"
                   "and restarts into it. Eject the drive if nothing happens.\r\n");

  disk.onProgress([](size_t written)
                  {
                    const uint32_t kiB = written / 1024;
                    if (kiB != reportedKiB)
                    {
                      reportedKiB = kiB;
                      Serial.printf("%u KiB\n", static_cast<unsigned>(kiB));
                    }
                  });

  disk.onComplete([]() -> bool
                  {
                    Serial.println("Verified - restarting into the new firmware");
                    return true;
                  });

  disk.onError([](esp_err_t error)
               {
                 // ESP_ERR_INVALID_STATE is the host writing out of order.
                 // Anything else came from the flash or the verification.
                 Serial.printf("Update failed: %s\n", esp_err_to_name(error));
                 reportedKiB = 0;
               });

  if (!disk.attach(msc))
  {
    Serial.println("DISK_ATTACH_FAILED");
    return;
  }
  if (!device.begin())
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("Ready - a drive should appear on the host");
}

void loop()
{
  // Confirm this image once the host has it mounted, cancelling any pending
  // bootloader rollback. A no-op unless the bootloader was built with
  // CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE.
  static bool confirmed = false;
  if (!confirmed && device.ready())
  {
    confirmed = true;
    EspUsbDeviceFirmwareUpdate::markValid();
  }
  delay(20);
}
