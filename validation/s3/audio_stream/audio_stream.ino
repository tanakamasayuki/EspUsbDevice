#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
EspUsbHost host;
unsigned activeDirections = 0;
std::atomic<uint8_t> address{0};
std::atomic<uint32_t> received{0}, bad{0}, filled{0};
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &d) { if (d.vid == 0x303a && d.pid == 0x4000) address.store(d.address); });
  host.onAudioData([](const EspUsbHostAudioData &d) {
    received += d.length;
    for (size_t i = 0; i < d.length; ++i) if (d.data[i] != 0 && d.data[i] != 0x5a) ++bad;
    for (size_t i = 0; i < d.length; ++i) if (d.data[i] == 0x5a) ++filled;
  });
  host.onAudioOutputRequest([](EspUsbHostAudioOutputRequest &r) { memset(r.data, 0x5a, r.byteCount); r.writtenFrames = r.frameCount; });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start") {
    address.store(0); EspUsbHostConfig config; config.fifo = {64, 32, 64};
    bool ok = host.begin(config); const uint32_t until = millis() + 10000;
    while (ok && !address.load() && int32_t(until - millis()) > 0) delay(1);
    Serial.printf("HOST_READY %u\r\n", unsigned(ok && address.load()));
  } else if (command.startsWith("run ")) {
    unsigned directions = 0, rate = 0; sscanf(command.c_str(), "run %u %u", &directions, &rate);
    activeDirections = directions; received = bad = filled = 0;
    bool ok = true;
    if (directions & 1) ok &= host.audioOutputStart(2, 16, rate, address.load());
    if (directions & 2) ok &= host.audioInputStart(2, 16, rate, address.load());
    delay(1500);
    Serial.printf("PCM ok=%u bytes=%u bad=%u filled=%u feedback=%u rejects=%u\r\n", unsigned(ok), unsigned(received.load()), unsigned(bad.load()), unsigned(filled.load()), unsigned(directions & 1 ? host.audioOutputFeedbackUpdates(address.load()) : 0), unsigned(directions & 1 ? host.audioOutputFeedbackRejects(address.load()) : 0));
  } else if (command == "pause") {
    bool ok = !(activeDirections & 2) || host.audioInputStop(address.load());
    if (activeDirections & 1) host.audioOutputStop(address.load());
    Serial.printf("PAUSED %u\r\n", unsigned(ok && (!(activeDirections & 2) || !host.audioInputRunning(address.load())) && (!(activeDirections & 1) || !host.audioOutputRunning(address.load()))));
  } else if (command == "stop") { host.end(); Serial.println(!host.ready() ? "STOPPED" : "STOP_FAILED"); }
}
