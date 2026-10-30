#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
#include "esp_heap_caps.h"
// Host side of the Vendor link over a high-speed port. Bulk IN goes through the
// published Host's cache-prepared read queue and its data callback.
EspUsbHost host;
std::atomic<unsigned> address{0}, speed{255};
std::atomic<size_t> received{0};
std::atomic<bool> overflow{false};
bool started = false;
uint8_t sent[65537], incoming[65537];

uint8_t pattern(size_t i, size_t n) { return uint8_t(i * 29 + n); }
void expectBytes() { received.store(0); overflow.store(false); }
bool receiveBytes(size_t n) {
  const uint32_t begun = millis();
  while (received.load(std::memory_order_acquire) < n && !overflow.load() && millis() - begun < 10000) delay(1);
  bool ok = received.load(std::memory_order_acquire) == n && !overflow.load();
  for (size_t i = 0; i < n && ok; ++i) ok &= incoming[i] == pattern(i, n);
  return ok;
}
bool sendBytes(size_t n) {
  for (size_t i = 0; i < n; ++i) sent[i] = pattern(i, n);
  return host.vendorWrite(sent, n, address.load());
}
void stopHost() {
  if (started && address.load() && host.vendorReadQueueReady(address.load())) host.vendorReadQueueEnd(address.load());
  if (started) host.end();
  started = false; address = 0;
}
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    if (info.vid == 0x1209 && info.pid == 0x0003 && strcmp(info.product, "EspUsbDevice vendor link") == 0) {
      speed = info.speed; address = info.address;
    }
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
  const size_t n = command.indexOf(' ') > 0 ? command.substring(command.indexOf(' ') + 1).toInt() : 0;
  if (command == "start") {
    address = 0; speed = 255;
    EspUsbHostConfig config; config.port = ESP_USB_HOST_PORT_HIGH_SPEED;
    started = host.begin(config);
    const uint32_t begun = millis();
    while (started && !address.load() && millis() - begun < 10000) delay(1);
    bool ok = started && address.load();
    ok = ok && host.vendorOpen(address.load(), 0xff, ESP_USB_HOST_VENDOR_READ_CONTINUOUS, 512);
    ok = ok && host.vendorReadQueueBegin(2, 512, address.load());
    Serial.printf("HOST_READY ok=%u speed=%u in=%u out=%u\r\n", unsigned(ok), speed.load(),
                  ok ? host.vendorInPacketSize(address.load()) : 0, ok ? host.vendorOutPacketSize(address.load()) : 0);
  } else if (command.startsWith("exchange ")) {
    expectBytes();
    const bool written = sendBytes(n);
    Serial.printf("EXCHANGE %u ok=%u\r\n", unsigned(n), unsigned(written && receiveBytes(n)));
  } else if (command.startsWith("send ")) {
    Serial.printf("SENT %u ok=%u\r\n", unsigned(n), unsigned(sendBytes(n)));
  } else if (command.startsWith("expect ")) {
    expectBytes();
    Serial.printf("EXPECTING %u\r\n", unsigned(n));
  } else if (command.startsWith("received ")) {
    Serial.printf("RECEIVED %u ok=%u\r\n", unsigned(n), unsigned(receiveBytes(n)));
  } else if (command == "stop") {
    stopHost();
    Serial.printf("STOPPED heap=%u\r\n", unsigned(heap_caps_check_integrity_all(true)));
  }
}
