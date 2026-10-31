// Expose 16 raw RAM blocks as USB mass storage (no filesystem).
// Wiring, host-side steps and adaptation notes: README.md / README.ja.md.
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceMsc msc(device);

// 16 x 512 bytes of RAM, lost on reset. A SCSI/block bring-up target, not a
// FAT image: the PC may offer to format it. MSCFatRamDisk has real files.
constexpr uint32_t blockCount = 16;
constexpr uint16_t blockBytes = 512;
uint8_t storage[blockCount * blockBytes];
EspUsbDeviceMscRamDisk disk(storage, blockCount, blockBytes);

void prepareDisk() {
  disk.clear();
  // A boot-sector signature, enough to recognise block 0 from the host.
  disk.writeByte(0, 510, 0x55);
  disk.writeByte(0, 511, 0xaa);
}

void setup() {
  Serial.begin(115200); // Separate debug connection.
  delay(1500);
  prepareDisk();

  EspUsbMscConfig mscConfig;
  mscConfig.name = "RAM disk";
  mscConfig.transferBytes = 512; // One staging buffer; valid at FS and HS.
  esp_err_t error = msc.configure(mscConfig);
  if (error != ESP_OK) { Serial.printf("MSC_CONFIGURE_FAILED %s\n", esp_err_to_name(error)); return; }

  // INQUIRY strings: 8 / 16 / 4 characters at most.
  msc.vendorID("ESP32");
  msc.productID("MSC RAM DISK");
  msc.productRevision("1.0");
  msc.isWritable(true);
  // Runs on the USB worker: log and return, never block here.
  msc.onStartStop([](uint8_t powerCondition, bool start, bool loadEject) {
    Serial.printf("MSC_START_STOP pc=%u start=%u eject=%u\n",
                  powerCondition, start ? 1 : 0, loadEject ? 1 : 0);
    return true;
  });

  // Installs the read/write callbacks and the geometry. It does not touch the
  // medium state, so report the disk as inserted explicitly.
  if (!disk.attach(msc)) { Serial.println("MSC_ATTACH_FAILED"); return; }
  msc.mediaPresent(true);

  EspUsbDeviceConfig identity;
  identity.vid = 0x303a;
  identity.pid = 0x4007;
  identity.manufacturer = "EspUsb";
  identity.product = "EspUsbDevice MSC";
  identity.serialNumber = "espusb-msc";
  if (!device.begin(identity)) {
    Serial.printf("USB_BEGIN_FAILED %s msc=%s\n", device.lastErrorName(),
                  esp_err_to_name(msc.startError()));
    return;
  }
  Serial.printf("USB MSC RAM disk ready blocks=%lu block_size=%u bytes=%u staging=%u\n",
                (unsigned long)disk.blockCount(), disk.blockSize(),
                unsigned(disk.byteSize()), unsigned(msc.transferCapacity()));
}

void loop() {
  delay(1000);
}
