#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
EspUsbHost host;
std::atomic<uint8_t> address{0};
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &d) { if (d.vid == 0x303a && d.pid == 0x4000) address.store(d.address); });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start") {
    address.store(0); bool ok = host.begin(); const uint32_t until = millis() + 10000;
    while (ok && !address.load() && int32_t(until - millis()) > 0) delay(1);
    bool muted = true;
    ok &= address.load() && host.audioGetMute(muted, address.load()) && !muted;
    Serial.printf("HOST_READY %u\r\n", unsigned(ok));
  } else if (command.startsWith("burst ")) {
    const unsigned n = command.substring(6).toInt(); bool ok = true;
    for (unsigned i = 0; i < n && ok; ++i) {
      bool muted = false;
      ok &= host.audioSetMute((i + 1) & 1, address.load()) && host.audioGetMute(muted, address.load()) && muted == bool((i + 1) & 1);
    }
    Serial.printf("BURST %u\r\n", unsigned(ok));
  } else if (command == "stop") {
    host.end(); Serial.println(!host.ready() ? "STOPPED" : "STOP_FAILED");
  }
}
