#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
EspUsbHost host;
std::atomic<bool> ready{false};
std::atomic<unsigned> mask{0}, invalid{0}, descriptorBytes{0};
std::atomic<bool> descriptorValid{false};
bool started = false;
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    if (info.vid == 0x303a && info.pid == 0x4000 && strcmp(info.product, "v3 Function registry") == 0) ready.store(true);
  });
  host.onHIDReportDescriptor([](const EspUsbHostHIDReportDescriptor &descriptor) {
    descriptorBytes.store(descriptor.length);
    descriptorValid.store(descriptor.length > 0 && descriptor.length == descriptor.reportedLength);
  });
  host.onHIDInput([](const EspUsbHostHIDInput &event) {
    if (!event.data || event.length != 2 || event.data[0] < 1 || event.data[0] > 7 || event.data[1] != 0x40 + event.data[0]) { ++invalid; return; }
    mask.fetch_or(1u << event.data[0]);
  });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start") {
    ready.store(false); mask.store(0); invalid.store(0);
    descriptorBytes.store(0); descriptorValid.store(false);
    started = host.begin();
    const uint32_t deadline = millis() + 10000;
    while (started && (!ready.load() || !descriptorValid.load()) && int32_t(deadline - millis()) > 0) delay(1);
    Serial.printf("HOST_READY %u\r\n", unsigned(started && ready.load() && descriptorValid.load()));
    Serial.printf("REGISTRY_DESCRIPTOR ok=%u bytes=%u\r\n", unsigned(descriptorValid.load()), descriptorBytes.load());
  } else if (command == "reports") {
    const uint32_t deadline = millis() + 3000;
    while (mask.load() != 254 && int32_t(deadline - millis()) > 0) delay(1);
    Serial.printf("REPORTS mask=%u invalid=%u\r\n", mask.load(), invalid.load());
  } else if (command == "stop") {
    if (started) host.end();
    started = false;
    Serial.println("STOPPED");
  }
}
