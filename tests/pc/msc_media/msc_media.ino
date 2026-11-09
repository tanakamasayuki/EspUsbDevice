#include <Arduino.h>
#include <EspUsbDevice.h>
#include <atomic>
// RAM MSC on the P4X HS controller; the Linux PC drives BOT/SCSI itself.
// The medium can be pulled from inside the Nth storage callback.
EspUsbDevice device;
EspUsbDeviceMsc msc(device);
constexpr uint32_t Blocks = 64, Block = 512;
uint8_t disk[Blocks * Block];
std::atomic<int> pullAt{-1};
std::atomic<bool> reinsert{false}, pulled{false};
std::atomic<unsigned> reads{0}, writes{0}, readsAfter{0}, writesAfter{0};
void maybePull(unsigned n) {
  if (int(n) != pullAt.load()) return;
  msc.mediaPresent(false); pulled = true;
  if (reinsert.load()) msc.mediaPresent(true);
}
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start") {
    for (uint32_t i = 0; i < sizeof(disk); ++i) disk[i] = uint8_t(i / Block * 7 + i);
    msc.onRead([](uint32_t lba, uint32_t offset, void *buffer, uint32_t bytes) -> int32_t {
      if (pulled.load()) ++readsAfter;
      memcpy(buffer, disk + lba * Block + offset, bytes); maybePull(++reads); return int32_t(bytes);
    });
    msc.onWrite([](uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bytes) -> int32_t {
      if (pulled.load()) ++writesAfter;
      memcpy(disk + lba * Block + offset, buffer, bytes); maybePull(++writes); return int32_t(bytes);
    });
    EspUsbMscConfig config; config.transferBytes = Block;
    msc.mediaPresent(true);
    bool ok = msc.configure(config) == ESP_OK && msc.begin(Blocks, Block);
    EspUsbDeviceConfig identity; identity.vid = 0x1209; identity.pid = 0x0001;
    identity.product = "EspUsbDevice MSC media"; identity.serialNumber = "espusb-msc-media";
    ok &= device.begin(identity);
    Serial.printf("STARTED %u\r\n", unsigned(ok));
  } else if (command.startsWith("present ")) {
    msc.mediaPresent(command.substring(8).toInt()); Serial.println("PRESENT_SET");
  } else if (command.startsWith("pull ")) {
    int at = 0, again = 0; sscanf(command.c_str(), "pull %d %d", &at, &again);
    reads = writes = readsAfter = writesAfter = 0; pulled = false; reinsert = again; pullAt = at;
    Serial.println("PULL_ARMED");
  } else if (command == "counts") {
    Serial.printf("COUNTS reads=%u writes=%u readsAfter=%u writesAfter=%u present=%u\r\n", unsigned(reads.load()),
                  unsigned(writes.load()), unsigned(readsAfter.load()), unsigned(writesAfter.load()), unsigned(msc.testUnitReady()));
  } else if (command.startsWith("block ")) {
    const uint32_t lba = command.substring(6).toInt(); uint32_t sum = 0;
    for (uint32_t i = 0; i < Block; ++i) sum = sum * 31 + disk[lba * Block + i];
    Serial.printf("BLOCK %u sum=%u\r\n", unsigned(lba), unsigned(sum));
  } else if (command == "stop") {
    Serial.println(!device.started() || device.end() == ESP_OK ? "STOPPED" : "STOP_FAILED");
  }
}
