#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
#include "esp_heap_caps.h"
#include "../common/probe_link.h"
// Host side: records every HID input report, reads the declared interrupt IN
// packet, and sends control and bulk requests to the API semantics Device.
EspUsbHost host;
std::atomic<unsigned> address{0}, reports{0};
uint8_t recorded[32][9];
uint8_t lengths[32];
bool started = false;

void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    if (strcmp(info.product, "v3 API semantics") == 0) address = info.address;
  });
  host.onDeviceDisconnected([](const EspUsbHostDeviceInfo &) { address = 0; });
  host.onHIDInput([](const EspUsbHostHIDInput &event) {
    const unsigned n = reports.load();
    if (n < 32 && event.data) {
      lengths[n] = uint8_t(std::min<size_t>(event.length, 9));
      memcpy(recorded[n], event.data, lengths[n]);
    }
    reports.store(n + 1);
  });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start") {
    address = 0;
    EspUsbHostConfig config;
#if CONFIG_IDF_TARGET_ESP32P4
    config.port = ESP_USB_HOST_PORT_HIGH_SPEED;
#endif
    started = host.begin(config);
    const uint32_t begun = millis();
    while (started && !address.load() && millis() - begun < 10000) delay(1);
    delay(300);  // let the host finish its own HID setup requests
    reports = 0;
    Serial.printf("HOST_READY %u\r\n", unsigned(started && address.load()));
  } else if (command == "arm") {
    reports = 0;
    Serial.println("ARMED");
  } else if (command.startsWith("dump ")) {
    const unsigned want = command.substring(5).toInt();
    const uint32_t begun = millis();
    while (reports.load() < want && millis() - begun < 3000) delay(1);
    delay(50);
    const unsigned n = std::min(reports.load(), 32u);
    Serial.printf("REPORTS n=%u", reports.load());
    for (unsigned i = 0; i < n; ++i) {
      Serial.print(' ');
      for (unsigned j = 0; j < lengths[i]; ++j) Serial.printf("%02x", recorded[i][j]);
    }
    Serial.print("\r\n");
  } else if (command == "packet") {
    // GET_DESCRIPTOR(Configuration) at the current speed; first interrupt IN.
    uint8_t data[256]; size_t actual = 0; unsigned packet = 0;
    const bool ok = host.vendorControlTransfer(0x80, 6, 0x0200, 0, data, sizeof(data), &actual, address.load(), 1000);
    for (size_t i = 0; ok && i + 7 <= actual && data[i] >= 2; i += data[i])
      if (data[i + 1] == 5 && (data[i + 2] & 0x80) && (data[i + 3] & 3) == 3) { packet = data[i + 4] | data[i + 5] << 8; break; }
    Serial.printf("DECLARED ok=%u packet=%u\r\n", unsigned(ok), packet);
  } else if (command.startsWith("request ")) {
    unsigned type = 0, request = 0, value = 0, index = 0, length = 0;
    sscanf(command.c_str(), "request %x %x %x %x %u", &type, &request, &value, &index, &length);
    uint8_t data[64] = {}; size_t actual = 0;
    const bool ok = host.vendorControlTransfer(uint8_t(type), uint8_t(request), uint16_t(value), uint16_t(index),
                                               length ? data : nullptr, length, &actual, address.load(), 1000);
    Serial.printf("REQUEST ok=%u actual=%u\r\n", unsigned(ok), unsigned(actual));
  } else if (command == "bulk") {
    const uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const bool ok = host.vendorOpen(address.load(), 0xff, ESP_USB_HOST_VENDOR_READ_CONTINUOUS, PROBE_BULK_MPS) &&
                    host.vendorWrite(data, sizeof(data), address.load());
    Serial.printf("BULK ok=%u\r\n", unsigned(ok));
  } else if (command == "stop") {
    if (started) host.end();
    started = false; address = 0;
    Serial.printf("STOPPED heap=%u\r\n", unsigned(heap_caps_check_integrity_all(true)));
  }
}
