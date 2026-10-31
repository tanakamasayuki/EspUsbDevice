#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
#include "esp_heap_caps.h"
// Host side: read the Vendor bulk packet sizes from the enumerated descriptor
// and echo lengths around them.
EspUsbHost host;
std::atomic<unsigned> address{0};
std::atomic<size_t> received{0};
std::atomic<bool> overflow{false};
bool started = false;
uint8_t sent[1024], incoming[1024];

void stopHost() {
  if (started && address.load() && host.vendorReadQueueReady(address.load())) host.vendorReadQueueEnd(address.load());
  if (started) host.end();
  started = false; address = 0;
}
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    if (strcmp(info.product, "EspUsbDevice packet size") == 0) address = info.address;
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
  if (command.startsWith("start ")) {
    // Read one packet per request: a full packet without a ZLP must still complete.
    const unsigned packet = command.substring(6).toInt();
    address = 0;
    EspUsbHostConfig config;
#if CONFIG_IDF_TARGET_ESP32P4
    config.port = ESP_USB_HOST_PORT_HIGH_SPEED;
#endif
    started = host.begin(config);
    const uint32_t begun = millis();
    while (started && !address.load() && millis() - begun < 10000) delay(1);
    bool ok = started && address.load() && host.vendorOpen(address.load(), 0xff, ESP_USB_HOST_VENDOR_READ_CONTINUOUS, packet);
    ok = ok && host.vendorReadQueueBegin(2, packet, address.load());
    Serial.printf("HOST_READY ok=%u in=%u out=%u\r\n", unsigned(ok),
                  ok ? host.vendorInPacketSize(address.load()) : 0, ok ? host.vendorOutPacketSize(address.load()) : 0);
  } else if (command.startsWith("exchange ")) {
    const size_t n = command.substring(9).toInt();
    for (size_t i = 0; i < n; ++i) sent[i] = uint8_t(i * 31 + n);
    received.store(0); overflow = false;
    bool ok = n <= sizeof(sent) && host.vendorWrite(sent, n, address.load());
    const uint32_t begun = millis();
    while (ok && received.load(std::memory_order_acquire) < n && !overflow.load() && millis() - begun < 5000) delay(1);
    ok = ok && received.load(std::memory_order_acquire) == n && !overflow.load() && !memcmp(sent, incoming, n);
    Serial.printf("EXCHANGE %u ok=%u\r\n", unsigned(n), unsigned(ok));
  } else if (command == "stop") {
    stopHost();
    Serial.printf("STOPPED heap=%u\r\n", unsigned(heap_caps_check_integrity_all(true)));
  }
}
