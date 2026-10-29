#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
EspUsbHost host;
std::atomic<uint8_t> address{0};
bool started = false;
uint8_t block[512], fatData[512], input[7 * 512], output[7 * 512];
uint16_t le16(const uint8_t *p) { return p[0] | uint16_t(p[1]) << 8; }
uint32_t le32(const uint8_t *p) { return le16(p) | uint32_t(le16(p+2)) << 16; }
bool sector(uint32_t lba, uint8_t *data) { return host.mscReadBlocks(lba, data, 1, address.load()); }
bool fatRoundtrip() {
  if (!sector(0, block) || block[510] != 0x55 || block[511] != 0xaa || le16(block+11) != 512 ||
      block[13] != 1 || block[16] != 2 || le16(block+19) != 64 || memcmp(block+54,"FAT12   ",8)) return false;
  const unsigned reserved = le16(block+14), sectorsPerFat = le16(block+22);
  const unsigned root = reserved + 2 * sectorsPerFat, entries = le16(block+17);
  const unsigned dataStart = root + (entries * 32 + 511) / 512;
  // This intentionally small disk has one sector per FAT; independently check both copies.
  if (sectorsPerFat != 1 || !sector(reserved, fatData) || !sector(reserved+1, block) || memcmp(fatData,block,512)) return false;
  unsigned cluster = 0; uint32_t size = 0;
  for (unsigned i = 0; i < (entries * 32 + 511) / 512; ++i) {
    if (!sector(root+i, block)) return false;
    for (unsigned j = 0; j < 512; j += 32) if (!memcmp(block+j,"PAYLOAD BIN",11)) {
      cluster = le16(block+j+26); size = le32(block+j+28);
    }
  }
  if (size != 1537 || cluster < 2) return false;
  unsigned clusters[4] = {}; size_t copied = 0;
  for (unsigned i = 0; i < 4; ++i) {
    if (cluster < 2 || cluster >= 2 + 64 - dataStart || !sector(dataStart+cluster-2, block)) return false;
    clusters[i] = cluster;
    const size_t n = std::min<size_t>(512, size - copied);
    memcpy(input+copied, block, n); copied += n;
    const unsigned offset = cluster + cluster / 2;
    const unsigned pair = le16(fatData+offset);
    cluster = (cluster & 1) ? pair >> 4 : pair & 0xfff;
  }
  if (copied != size || cluster < 0xff8) return false;
  for (size_t i = 0; i < size; ++i) if (input[i] != uint8_t(i * 37 + 11)) return false;
  copied = 0;
  for (unsigned i = 0; i < 4; ++i) {
    const unsigned lba = dataStart+clusters[i]-2;
    if (!sector(lba, block)) return false;
    const size_t n = std::min<size_t>(512, size - copied);
    for (size_t j = 0; j < n; ++j) block[j] = uint8_t((copied+j) * 13 + 23);
    if (!host.mscWriteBlocks(lba, block, 1, address.load())) return false;
    copied += n;
  }
  return host.mscSynchronizeCache(address.load());
}
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    if (info.vid == 0x303a && info.pid == 0x4000 && strcmp(info.product, "v3 MSC helpers") == 0) address.store(info.address);
  });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command.startsWith("start ")) {
    const unsigned mode = command.substring(6).toInt();
    address.store(0); started = host.begin();
    const uint32_t until = millis() + 10000;
    while (started && (!address.load() || !host.mscReady(address.load())) && int32_t(until-millis()) > 0) delay(1);
    uint32_t count = 0, size = 0;
    bool ok = started && address.load() && host.mscCapacity(count, size, address.load()) && count == 64 && size == (mode ? 512 : 511);
    Serial.printf("HOST_READY %u\r\n", unsigned(ok));
  } else if (command == "raw") {
    bool ok = sector(0, input) && input[510] == 0xa7;
    for (size_t i = 0; i < 7 * 511; ++i) output[i] = uint8_t(i * 13 + 23);
    ok &= host.mscWriteBlocks(3, output, 7, address.load());
    memset(input, 0, sizeof(input));
    ok &= host.mscReadBlocks(3, input, 7, address.load()) && !memcmp(input, output, 7 * 511);
    ok &= host.mscSynchronizeCache(address.load());
    Serial.printf("RAW_ROUNDTRIP %u\r\n", unsigned(ok));
  } else if (command == "fat") {
    Serial.printf("FAT_ROUNDTRIP %u\r\n", unsigned(fatRoundtrip()));
  } else if (command == "stop") {
    if (started) host.end(); started = false; address.store(0); Serial.println("STOPPED");
  }
}
