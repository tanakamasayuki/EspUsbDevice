#include <EspUsbDevice.h>
#include <atomic>
#include <string.h>

EspUsbDevice device;
EspUsbDeviceCdcSerial console(device, "disk statistics");
EspUsbDeviceMsc disk(device);

// A raw, initially zero-filled block device, with no filesystem or flash use.
// Its contents are lost on reset. Supply an application storage backend here
// when a formatted or persistent disk is needed.
constexpr uint32_t blockBytes = 512, blockCount = 16;
uint8_t storage[blockBytes * blockCount];
std::atomic<uint32_t> reads{0}, writes{0};

bool inRange(uint32_t lba, uint32_t offset, uint32_t bytes) {
  return lba < blockCount && offset < blockBytes &&
         bytes <= sizeof(storage) - (size_t(lba) * blockBytes + offset);
}

void setup() {
  Serial.begin(115200); // Separate debug connection.
  disk.onRead([](uint32_t lba, uint32_t offset, void *data, uint32_t bytes) -> int32_t {
    if (!inRange(lba, offset, bytes)) return -1;
    memcpy(data, storage + size_t(lba) * blockBytes + offset, bytes);
    ++reads;
    return int32_t(bytes);
  });
  disk.onWrite([](uint32_t lba, uint32_t offset, uint8_t *data, uint32_t bytes) -> int32_t {
    if (!inRange(lba, offset, bytes)) return -1;
    memcpy(storage + size_t(lba) * blockBytes + offset, data, bytes);
    ++writes;
    return int32_t(bytes);
  });
  disk.vendorID("ESPUSB");
  disk.productID("RAM blocks");
  disk.mediaPresent(true);

  EspUsbMscConfig blockConfig;
  blockConfig.transferBytes = 512; // One staging buffer; valid at FS and HS.
  esp_err_t error = disk.configure(blockConfig);
  if (error != ESP_OK) { Serial.printf("MSC configure: %d\n", int(error)); return; }
  if (!disk.begin(blockCount, blockBytes)) {
    Serial.printf("MSC geometry: %d\n", int(disk.startError())); return;
  }

  EspUsbBulkStreamConfig serialConfig;
  serialConfig.rxCapacity = 512;
  serialConfig.txCapacity = 256;
  serialConfig.transferBytes = 64;
  error = console.configure(serialConfig);
  if (error != ESP_OK) { Serial.printf("CDC configure: %d\n", int(error)); return; }

  EspUsbDeviceConfig identity;
  identity.product = "RAM blocks and console";
  if (!device.begin(identity)) Serial.printf("USB begin: %d\n", int(device.lastError()));
}

void loop() {
  if (!console.connected()) { delay(1); return; }
  bool requested = false;
  while (console.available()) { requested |= console.read() == '?'; }
  static uint32_t last = 0;
  const uint32_t now = millis();
  if (requested || uint32_t(now - last) >= 1000) {
    last = now;
    // Counts are storage callback calls, not USB packet or byte counts.
    console.print("reads="); console.print(reads.load());
    console.print(" writes="); console.println(writes.load());
  }
  delay(1);
}
