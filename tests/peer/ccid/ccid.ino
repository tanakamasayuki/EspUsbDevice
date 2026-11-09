#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
#include "esp_heap_caps.h"
// Host side: CCID power, APDU and Escape exchanges and slot status over a high-speed link.
EspUsbHost host;
std::atomic<unsigned> address{0}, speed{255};
const uint8_t expectedAtr[] = {0x3b, 0x80, 0x01, 0x81};
uint8_t sent[600], received[1024];
bool started = false;
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    if (strcmp(info.product, "EspUsbDevice CCID") == 0) { speed = info.speed; address = info.address; }
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
    while (started && !address.load() && millis() - begun < 12000) delay(1);
    const bool ok = started && address.load() && host.ccidOpen(address.load());
    Serial.printf("HOST_READY ok=%u speed=%u\r\n", unsigned(ok), speed.load());
  } else if (command == "exercise") {
    uint8_t atr[16]; size_t atrBytes = 0;
    bool ok = host.ccidPowerOn(atr, sizeof(atr), &atrBytes, ESP_USB_HOST_CCID_VOLTAGE_AUTO, 0, address.load()) &&
              atrBytes == sizeof(expectedAtr) && !memcmp(atr, expectedAtr, atrBytes);
    unsigned transfers = 0, opaque = 0;
    for (size_t n : {4u, 52u, 63u, 64u, 65u, 116u, 129u, 500u, 511u, 512u, 513u}) {
      if (!ok) break;
      for (size_t i = 0; i < n; ++i) sent[i] = uint8_t(i * 37 + n);
      size_t got = 0; uint16_t status = 0;
      ok &= host.ccidApdu(sent, n, received, sizeof(received), &got, &status, 0, address.load()) && got == n && status == 0x9000;
      for (size_t i = 0; i < n && ok; ++i) ok &= received[i] == uint8_t(sent[i] ^ 0x31);
      if (ok) ++transfers;
    }
    for (size_t n : {1u, 64u, 512u, 513u}) {
      if (!ok) break;
      for (size_t i = 0; i < n; ++i) sent[i] = uint8_t(i * 13 + n);
      size_t got = 0;
      ok &= host.ccidEscape(sent, n, received, sizeof(received), &got, 0, address.load()) && got == n;
      for (size_t i = 0; i < n && ok; ++i) ok &= received[i] == uint8_t(sent[i] ^ 0x57);
      if (ok) ++opaque;
    }
    Serial.printf("CCID ok=%u apdu=%u escape=%u\r\n", unsigned(ok), transfers, opaque);
  } else if (command == "off") {
    Serial.printf("OFF %u\r\n", unsigned(host.ccidPowerOff(0, address.load())));
  } else if (command == "status") {
    EspUsbHostCcidStatus state;
    const bool ok = host.ccidGetStatus(state, 0, address.load());
    Serial.printf("STATUS ok=%u present=%u active=%u\r\n", unsigned(ok), unsigned(state.present), unsigned(state.active));
  } else if (command == "stop") {
    if (started && address.load()) host.ccidClose(address.load());
    if (started) host.end();
    started = false; address = 0;
    Serial.printf("STOPPED heap=%u\r\n", unsigned(heap_caps_check_integrity_all(true)));
  }
}
