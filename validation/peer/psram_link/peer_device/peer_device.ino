#include <Arduino.h>
#include <EspUsbDevice.h>
#include <algorithm>
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_psram.h"
// Device side: a 4 MiB MSC medium and both Vendor rings live in PSRAM on the
// high-speed controller; USB DMA staging stays internal. The echo runs from
// loop, so a full TX ring only makes it wait.
EspUsbDevice device;
EspUsbDeviceMsc disk(device);
EspUsbDeviceVendor vendor(device);
EspUsbDeviceMscRamDisk *ram = nullptr;
uint8_t *medium = nullptr, *rxRing = nullptr, *txRing = nullptr;
constexpr size_t blocks = 8192, diskBytes = blocks * 512, ringBytes = 8192, guardBytes = 64;
// The Host writes these (LBA, block count) ranges in order; a later one wins on overlap.
constexpr unsigned writes[][2] = {{0, 7}, {3, 7}, {4000, 64}, {8185, 7}};
size_t echoed = 0;
unsigned bad = 0;

uint8_t *external(size_t bytes) {
  auto *p = static_cast<uint8_t *>(heap_caps_aligned_alloc(64, bytes + 2 * guardBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (p) memset(p, 0xc3, bytes + 2 * guardBytes);
  return p;
}
bool guard(const uint8_t *p, size_t bytes) {
  if (!p) return false;
  for (size_t i = 0; i < guardBytes; ++i) if (p[i] != 0xc3 || p[guardBytes + bytes + i] != 0xc3) return false;
  return true;
}
bool guards() { return guard(medium, diskBytes) && guard(rxRing, ringBytes) && guard(txRing, ringBytes); }
void release() {
  delete ram; ram = nullptr;
  heap_caps_free(medium); heap_caps_free(rxRing); heap_caps_free(txRing);
  medium = rxRing = txRing = nullptr;
}
uint8_t expected(unsigned block, size_t i) {
  uint8_t value = 0x5a;
  for (const auto &w : writes)
    if (block >= w[0] && block < w[0] + w[1]) value = uint8_t(((block - w[0]) * 512 + i) * 13 + w[0]);
  return value;
}
void pump() {
  uint8_t data[512];
  while (device.ready() && vendor.available() > 0 && vendor.writeAvailable()) {
    const size_t n = vendor.read(data, std::min(sizeof(data), vendor.writeAvailable()));
    if (!n) break;
    if (vendor.write(data, n) != n) ++bad;
    echoed += n;
  }
}
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  pump();
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start") {
    release(); echoed = 0; bad = 0;
    medium = external(diskBytes); rxRing = external(ringBytes); txRing = external(ringBytes);
    const bool external = medium && rxRing && txRing && esp_ptr_external_ram(medium) && esp_ptr_external_ram(rxRing) && esp_ptr_external_ram(txRing);
    bool ok = external;
    if (ok) {
      ram = new EspUsbDeviceMscRamDisk(medium + guardBytes, blocks, 512);
      ok &= ram->valid() && ram->attach(disk);
      ram->clear(0x5a);
      disk.mediaPresent(true); disk.isWritable(true);
      EspUsbMscConfig mc; mc.transferBytes = 512; ok &= disk.configure(mc) == ESP_OK;
      EspUsbVendorConfig vc; vc.rxCapacity = vc.txCapacity = ringBytes; vc.transferBytes = 512;
      vc.rxBuffer = rxRing + guardBytes; vc.txBuffer = txRing + guardBytes;
      ok &= vendor.configure(vc) == ESP_OK;
      EspUsbDeviceConfig dc; dc.product = "EspUsbDevice PSRAM link"; dc.controller = EspUsbController::HighSpeed;
      ok &= device.begin(dc);
    }
    Serial.printf("STARTED ok=%u external=%u psram=%u\r\n", unsigned(ok), unsigned(external),
                  unsigned(esp_psram_is_initialized() ? esp_psram_get_size() : 0));
  } else if (command == "verify") {
    bool ok = ram != nullptr;
    for (unsigned block = 0; block < blocks && ok; ++block)
      for (size_t i = 0; i < 512 && ok; ++i) ok &= ram->data()[block * 512 + i] == expected(block, i);
    Serial.printf("VERIFY ok=%u guards=%u echoed=%u bad=%u\r\n", unsigned(ok), unsigned(guards()), unsigned(echoed), bad);
  } else if (command == "stop") {
    const bool stopped = !device.started() || device.end() == ESP_OK;
    const bool intact = !medium || guards();
    release();
    Serial.printf("STOPPED %u guards=%u heap=%u\r\n", unsigned(stopped), unsigned(intact), unsigned(heap_caps_check_integrity_all(true)));
  }
}
