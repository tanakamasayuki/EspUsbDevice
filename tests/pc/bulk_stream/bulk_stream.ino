#include <Arduino.h>
#include <EspUsbDevice.h>
#include <atomic>
#include "esp_heap_caps.h"
// Borrowed bulk IN streamed to the Linux PC: fixed-size blocks, each starting
// with its sequence number, resubmitted from the completion callback. depth is
// how many blocks the sketch keeps submitted; a submission refused as Busy
// waits for the next completion, the way a streaming application would.
EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
constexpr size_t Block = 53 * 512;   // 27136 bytes, the largest block
std::atomic<uint32_t> blockBytes{Block};
constexpr unsigned Slots = 2;
uint8_t *slots[Slots] = {};
std::atomic<uint32_t> nextSeq{0}, completed{0}, busy{0}, errors{0}, total{0}, startUs{0}, endUs{0};
std::atomic<bool> running{false};
int parked = -1;   // slot refused as Busy, resubmitted on the next completion

void fill(uint8_t *slot) {
  const uint32_t seq = nextSeq.fetch_add(1);
  memcpy(slot, &seq, sizeof(seq));
}
bool submit(uint8_t *slot) {
  const auto s = vendor.writeDirect(slot, blockBytes.load(), EspUsbTransferTermination::Continue);
  if (s) return true;
  if (s.error == EspUsbTransferError::Busy) { ++busy; parked = slot == slots[0] ? 0 : 1; return true; }
  ++errors; return false;
}
void onComplete(const EspUsbTransferResult &r) {
  if (r.status != EspUsbTransferStatus::Success || r.actualBytes != blockBytes.load()) { ++errors; running = false; return; }
  const uint32_t done = completed.fetch_add(1) + 1;
  if (done >= total.load()) { endUs = micros(); running = false; return; }
  if (parked >= 0) { uint8_t *p = slots[parked]; parked = -1; submit(p); }
  if (running.load() && nextSeq.load() < total.load()) { auto *slot = const_cast<uint8_t *>(static_cast<const uint8_t *>(r.buffer)); fill(slot); submit(slot); }
}
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  for (auto &s : slots) {
    s = static_cast<uint8_t *>(heap_caps_aligned_alloc(64, Block, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (s) for (size_t i = 0; i < Block; ++i) s[i] = uint8_t(i * 7 + 3);
  }
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start") {
    EspUsbVendorConfig config = vendor.configuration();
    config.rxCapacity = 64;
    bool ok = slots[0] && slots[1] && vendor.configure(config) == ESP_OK && vendor.onDirectTxComplete(onComplete) == ESP_OK;
    EspUsbDeviceConfig identity; identity.vid = 0x1209; identity.pid = 0x0001;
    identity.product = "EspUsbDevice bulk stream"; identity.serialNumber = "espusb-bulk-stream";
    ok &= device.begin(identity);
    Serial.printf("STARTED %u\r\n", unsigned(ok));
  } else if (command == "ready") {
    Serial.printf("READY %u speed=%u\r\n", unsigned(device.ready()), unsigned(device.speed()));
  } else if (command.startsWith("stream ")) {
    unsigned blocks = 0, depth = 1, bytes = Block;
    sscanf(command.c_str(), "stream %u %u %u", &blocks, &depth, &bytes);
    blockBytes = bytes >= 512 && bytes <= Block && bytes % 512 == 0 ? bytes : Block;
    nextSeq = 0; completed = 0; busy = 0; errors = 0; total = blocks; parked = -1; endUs = 0;
    running = true; startUs = micros();
    bool ok = true;
    for (unsigned i = 0; i < depth && i < Slots && ok; ++i) { fill(slots[i]); ok = submit(slots[i]); }
    Serial.printf("STREAMING %u depth=%u bytes=%u\r\n", unsigned(ok), depth, unsigned(blockBytes.load()));
  } else if (command == "result") {
    const uint32_t us = endUs.load() ? endUs.load() - startUs.load() : 0;
    Serial.printf("RESULT done=%u blocks=%u busy=%u errors=%u us=%u\r\n", unsigned(!running.load()),
                  unsigned(completed.load()), unsigned(busy.load()), unsigned(errors.load()), unsigned(us));
  } else if (command == "stop") {
    running = false;
    Serial.printf("STOPPED %d\r\n", int(device.end()));
  }
}
