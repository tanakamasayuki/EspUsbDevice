#include <Arduino.h>
#include <EspUsbDevice.h>
#include <atomic>
#include "esp_heap_caps.h"
// Device side: RAM-disk MSC and a two-cable MIDI echo on the high-speed controller.
EspUsbDevice device;
EspUsbDeviceMsc disk(device);
EspUsbDeviceMidi midi(device, 2);
uint8_t storage[64 * 512 + 128];
EspUsbDeviceMscRamDisk *ram = nullptr;
std::atomic<unsigned> echoed{0}, bad{0};
unsigned sectorSize = 512;
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  midi.onPacket([](const EspUsbDeviceMidiPacket &packet) { if (midi.writePacket(packet)) ++echoed; else ++bad; });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command.startsWith("start ")) {
    unsigned staging = 512;
    sscanf(command.c_str(), "start %u %u", &sectorSize, &staging);
    memset(storage, 0xc3, sizeof(storage)); echoed = 0; bad = 0;
    ram = new EspUsbDeviceMscRamDisk(storage + 64, 64, sectorSize);
    bool ok = ram && ram->valid() && ram->attach(disk);
    if (ram) ram->clear(0x5a);
    disk.mediaPresent(true); disk.isWritable(true);
    EspUsbMscConfig mc; mc.transferBytes = staging; ok &= disk.configure(mc) == ESP_OK;
    EspUsbMidiConfig mi; mi.inCables = mi.outCables = 2; mi.rxMode = EspUsbMidiRxMode::Callback;
    mi.rxPackets = 0; mi.txPackets = 32; mi.transferPackets = 16;
    ok &= midi.configure(mi) == ESP_OK;
    EspUsbDeviceConfig dc; dc.product = "EspUsbDevice MSC MIDI"; dc.controller = EspUsbController::HighSpeed;
    bool begun = device.begin(dc);
    unsigned rejected = 0;
    if (staging < 512) {
      // Staging below the high-speed packet is refused before the bus is touched.
      rejected = !begun && device.lastError() == ESP_ERR_INVALID_SIZE && disk.transferCapacity() == 0;
      ok &= rejected && device.end() == ESP_OK;
      mc.transferBytes = 512; ok &= disk.configure(mc) == ESP_OK;
      begun = device.begin(dc);
    }
    ok &= begun;
    Serial.printf("STARTED ok=%u rejected=%u staging=%u\r\n", unsigned(ok), rejected, unsigned(disk.transferCapacity()));
  } else if (command == "verify") {
    bool ok = ram != nullptr, guards = true;
    // The Host wrote 7 blocks at LBA 0, 3 and 57 in that order; LBA 3 overwrote blocks 3..6.
    for (unsigned block = 0; block < 64 && ok; ++block) {
      const unsigned lba = block >= 57 ? 57 : block >= 3 && block < 10 ? 3 : block < 3 ? 0 : 64;
      for (size_t i = 0; i < sectorSize && ok; ++i) {
        const size_t offset = (block - lba) * sectorSize + i;
        const uint8_t want = lba == 64 ? 0x5a : uint8_t(offset * 13 + lba + sectorSize);
        ok &= ram->data()[block * sectorSize + i] == want;
      }
    }
    for (size_t i = 0; i < 64; ++i) guards &= storage[i] == 0xc3;
    for (size_t i = 64 + 64 * sectorSize; i < sizeof(storage); ++i) guards &= storage[i] == 0xc3;
    Serial.printf("VERIFY ok=%u guards=%u midi=%u bad=%u\r\n", unsigned(ok), unsigned(guards), echoed.load(), bad.load());
  } else if (command == "stop") {
    const bool stopped = !device.started() || device.end() == ESP_OK;
    delete ram; ram = nullptr;
    Serial.printf("STOPPED %u heap=%u\r\n", unsigned(stopped), unsigned(heap_caps_check_integrity_all(true)));
  }
}
