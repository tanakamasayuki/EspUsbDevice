#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
#include "esp_heap_caps.h"
// Host side: MSC block writes/reads and a Vendor echo against a Device whose
// medium and rings are in PSRAM, over a high-speed link.
EspUsbHost host;
std::atomic<unsigned> address{0}, speed{255};
std::atomic<size_t> received{0};
std::atomic<bool> overflow{false};
bool started = false;
uint8_t sent[64 * 512], incoming[64 * 512];
// Same table as the Device's verify.
constexpr unsigned writes[][2] = {{0, 7}, {3, 7}, {4000, 64}, {8185, 7}};

void stopHost() {
  if (started && address.load() && host.vendorReadQueueReady(address.load())) host.vendorReadQueueEnd(address.load());
  if (started) host.end();
  started = false; address = 0;
}
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    if (strcmp(info.product, "EspUsbDevice PSRAM link") == 0) { speed = info.speed; address = info.address; }
  });
  host.onDeviceDisconnected([](const EspUsbHostDeviceInfo &) { address = 0; });
  host.onVendorData([](const EspUsbHostVendorData &event) {
    const size_t offset = received.load();
    if (offset + event.length <= sizeof(incoming)) memcpy(incoming + offset, event.data, event.length);
    else overflow = true;
    received.store(offset + event.length, std::memory_order_release);
  });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start") {
    address = 0; speed = 255;
    EspUsbHostConfig config; config.port = ESP_USB_HOST_PORT_HIGH_SPEED;
    started = host.begin(config);
    const uint32_t begun = millis();
    while (started && (!address.load() || !host.mscReady(address.load())) && millis() - begun < 12000) delay(1);
    uint32_t count = 0, size = 0;
    bool ok = started && address.load() && host.mscCapacity(count, size, address.load());
    ok = ok && host.vendorOpen(address.load(), 0xff, ESP_USB_HOST_VENDOR_READ_CONTINUOUS, 512);
    ok = ok && host.vendorReadQueueBegin(2, 512, address.load());
    Serial.printf("HOST_READY ok=%u speed=%u blocks=%u size=%u\r\n", unsigned(ok), speed.load(), unsigned(count), unsigned(size));
  } else if (command == "exercise") {
    bool ok = address.load();
    unsigned blocks = 0, echoes = 0;
    for (const auto &w : writes) {
      if (!ok) break;
      const size_t n = w[1] * 512;
      for (size_t i = 0; i < n; ++i) sent[i] = uint8_t(i * 13 + w[0]);
      memset(incoming, 0, n);
      ok &= host.mscWriteBlocks(w[0], sent, w[1], address.load()) && host.mscReadBlocks(w[0], incoming, w[1], address.load()) && !memcmp(sent, incoming, n);
      if (ok) blocks += w[1];
    }
    ok &= ok && host.mscSynchronizeCache(address.load());
    // Lengths around the 512-byte packet and past the Device's 8 KiB rings.
    const size_t lengths[] = {1, 511, 512, 513, 4095, 4096, 4097, 8192, 16384, 32768};
    for (unsigned repeat = 0; repeat < 4 && ok; ++repeat)
      for (size_t n : lengths) {
        if (!ok) break;
        for (size_t i = 0; i < n; ++i) sent[i] = uint8_t(i * 29 + n + repeat);
        received.store(0); overflow = false;
        ok &= host.vendorWrite(sent, n, address.load());
        const uint32_t begun = millis();
        while (ok && received.load(std::memory_order_acquire) < n && millis() - begun < 10000) delay(1);
        ok &= received.load(std::memory_order_acquire) == n && !overflow.load() && !memcmp(sent, incoming, n);
        if (ok) ++echoes;
      }
    Serial.printf("PSRAM_LINK ok=%u blocks=%u echoes=%u\r\n", unsigned(ok), blocks, echoes);
  } else if (command == "stop") {
    stopHost();
    Serial.printf("STOPPED heap=%u\r\n", unsigned(heap_caps_check_integrity_all(true)));
  }
}
