// Present a small FAT12 RAM disk, then read back CONFIG.TXT after the host
// ejects it. Host-side steps and limits: README.md / README.ja.md.
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceMsc msc(device);

// 64 KiB fits the ESP32-S2's internal DRAM next to the USB buffers. Larger
// disks can live in PSRAM (the image is only memcpy'd, never DMA'd); persistent
// or much larger media belong on SD (see MSCSdCard).
static uint8_t storage[64 * 1024];
EspUsbDeviceMscFatRamDisk disk(storage, sizeof(storage));

// Set on the USB worker by the eject callback, acted on in loop().
volatile bool scanRequested = false;

void setup() {
  Serial.begin(115200); // Separate debug connection.
  delay(1500);

  // Build the volume before USB exposes it; this rewrites the whole buffer.
  if (!disk.format("ESPUSB")) { Serial.println("FAT_FORMAT_FAILED"); return; }
  disk.addTextFile("README.TXT",
                   "EspUsbDevice FAT RAM disk\r\n"
                   "\r\n"
                   "Copy CONFIG.TXT here, eject the drive, and watch Serial.\r\n");
  // Runs on the USB worker: only record the request. The host may still be
  // writing until it ejects, so the FAT is read in loop() afterwards.
  disk.onEject([]() { scanRequested = true; });

  EspUsbMscConfig mscConfig;
  mscConfig.name = "FAT RAM disk";
  mscConfig.transferBytes = 512; // Valid at FS and HS.
  esp_err_t error = msc.configure(mscConfig);
  if (error != ESP_OK) { Serial.printf("MSC_CONFIGURE_FAILED %s\n", esp_err_to_name(error)); return; }
  msc.vendorID("ESP32");
  msc.productID("FAT RAM DISK");
  msc.productRevision("1.0");
  msc.isWritable(true);

  // Installs read/write and START STOP UNIT; the medium state is left to us.
  if (!disk.attach(msc)) { Serial.println("MSC_ATTACH_FAILED"); return; }
  msc.mediaPresent(true);

  EspUsbDeviceConfig identity;
  identity.vid = 0x303a;
  identity.pid = 0x4008;
  identity.manufacturer = "EspUsb";
  identity.product = "EspUsbDevice FAT RAM Disk";
  identity.serialNumber = "espusb-fat-ramdisk";
  if (!device.begin(identity)) {
    Serial.printf("USB_BEGIN_FAILED %s msc=%s\n", device.lastErrorName(),
                  esp_err_to_name(msc.startError()));
    return;
  }
  Serial.printf("USB FAT RAM disk ready blocks=%lu bytes=%u\n",
                (unsigned long)disk.blockCount(), unsigned(disk.byteSize()));
}

void loop() {
  if (!scanRequested) { delay(10); return; }
  scanRequested = false;
  Serial.println("MSC_EJECT");

  if (!disk.exists("CONFIG.TXT")) { Serial.println("CONFIG_NOT_FOUND"); return; }
  // Shows a bounded prefix; a larger file is not truncated on the disk.
  static uint8_t buffer[256];
  const size_t size = disk.fileSize("CONFIG.TXT");
  const size_t readSize = disk.readFile("CONFIG.TXT", buffer, min(size, sizeof(buffer) - 1));
  buffer[readSize] = '\0';
  Serial.printf("CONFIG_SIZE %u\n", unsigned(size));
  Serial.printf("CONFIG_BEGIN\n%s\nCONFIG_END\n", reinterpret_cast<char *>(buffer));
}
