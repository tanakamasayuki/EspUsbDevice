#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
EspUsbHost host;
std::atomic<uint8_t> address{0};
std::atomic<unsigned> armed{0}, received{0};
std::atomic<bool> valid{true};
unsigned kind = 0, phase = 0;
bool started = false;
uint8_t wanted[2][12] = {};
size_t reportLength = 0;
void expected(unsigned k, unsigned p) {
  memset(wanted, 0, sizeof(wanted));
  wanted[0][0] = wanted[1][0] = 3 + k;
  reportLength = k == 0 ? 12 : k == 1 ? 3 : 2;
  if (k == 0 && p == 0) {
    const uint8_t values[] = {0x81,0x7f,0xff,1,0xc0,0x40,8,8,4,2,0x81};
    memcpy(wanted[0] + 1, values, sizeof(values));
  } else if (k == 0 && p == 1) {
    const uint8_t values[] = {12,0xe9,34,0xd3,56,0xbd,3,0x98,0xba,0xdc,0xfe};
    memcpy(wanted[0] + 1, values, sizeof(values));
  } else if (k == 1) {
    const uint16_t usage = p == 0 ? 0x123 : p == 1 ? 0xe9 : p == 3 ? 0xea : 0;
    wanted[0][1] = usage; wanted[0][2] = usage >> 8;
  } else if (k == 2) wanted[0][1] = p == 0 ? 0x81 : p == 1 ? 0x82 : p == 3 ? 0x83 : 0;
}
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    if (info.vid == 0x303a && info.pid == 0x4000 && strcmp(info.product, "v3 typed HID") == 0) address.store(info.address);
  });
  host.onHIDInput([](const EspUsbHostHIDInput &event) {
    const unsigned count = armed.load(), offset = received.load();
    if (!count) return;
    if (offset >= count || !event.data || event.length != reportLength ||
        memcmp(event.data, wanted[std::min(offset, 1u)], reportLength)) valid.store(false);
    received.store(offset + 1);
  });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start") {
    address.store(0); armed.store(0); started = host.begin();
    const uint32_t until = millis() + 10000;
    while (started && !address.load() && int32_t(until - millis()) > 0) delay(1);
    Serial.printf("HOST_READY %u\r\n", unsigned(started && address.load()));
  } else if (command.startsWith("arm ")) {
    armed.store(0);
    sscanf(command.c_str(), "arm %u %u", &kind, &phase);
    expected(kind, phase); received.store(0); valid.store(true);
    armed.store(phase == 3 ? 2 : 1);
    Serial.println("ARMED");
  } else if (command == "check") {
    const unsigned count = armed.load();
    const uint32_t until = millis() + 3000;
    while (received.load() < count && int32_t(until - millis()) > 0) delay(1);
    bool ok = count && valid.load() && received.load() == count;
    uint8_t snapshot[32] = {}; size_t actual = 0;
    ok &= host.vendorControlTransfer(0xa1, 1, 0x0100 | (3 + kind), 0,
                                    snapshot, sizeof(snapshot), &actual, address.load(), 1000);
    ok &= actual == reportLength && !memcmp(snapshot, wanted[count == 2 ? 1 : 0], reportLength);
    Serial.printf("REPORT kind=%u phase=%u ok=%u count=%u bytes=%u\r\n", kind, phase, unsigned(ok), received.load(), unsigned(actual));
    armed.store(0);
  } else if (command == "stop") {
    armed.store(0); if (started) host.end(); started = false; address.store(0);
    Serial.println("STOPPED");
  }
}
