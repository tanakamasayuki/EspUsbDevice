#include <Arduino.h>
#include <EspUsbDevice.h>
#include "esp_heap_caps.h"
// Two borrowed sends outstanding: one on the wire, one queued behind it. A
// third is Busy. Both come back exactly once and in order, whether they are
// sent, cancelled or stopped with the device.
EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
uint8_t *slots[3] = {};
struct Done { int slot; uint8_t status; size_t actual; uint64_t id; };
Done done[16];
volatile unsigned doneCount = 0;
uint64_t ids[3] = {};

int slotOf(const void *p) { for (int i = 0; i < 3; ++i) if (p == slots[i]) return i; return -1; }
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  for (auto &s : slots) s = static_cast<uint8_t *>(heap_caps_aligned_alloc(64, 1024, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  unsigned a = 0, b = 0;
  if (command == "start") {
    bool ok = slots[0] && slots[1] && slots[2];
    if (!device.started()) {
      ok &= vendor.onDirectTxComplete([](const EspUsbTransferResult &r) {
        if (doneCount < 16) done[doneCount] = {slotOf(r.buffer), uint8_t(r.status), r.actualBytes, r.id};
        ++doneCount;
      }) == ESP_OK;
      EspUsbDeviceConfig identity; identity.vid = 0x303a; identity.pid = 0x4000; identity.product = "v3 Vendor API probe";
      ok &= device.begin(identity);
    }
    Serial.printf("DEVICE_STARTED %u\r\n", unsigned(ok));
  } else if (sscanf(command.c_str(), "post %u %u", &a, &b) == 2) {
    // slot b carries the vendor_roundtrip pattern for length a.
    for (unsigned i = 0; i < a; ++i) slots[b][i] = uint8_t(i * 37 + a);
    const auto s = vendor.writeDirect(slots[b], a, EspUsbTransferTermination::Continue);
    ids[b] = s.id;
    Serial.printf("POSTED %u error=%u\r\n", unsigned(bool(s)), unsigned(s.error));
  } else if (command == "clear") {
    doneCount = 0; Serial.println("CLEARED");
  } else if (sscanf(command.c_str(), "wait %u", &a) == 1) {
    const uint32_t until = millis() + 3000;
    while (doneCount < a && int32_t(until - millis()) > 0) delay(1);
    Serial.printf("DONE n=%u", unsigned(doneCount));
    for (unsigned i = 0; i < doneCount && i < 16; ++i)
      Serial.printf(" %d:%u:%u:%u", done[i].slot, unsigned(done[i].status), unsigned(done[i].actual),
                    unsigned(done[i].slot >= 0 && done[i].id == ids[done[i].slot]));
    Serial.print("\r\n");
  } else if (command == "cancel") {
    Serial.printf("CANCEL %d\r\n", int(vendor.cancelDirectTx()));
  } else if (command == "cancel_queued") {
    Serial.printf("CANCEL %d\r\n", int(vendor.cancelDirectTx(ids[1])));
  } else if (command == "stop") {
    Serial.printf("STOPPED %d\r\n", int(device.end()));
  }
}
