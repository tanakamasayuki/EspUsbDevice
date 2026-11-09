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
// Interpret the actual EP0 descriptor so correct raw bytes cannot hide a
// different logical/physical meaning. Global stacks are local to this probe.
struct HidGlobals {
  uint32_t page = 0, id = 0, bits = 0, count = 0, unit = 0;
  int32_t low = 0, high = 0, physicalLow = 0, physicalHigh = 0;
};
bool descriptorSemantics(bool system, unsigned (&mapped)[3], bool &released, bool &axes, bool &buttons) {
  uint8_t data[512]; size_t actual = 0;
  if (!host.vendorControlTransfer(0x81, 6, 0x2200, 0, data, sizeof(data), &actual, address.load(), 1000)) return false;
  HidGlobals g, stack[4]; unsigned depth = 0;
  uint32_t usageMin = 0, usageMax = 0, usages[8] = {};
  unsigned usageCount = 0;
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
      if (tag == 0) g.page = value;
      else if (tag == 1) g.low = signedValue;
      else if (tag == 2) g.high = g.low < 0 ? signedValue : int32_t(value);
      else if (tag == 3) g.physicalLow = signedValue;
      else if (tag == 4) g.physicalHigh = g.physicalLow < 0 ? signedValue : int32_t(value);
      else if (tag == 6) g.unit = value;
      else if (tag == 7) g.bits = value;
      else if (tag == 8) g.id = value;
      else if (tag == 9) g.count = value;
      else if (tag == 10) { if (depth == 4) return false; stack[depth++] = g; }
      else if (tag == 11) { if (!depth) return false; g = stack[--depth]; }
    } else if (type == 2) {
      if (tag == 0) { if (usageCount == 8) return false; usages[usageCount++] = value; }
      else if (tag == 1) { usageMin = value; hasMin = true; }
      else if (tag == 2) { usageMax = value; hasMax = true; }
    } else if (type == 0) {
      if (tag == 8 && system && g.page == 1 && g.id == 5) {
        ok &= !found && g.bits == 8 && g.count == 1 && !(value & 3) && hasMin && hasMax;
        found = true;
        // HID arrays recommend logical selectors starting at 1; zero means
        // no asserted element. The Null State bit applies to variable fields.
        released = 0 < g.low;
        for (unsigned selector = 1; selector <= 3; ++selector) {
          if (int32_t(selector) < g.low || int32_t(selector) > g.high) { ok = false; continue; }
          mapped[selector - 1] = usageMin + int32_t(selector) - g.low;
          ok &= mapped[selector - 1] <= usageMax && mapped[selector - 1] == 0x80 + selector;
        }
      } else if (tag == 8 && !system && g.id == 3) {
        if (g.page == 1 && usageCount == 6) {
          const uint32_t expectedAxes[] = {0x30,0x31,0x32,0x35,0x33,0x34};
          axes = g.bits == 8 && g.count == 6 && g.low == -127 && g.high == 127 && g.unit == 0 &&
                 !memcmp(usages, expectedAxes, sizeof(expectedAxes)) &&
                 ((g.physicalLow == 0 && g.physicalHigh == 0) || (g.physicalLow == g.low && g.physicalHigh == g.high));
        } else if (g.page == 1 && usageCount == 1 && usages[0] == 0x39) {
          ok &= !found && g.bits == 8 && g.count == 1 && (value & 3) == 2 && g.unit == 0x14 && g.high > g.low;
          found = true; released = (value & 0x40) && 0 < g.low;
          const int selectors[] = {1,3,8}; const unsigned degrees[] = {0,90,315};
          for (unsigned i = 0; i < 3; ++i) {
            if (selectors[i] < g.low || selectors[i] > g.high || g.high <= g.low) { ok = false; continue; }
            mapped[i] = g.physicalLow + (selectors[i] - g.low) * (g.physicalHigh - g.physicalLow) / (g.high - g.low);
            ok &= mapped[i] == degrees[i];
          }
        } else if (g.page == 9) {
          buttons = g.bits == 1 && g.count == 32 && g.low == 0 && g.high == 1 && !g.unit &&
                    hasMin && hasMax && usageMin == 1 && usageMax == 32 && g.physicalLow == 0 &&
                    (g.physicalHigh == 0 || g.physicalHigh == 1);
        }
      }
      hasMin = hasMax = false; usageCount = 0;
    }
  }
  return !depth && found && ok && released && (system || (axes && buttons));
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
  } else if (command == "system_descriptor" || command == "gamepad_descriptor") {
    unsigned mapped[3] = {}; bool released = false, axes = false, buttons = false;
    const bool system = command == "system_descriptor";
    const bool ok = descriptorSemantics(system, mapped, released, axes, buttons);
    if (system) Serial.printf("SYSTEM_DESCRIPTOR ok=%u power=%u sleep=%u wake=%u release=%u\r\n", unsigned(ok), mapped[0], mapped[1], mapped[2], unsigned(released));
    else Serial.printf("GAMEPAD_DESCRIPTOR ok=%u up=%u right=%u left_up=%u null=%u axes=%u buttons=%u\r\n", unsigned(ok), mapped[0], mapped[1], mapped[2], unsigned(released), unsigned(axes), unsigned(buttons));
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
