#include <Arduino.h>
#include <EspUsbDevice.h>
#include <atomic>
#include "esp_heap_caps.h"
// Device side: a simulated card answering APDUs and Escapes on the high-speed controller.
EspUsbDevice device;
EspUsbDeviceCcid card(device);
std::atomic<unsigned> apdus{0}, escapes{0};
const uint8_t atr[] = {0x3b, 0x80, 0x01, 0x81};
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  card.onApdu([](const uint8_t *data, size_t n, uint8_t *reply, size_t capacity) {
    if (n + 2 > capacity) return size_t(0);
    for (size_t i = 0; i < n; ++i) reply[i] = data[i] ^ 0x31;
    reply[n] = 0x90; reply[n + 1] = 0; ++apdus; return n + 2;
  });
  card.onEscape([](const uint8_t *data, size_t n, uint8_t *reply, size_t capacity) {
    if (n > capacity) return size_t(0);
    for (size_t i = 0; i < n; ++i) reply[i] = data[i] ^ 0x57;
    ++escapes; return n;
  });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command.startsWith("start ")) {
    apdus = 0; escapes = 0;
    EspUsbCcidConfig config; config.messageBytes = 1024; config.removable = command.endsWith("1"); config.name = "HS simulated card";
    bool ok = card.configure(config) == ESP_OK && card.insertCard(atr, sizeof(atr));
    EspUsbDeviceConfig dc; dc.product = "EspUsbDevice CCID"; dc.controller = EspUsbController::HighSpeed;
    ok &= device.begin(dc);
    Serial.printf("STARTED ok=%u capacity=%u\r\n", unsigned(ok), unsigned(card.messageCapacity()));
  } else if (command == "card") {
    Serial.printf("CARD powered=%u apdu=%u escape=%u\r\n", unsigned(card.cardPowered()), apdus.load(), escapes.load());
  } else if (command == "remove") {
    Serial.printf("REMOVE err=%d\r\n", int(card.removeCard()));
  } else if (command == "insert") {
    Serial.printf("INSERT %u\r\n", unsigned(card.insertCard(atr, sizeof(atr))));
  } else if (command == "stop") {
    const bool stopped = !device.started() || device.end() == ESP_OK;
    Serial.printf("STOPPED %u capacity=%u heap=%u\r\n", unsigned(stopped), unsigned(card.messageCapacity()),
                  unsigned(heap_caps_check_integrity_all(true)));
  }
}
