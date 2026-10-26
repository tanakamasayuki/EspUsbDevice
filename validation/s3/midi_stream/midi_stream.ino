#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
#include "packets.h"
EspUsbHost host;
std::atomic<uint8_t> address{0};
std::atomic<unsigned> count{0}, bad{0}, serialCount{0}, keyDown{0}, keyUp{0};
unsigned cables = 1, producerSequence[2] = {}; bool started = false, helper = false, parallel = false, ignore = false, semantics = false;
unsigned helperCable = 0;
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    if (strcmp(info.product, "v3 MIDI stream") == 0) address.store(info.address);
  });
  host.onSerialData([](const EspUsbHostSerialData &event) {
    for (size_t i = 0; i < event.length; ++i) {
      const unsigned index = serialCount.load();
      if (index >= 3 || event.data[i] != uint8_t("mix"[index])) ++bad;
      ++serialCount;
    }
  });
  host.onHIDInput([](const EspUsbHostHIDInput &event) {
    if (!event.data || event.length != 8) { ++bad; return; }
    if (event.data[2] == 4) ++keyDown;
    else if (event.data[2] == 0) ++keyUp;
    else ++bad;
  });
  host.onMidiMessage([](const EspUsbHostMidiMessage &event) {
    if (ignore) return;
    if (semantics) {
      const unsigned index = count.load(); uint8_t expected[4] = {};
      if (index < midiHelperCount) { memcpy(expected, midiHelperExpected[index], 4); expected[0] |= uint8_t(helperCable << 4); }
      if (index >= midiHelperCount || !event.raw || event.length != 4 || memcmp(event.raw, expected, 4) != 0) ++bad;
      ++count; return;
    }
    if (parallel) {
      if (!event.raw || event.length != 4 || event.raw[0] != 9 || (event.raw[1] & 0xfe) != 0x90) { ++bad; ++count; return; }
      const unsigned source = event.raw[1] & 1, seq = producerSequence[source]++;
      if (seq >= 100 || event.raw[2] != seq || event.raw[3] != (seq ^ 0x55)) ++bad;
      ++count; return;
    }
    uint8_t expected[4]; midiPattern(expected, count.load(), cables);
    if (helper) { expected[0] = 9; expected[1] = 0x92; expected[2] = 60; expected[3] = 100; }
    if (!event.raw || event.length != 4 || memcmp(event.raw, expected, 4) != 0) ++bad;
    ++count;
  });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command.startsWith("start ")) {
    unsigned inCables = 1, outCables = 0;
    sscanf(command.c_str(), "start %u %u", &inCables, &outCables); if (!outCables) outCables = inCables;
    cables = outCables; count.store(0); bad.store(0); helper = parallel = ignore = semantics = false; address.store(0);
    started = host.begin(); const uint32_t deadline = millis() + 10000;
    while (started && !host.midiReady(address.load()) && int32_t(deadline - millis()) > 0) delay(1);
    EspUsbHostMidiPortInfo info;
    const bool ready = started && address.load() && host.getMidiPortInfo(info, address.load()) && info.inCableCount == inCables && info.outCableCount == outCables;
    Serial.printf("HOST_READY %u\r\n", unsigned(ready));
  } else if (command.startsWith("send ")) {
    const unsigned events = command.substring(5).toInt(); uint8_t data[4 * 64]; bool ok = events <= 64;
    for (unsigned i = 0; ok && i < events; ++i) midiPattern(data + 4 * i, i, cables);
    Serial.printf("SENT %u\r\n", unsigned(ok && host.midiSend(data, events * 4, address.load())));
  } else if (command.startsWith("wait ")) {
    const unsigned events = command.substring(5).toInt(); const uint32_t deadline = millis() + 5000;
    while (count.load() < events && int32_t(deadline - millis()) > 0) delay(1);
    Serial.printf("RECEIVED %u bad=%u\r\n", count.load(), bad.load());
  } else if (command == "clear") {
    count.store(0); bad.store(0); helper = parallel = false; Serial.println("CLEARED");
  } else if (command == "quiet") {
    delay(20); Serial.printf("QUIET %u\r\n", unsigned(!count.load() && !bad.load()));
  } else if (command == "parallel") {
    count.store(0); bad.store(0); producerSequence[0] = producerSequence[1] = 0; parallel = true; Serial.println("PARALLEL_ARMED");
  } else if (command == "ignore") {
    ignore = true; Serial.println("IGNORING");
  } else if (command.startsWith("semantics ")) {
    count.store(0); bad.store(0); helperCable = command.substring(10).toInt(); semantics = true; Serial.println("SEMANTICS_ARMED");
  } else if (command == "helper") {
    count.store(0); bad.store(0); helper = true; Serial.println("HELPER_ARMED");
  } else if (command == "second") {
    uint8_t data[4]; midiPattern(data, 0);
    const bool ok = host.vendorOpen(address.load(), 3, ESP_USB_HOST_VENDOR_READ_ON_DEMAND) && host.vendorWrite(data, 4, address.load());
    Serial.printf("SECOND_SENT %u\r\n", unsigned(ok));
  } else if (command == "second_read") {
    uint8_t data[64], expected[4]; size_t actual = 0; midiPattern(expected, 0);
    const bool ok = host.vendorReadSync(data, sizeof(data), &actual, 3000, address.load()) && actual == 4 && memcmp(data, expected, 4) == 0;
    Serial.printf("SECOND_RECEIVED %u\r\n", unsigned(ok));
  } else if (command == "composite") {
    serialCount.store(0); keyDown.store(0); keyUp.store(0);
    Serial.printf("COMPOSITE_SENT %u\r\n", unsigned(host.sendSerial(reinterpret_cast<const uint8_t *>("mix"), 3, address.load())));
  } else if (command == "composite_result") {
    const uint32_t deadline = millis() + 3000;
    while ((serialCount.load() < 3 || !keyDown.load() || !keyUp.load()) && int32_t(deadline - millis()) > 0) delay(1);
    Serial.printf("COMPOSITE_RECEIVED %u\r\n", unsigned(serialCount.load() == 3 && keyDown.load() && keyUp.load() && !bad.load()));
  } else if (command == "invalid") {
    const uint8_t bytes[] = {0xf9,0x90,1,2, 0x00,1,2,3, 0x09,0x90,0,7, 0xab};
    Serial.printf("INVALID_SENT %u\r\n", unsigned(host.midiSend(bytes, sizeof(bytes), address.load())));
  } else if (command == "stop") {
    if (started) host.end(); started = false; address.store(0); Serial.println("STOPPED");
  }
}
