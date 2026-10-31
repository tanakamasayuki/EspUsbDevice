#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
#include "../common/probe_link.h"

EspUsbHost host;
std::atomic<uint8_t> address{0};
bool started = false;
uint8_t patternByte(size_t offset, size_t seed) {
  return uint8_t(offset * 37 + (offset >> 8) * 17 + (offset >> 16) * 53 + seed);
}
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    if (info.vid == 0x303a && info.pid == 0x4000 && strcmp(info.product, "v3 transfer limits") == 0) address.store(info.address);
  });
  host.onDeviceDisconnected([](const EspUsbHostDeviceInfo &) { address.store(0); });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start") {
    started = host.begin();
    const uint32_t deadline = millis() + 10000;
    while (started && !address.load() && int32_t(deadline - millis()) > 0) delay(1);
    const bool opened = started && address.load() && host.vendorOpen(address.load(), 0xff, ESP_USB_HOST_VENDOR_READ_ON_DEMAND);
    Serial.println(opened ? "HOST_READY" : "HOST_START_FAILED");
  } else if (command.startsWith("send ") || command.startsWith("receive ")) {
    const bool send = command.startsWith("send ");
    unsigned length = 0, terminate = 0, pattern = 0;
    sscanf(command.c_str() + (send ? 5 : 8), "%u %u %u", &length, &terminate, &pattern);
    if (!pattern) pattern = length;
    constexpr size_t payloadChunk = 4096;
    static uint8_t data[payloadChunk + PROBE_BULK_MPS];
    size_t total = 0;
    bool ok = length && length <= 131072;
    while (ok && total < length) {
      const size_t remaining = length - total;
      const size_t chunk = remaining < payloadChunk ? remaining : payloadChunk;
      if (send) {
        for (size_t i = 0; i < chunk; ++i) data[i] = patternByte(total + i, pattern);
        ok = host.vendorWrite(data, chunk, address.load());
        if (ok) total += chunk;
      } else {
        size_t actual = 0;
        // On the final read, leave room for an explicit ZLP/short terminator.
        const size_t request = chunk + (terminate && chunk == remaining && chunk % PROBE_BULK_MPS == 0 ? PROBE_BULK_MPS : 0);
        // A final full chunk may be followed by an explicit ZLP.
        if (request > sizeof(data)) { ok = false; break; }
        ok = host.vendorReadSync(data, request, &actual, 5000, address.load()) && actual == chunk;
        for (size_t i = 0; ok && i < actual; ++i) ok = data[i] == patternByte(total + i, pattern);
        if (ok) total += actual;
      }
    }
    Serial.printf("TRANSFER length=%u actual=%u ok=%u\r\n", length, unsigned(total), unsigned(ok));
  } else if (command == "zlp") {
    Serial.printf("ZLP_SENT %u\r\n", unsigned(host.vendorWriteZlp(address.load())));
  } else if (command == "stop") {
    if (started) host.end();
    started = false; address.store(0);
    Serial.println("STOPPED");
  }
}
