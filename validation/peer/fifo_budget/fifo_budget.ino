#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
#include "esp_heap_caps.h"
// Host side: does the Device reach the configured state and report itself?
EspUsbHost host;
std::atomic<unsigned> address{0};
bool started = false;
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    if (strcmp(info.product, "EspUsbDevice FIFO budget") == 0) address = info.address;
  });
  host.onDeviceDisconnected([](const EspUsbHostDeviceInfo &) { address = 0; });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start") {
    address = 0;
    EspUsbHostConfig config; config.port = ESP_USB_HOST_PORT_HIGH_SPEED;
    started = host.begin(config);
    const uint32_t begun = millis();
    while (started && !address.load() && millis() - begun < 8000) delay(1);
    Serial.printf("HOST_READY started=%u connected=%u\r\n", unsigned(started), unsigned(address.load() != 0));
  } else if (command == "stop") {
    if (started) host.end();
    started = false; address = 0;
    Serial.printf("STOPPED heap=%u\r\n", unsigned(heap_caps_check_integrity_all(true)));
  }
}
