#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>

EspUsbHost host;
std::atomic<uint8_t> address{0};
bool started = false;
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
    uint8_t data[4096];
    size_t total = 0;
    bool ok = length && length <= 65535;
    while (ok && total < length) {
      const size_t remaining = length - total;
      const size_t chunk = remaining < sizeof(data) ? remaining : sizeof(data);
      if (send) {
        for (size_t i = 0; i < chunk; ++i) data[i] = uint8_t((total + i) * 37 + pattern);
        ok = host.vendorWrite(data, chunk, address.load());
        if (ok) total += chunk;
      } else {
        size_t actual = 0;
        // On the final read, leave room for an explicit ZLP/short terminator.
        const size_t request = chunk + (terminate && chunk == remaining && chunk % 64 == 0 ? 64 : 0);
        // Largest final chunk here is 4095 bytes, including the max-length case.
        if (request > sizeof(data)) { ok = false; break; }
        ok = host.vendorReadSync(data, request, &actual, 5000, address.load()) && actual == chunk;
        for (size_t i = 0; ok && i < actual; ++i) ok = data[i] == uint8_t((total + i) * 37 + pattern);
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
