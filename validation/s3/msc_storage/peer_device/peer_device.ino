#include <Arduino.h>
#include <EspUsbDevice.h>
#include <esp_heap_caps.h>
#include <atomic>
EspUsbDevice device;
EspUsbDeviceMsc disk(device);
uint8_t storage[32 * 512];
alignas(64) uint8_t external[4096 + 128];
std::atomic<unsigned> reads{0}, writes{0}, stops{0};
std::atomic<bool> ioFail{false}, offsets{true};
EspUsbDeviceCdcSerial *serialPort = nullptr;
EspUsbDeviceMsc *second = nullptr;
bool readOnly = false;
unsigned sectorBytes = 512;
void callbacks(bool readonly) {
  disk.onRead([](uint32_t lba, uint32_t offset, void *buffer, uint32_t bytes) -> int32_t {
    ++reads; offsets.store(offsets.load() && offset < sectorBytes && lba < 32 && bytes <= sizeof(storage) - (lba * sectorBytes + offset));
    if (ioFail.load() || lba >= 32 || offset >= sectorBytes || bytes > sizeof(storage) - (lba * sectorBytes + offset)) return -1;
    memcpy(buffer, storage + lba * sectorBytes + offset, bytes); return bytes;
  });
  disk.onWrite(readonly ? EspUsbDeviceMscWriteCallback{} : EspUsbDeviceMscWriteCallback{[](uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bytes) -> int32_t {
    ++writes; offsets.store(offsets.load() && offset < sectorBytes && lba < 32 && bytes <= sizeof(storage) - (lba * sectorBytes + offset));
    if (ioFail.load() || lba >= 32 || offset >= sectorBytes || bytes > sizeof(storage) - (lba * sectorBytes + offset)) return -1;
    memcpy(storage + lba * sectorBytes + offset, buffer, bytes); return bytes;
  }});
  disk.onStartStop([](uint8_t, bool, bool) { ++stops; return true; });
}
bool metadata() {
  bool ok = true;
  for (unsigned i = 0; i < 20; ++i) {
    EspUsbDevice local; EspUsbDeviceMsc function(local);
    function.onRead([](uint32_t, uint32_t, void *, uint32_t n) { return int32_t(n); });
    ok &= function.begin(32, 512);
    EspUsbMscConfig config; config.transferBytes = 64; config.name = "ストレージ";
    ok &= function.configure(config) == ESP_OK;
    auto invalid = config; invalid.transferBytes = 63;
    ok &= function.configure(invalid) == ESP_ERR_INVALID_SIZE && function.configuration().transferBytes == 64;
    invalid = config; invalid.transferBuffer = external + 1;
    ok &= function.configure(invalid) == ESP_ERR_INVALID_ARG;
    EspUsbDeviceConfig identity; identity.startTinyUsb = false;
    ok &= local.begin(identity) && function.transferCapacity() == 64;
    ok &= function.configure(config) == ESP_ERR_INVALID_STATE && !function.begin(64, 512) && function.onRead(nullptr) == ESP_ERR_INVALID_STATE;
    ok &= function.vendorID("changed") == ESP_ERR_INVALID_STATE && function.onStartStop(nullptr) == ESP_ERR_INVALID_STATE;
    ok &= local.end() == ESP_OK && !function.transferCapacity();
  }
  return ok;
}
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "metadata") {
    bool ok = metadata(); size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    ok &= metadata(); size_t after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    Serial.printf("METADATA ok=%u before=%u after=%u\r\n", unsigned(ok), unsigned(before), unsigned(after));
  } else if (command.startsWith("start ")) {
    unsigned bytes = 0, mode = 0; sectorBytes = 512;
    sscanf(command.c_str(), "start %u %u %u", &bytes, &mode, &sectorBytes);
    if (!sectorBytes || sectorBytes > 512) { Serial.println("INVALID_GEOMETRY"); return; }
    delete serialPort; serialPort = nullptr; delete second; second = nullptr;
    reads.store(0); writes.store(0); stops.store(0); offsets.store(true); ioFail.store(false);
    memset(storage, 0, sizeof(storage)); memset(external, 0x5a, sizeof(external));
    readOnly = mode == 2; callbacks(readOnly);
    disk.vendorID("V3TEST"); disk.productID("Runtime Disk"); disk.productRevision("3.0");
    disk.mediaPresent(true); disk.isWritable(!readOnly);
    EspUsbMscConfig config; config.transferBytes = bytes; config.name = "Runtime storage";
    if (mode == 1) config.transferBuffer = external + 64;
    bool ok = disk.configure(config) == ESP_OK && disk.begin(32, sectorBytes);
    if (mode == 3) serialPort = new EspUsbDeviceCdcSerial(device);
    if (mode == 4) {
      second = new EspUsbDeviceMsc(device);
      second->onRead([](uint32_t, uint32_t, void *buffer, uint32_t n) { memset(buffer, 0xa5, n); return int32_t(n); });
      second->mediaPresent(true); ok &= second->begin(8, 512);
    }
    EspUsbDeviceConfig identity; identity.product = "v3 MSC storage";
    ok &= device.begin(identity);
    Serial.printf("STARTED %u capacity=%u\r\n", unsigned(ok), unsigned(disk.transferCapacity()));
  } else if (command == "readonly" || command == "writable") {
    disk.isWritable(command == "writable"); Serial.println("MODE_CHANGED");
  } else if (command == "absent" || command == "present") {
    disk.mediaPresent(command == "present"); Serial.println("MEDIA_CHANGED");
  } else if (command == "fail" || command == "recover") {
    ioFail.store(command == "fail"); Serial.println("IO_CHANGED");
  } else if (command == "stats") {
    bool guards = true;
    for (unsigned i = 0; i < 64; ++i) guards &= external[i] == 0x5a && external[64 + disk.transferCapacity() + i] == 0x5a;
    Serial.printf("STATS reads=%u writes=%u offsets=%u guards=%u\r\n", reads.load(), writes.load(), unsigned(offsets.load()), unsigned(guards));
  } else if (command == "drop") {
    delete second; second = nullptr;
    Serial.printf("DROPPED %u\r\n", unsigned(!device.ready()));
  } else if (command == "stop") {
    Serial.println(device.end() == ESP_OK ? "STOPPED" : "STOP_FAILED");
  }
}
