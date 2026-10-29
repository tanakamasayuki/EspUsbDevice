#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
EspUsbHost host;
std::atomic<uint8_t> address{0};
std::atomic<unsigned> armed{0}, received{0};
std::atomic<bool> valid{true}, silenceWatch{false};
std::atomic<unsigned> unexpected{0};
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
  } else if (k == 2) wanted[0][1] = p == 0 ? 1 : p == 1 ? 2 : p == 3 ? 3 : 0;
}
// Decode the array's logical selector -> Generic Desktop usage mapping from
// the descriptor fetched over EP0. Raw echo alone cannot prove usage meaning.
bool systemDescriptor(unsigned (&mapped)[3], bool &nullRelease) {
  uint8_t data[512]; size_t actual = 0;
  if (!host.vendorControlTransfer(0x81, 6, 0x2200, 0, data, sizeof(data), &actual, address.load(), 1000)) return false;
  uint32_t page = 0, id = 0, bits = 0, count = 0, usageMin = 0, usageMax = 0;
  int32_t logicalMin = 0, logicalMax = 0;
  bool hasMin = false, hasMax = false, found = false, ok = true;
  for (size_t offset = 0; offset < actual;) {
    const uint8_t prefix = data[offset++];
    if (prefix == 0xfe) return false;
    const unsigned bytes = (prefix & 3) == 3 ? 4 : prefix & 3;
    if (bytes > actual - offset) return false;
    uint32_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) value |= uint32_t(data[offset++]) << (8 * i);
    int32_t signedValue = value;
    if (bytes && bytes < 4 && (value & (1u << (8 * bytes - 1)))) signedValue = int32_t(value | (~0u << (8 * bytes)));
    const unsigned type = (prefix >> 2) & 3, tag = prefix >> 4;
    if (type == 1) {
      if (tag == 0) page = value;
      else if (tag == 1) logicalMin = signedValue;
      else if (tag == 2) logicalMax = logicalMin < 0 ? signedValue : int32_t(value);
      else if (tag == 7) bits = value;
      else if (tag == 8) id = value;
      else if (tag == 9) count = value;
      else if (tag == 10 || tag == 11) return false; // This probe's three descriptors use no global stack.
    } else if (type == 2) {
      if (tag == 1) { usageMin = value; hasMin = true; }
      else if (tag == 2) { usageMax = value; hasMax = true; }
    } else if (type == 0) {
      if (tag == 8 && page == 1 && id == 5) {
        ok &= !found && bits == 8 && count == 1 && !(value & 3) && hasMin && hasMax;
        found = true;
        nullRelease = (value & 0x40) && (0 < logicalMin || 0 > logicalMax);
        for (unsigned selector = 1; selector <= 3; ++selector) {
          if (int32_t(selector) < logicalMin || int32_t(selector) > logicalMax) { ok = false; continue; }
          mapped[selector - 1] = usageMin + int32_t(selector) - logicalMin;
          ok &= mapped[selector - 1] <= usageMax && mapped[selector - 1] == 0x80 + selector;
        }
      }
      hasMin = hasMax = false;
    }
  }
  return found && ok && nullRelease;
}
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    if (info.vid == 0x303a && info.pid == 0x4000 && strcmp(info.product, "v3 typed HID") == 0) address.store(info.address);
  });
  host.onHIDInput([](const EspUsbHostHIDInput &event) {
    if (silenceWatch.load()) { ++unexpected; return; }
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
  } else if (command == "system_descriptor") {
    unsigned mapped[3] = {}; bool nullRelease = false;
    const bool ok = systemDescriptor(mapped, nullRelease);
    Serial.printf("SYSTEM_DESCRIPTOR ok=%u power=%u sleep=%u wake=%u null=%u\r\n", unsigned(ok), mapped[0], mapped[1], mapped[2], unsigned(nullRelease));
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
  } else if (command == "silence_start") {
    armed.store(0); unexpected.store(0); silenceWatch.store(true); Serial.println("WATCHING");
  } else if (command == "silence_check") {
    delay(50);
    uint8_t snapshot[2] = {}; size_t actual = 0;
    bool ok = host.vendorControlTransfer(0xa1, 1, 0x0105, 0, snapshot, sizeof(snapshot), &actual, address.load(), 1000);
    ok &= actual == 2 && !memcmp(snapshot, wanted[0], 2) && !unexpected.load();
    silenceWatch.store(false);
    Serial.printf("SILENCE ok=%u unexpected=%u\r\n", unsigned(ok), unexpected.load());
  } else if (command == "stop") {
    armed.store(0); if (started) host.end(); started = false; address.store(0);
    Serial.println("STOPPED");
  }
}
