#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
#include "esp_heap_caps.h"
// Host side: CDC ACM echo with line coding and a boot keyboard, as one composite.
EspUsbHost host;
std::atomic<unsigned> address{0}, speed{255}, presses{0}, releases{0};
std::atomic<size_t> count{0};
std::atomic<bool> overflow{false};
uint8_t sent[8192], input[8192];
bool started = false;
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    if (strcmp(info.product, "EspUsbDevice CDC HID") == 0) { speed = info.speed; address = info.address; }
  });
  host.onKeyboard([](const EspUsbHostKeyboardEvent &event) {
    if (event.keycode == 4) { if (event.pressed) ++presses; if (event.released) ++releases; }
  });
  host.onSerialData([](const EspUsbHostSerialData &event) {
    const size_t offset = count.load();
    if (offset + event.length <= sizeof(input)) memcpy(input + offset, event.data, event.length);
    else overflow = true;
    count.store(offset + event.length, std::memory_order_release);
  });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start") {
    address = 0; speed = 255; presses = 0; releases = 0;
    EspUsbHostConfig config; config.port = ESP_USB_HOST_PORT_HIGH_SPEED;
    started = host.begin(config);
    const uint32_t begun = millis();
    while (started && (!address.load() || !host.serialReady(address.load(), 0)) && millis() - begun < 12000) delay(1);
    bool ok = started && address.load() && host.serialReady(address.load(), 0);
    ok = ok && host.setSerialBaudRate(230400, address.load(), 0);
    Serial.printf("HOST_READY ok=%u speed=%u\r\n", unsigned(ok), speed.load());
  } else if (command == "serial") {
    bool ok = address.load(); unsigned exchanges = 0; size_t bytes = 0;
    for (size_t n : {1u, 64u, 129u, 511u, 512u, 513u, 4097u}) {
      if (!ok) break;
      for (size_t i = 0; i < n; ++i) sent[i] = uint8_t(n + i * 31);
      count = 0; overflow = false;
      ok &= host.sendSerial(sent, n, address.load(), 0);
      const uint32_t begun = millis();
      while (ok && count.load(std::memory_order_acquire) < n && millis() - begun < 5000) delay(1);
      ok &= count.load(std::memory_order_acquire) == n && !overflow.load() && memcmp(input, sent, n) == 0;
      if (ok) { ++exchanges; bytes += n; }
    }
    Serial.printf("SERIAL ok=%u exchanges=%u bytes=%u\r\n", unsigned(ok), exchanges, unsigned(bytes));
  } else if (command.startsWith("keys ")) {
    const unsigned want = command.substring(5).toInt();
    const uint32_t begun = millis();
    while ((presses.load() < want || releases.load() < want) && millis() - begun < 5000) delay(1);
    Serial.printf("KEYS press=%u release=%u\r\n", presses.load(), releases.load());
  } else if (command == "stop") {
    if (started) host.end();
    started = false; address = 0;
    Serial.printf("STOPPED heap=%u\r\n", unsigned(heap_caps_check_integrity_all(true)));
  }
}
