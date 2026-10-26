#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
EspUsbHost host;
std::atomic<uint8_t> address{0};
struct Statistics {
  std::atomic<unsigned> count{0};
  std::atomic<uint32_t> first{0}, last{0};
} stats[2];
std::atomic<unsigned> bad{0};
uint8_t assembly[130]; size_t used = 0, wanted = 0;
bool started = false;
void clearStats() {
  for (auto &s : stats) { s.count.store(0); s.first.store(0); s.last.store(0); }
}
bool get(uint8_t type, uint8_t id, uint8_t value, size_t limit = 256) {
  uint8_t data[256]; size_t actual = 0;
  const size_t bytes = id == 6 ? 8 : 130;
  memset(data, 0xa5, sizeof(data));
  if (!host.vendorControlTransfer(0xa1, 1, uint16_t(type << 8) | id, 0, data, limit, &actual, address.load(), 1000) || actual != std::min(bytes, limit) || data[0] != id) return false;
  for (size_t i = 1; i < actual; ++i) if (data[i] != value) return false;
  return true;
}
bool rate(uint8_t id, uint8_t value) {
  size_t actual = 0; uint8_t read = 255;
  if (!host.vendorControlTransfer(0x21, 10, uint16_t(value << 8) | id, 0, nullptr, 0, &actual, address.load(), 1000)) return false;
  return !id || (host.vendorControlTransfer(0xa1, 2, id, 0, &read, 1, &actual, address.load(), 1000) && actual == 1 && read == value);
}
void startHost() {
  address.store(0); used = wanted = 0; bad.store(0); clearStats();
  started = host.begin();
  const uint32_t deadline = millis() + 10000;
  while (started && !address.load() && int32_t(deadline - millis()) > 0) delay(1);
  Serial.printf("HOST_READY %u\r\n", unsigned(started && address.load()));
}
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    if (info.vid == 0x303a && info.pid == 0x4000 && strcmp(info.product, "v3 HID report state") == 0) address.store(info.address);
  });
  host.onHIDInput([](const EspUsbHostHIDInput &event) {
    if (!event.length) return;
    if (!event.data) { ++bad; return; }
    if (!used) {
      if (event.data[0] != 6 && event.data[0] != 1) { ++bad; return; }
      wanted = event.data[0] == 6 ? 8 : 130;
    }
    if (!event.data || event.length > wanted - used) { ++bad; used = 0; return; }
    memcpy(assembly + used, event.data, event.length); used += event.length;
    if (used != wanted) return;
    const unsigned index = assembly[0] == 6 ? 0 : 1;
    const uint8_t value = index ? 0x41 : 0x46;
    for (size_t i = 1; i < wanted; ++i) if (assembly[i] != value) { ++bad; break; }
    const uint32_t now = micros();
    if (!stats[index].count.load()) stats[index].first.store(now);
    stats[index].last.store(now); ++stats[index].count;
    used = 0;
  });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start") startHost();
  else if (command == "initial" || command == "reset_state") {
    bool ok = true;
    for (uint8_t id : {6, 1}) {
      ok &= get(1, id, 0) && get(2, id, 0);
      ok &= get(3, id, command == "initial" ? (id == 6 ? 0x16 : 0x21) : (id == 6 ? 0x76 : 0x71));
    }
    uint8_t data[8]; size_t actual = 0;
    ok &= !host.vendorControlTransfer(0xa1, 1, 0x0406, 0, data, sizeof(data), &actual, address.load(), 1000);
    ok &= !host.vendorControlTransfer(0xa1, 1, 0x01ff, 0, data, sizeof(data), &actual, address.load(), 1000);
    ok &= get(3, 6, command == "initial" ? 0x16 : 0x76, 3);
    Serial.printf("STATE %u\r\n", unsigned(ok));
  } else if (command == "inputs") {
    const uint32_t deadline = millis() + 3000;
    while ((!stats[0].count.load() || !stats[1].count.load()) && int32_t(deadline - millis()) > 0) delay(1);
    Serial.printf("INPUT_STATE %u\r\n", unsigned(stats[0].count.load() && stats[1].count.load() && !bad.load() && get(1, 6, 0x46) && get(1, 1, 0x41)));
  } else if (command == "put") {
    bool ok = true; uint8_t data[130]; size_t actual = 0;
    for (uint8_t id : {6, 1}) {
      const size_t bytes = id == 6 ? 8 : 130;
      data[0] = id; memset(data + 1, id == 6 ? 0x56 : 0x51, bytes - 1);
      ok &= host.vendorControlTransfer(0x21, 9, 0x0200 | id, 0, data + (id == 1 ? 1 : 0), bytes - (id == 1 ? 1 : 0), &actual, address.load(), 1000);
      data[0] = id; memset(data + 1, id == 6 ? 0x66 : 0x61, bytes - 1);
      ok &= host.vendorControlTransfer(0x21, 9, 0x0300 | id, 0, data, bytes, &actual, address.load(), 1000);
      ok &= get(2, id, id == 6 ? 0x56 : 0x51) && get(3, id, id == 6 ? 0x66 : 0x61);
    }
    Serial.printf("PUT %u\r\n", unsigned(ok));
  } else if (command == "feature") {
    Serial.printf("FEATURE_STATE %u\r\n", unsigned(get(3, 6, 0x76) && get(3, 1, 0x71) && get(2, 6, 0x56) && get(2, 1, 0x51) && get(1, 6, 0x46) && get(1, 1, 0x41)));
  } else if (command == "stress") {
    bool ok = true; uint8_t data[256]; size_t actual = 0;
    for (unsigned n = 0; n < 200 && ok; ++n) {
      ok &= host.vendorControlTransfer(0xa1, 1, 0x0301, 0, data, sizeof(data), &actual, address.load(), 1000) && actual == 130 && data[0] == 1 && (data[1] == 0x31 || data[1] == 0x32);
      for (size_t i = 2; ok && i < actual; ++i) ok &= data[i] == data[1];
    }
    Serial.printf("COHERENT %u reads=200\r\n", unsigned(ok));
  } else if (command == "idle") {
    clearStats(); bool ok = rate(6, 5) && rate(1, 25);
    delay(350);
    const unsigned small = stats[0].count.load(), large = stats[1].count.load();
    const uint32_t a = small > 1 ? (stats[0].last.load() - stats[0].first.load()) / (small - 1) : 0;
    const uint32_t b = large > 1 ? (stats[1].last.load() - stats[1].first.load()) / (large - 1) : 0;
    ok &= small >= 10 && large >= 3 && a >= 15000 && a <= 35000 && b >= 70000 && b <= 140000 && !bad.load();
    Serial.printf("IDLE ok=%u small=%u large=%u avg_small_us=%u avg_large_us=%u\r\n", unsigned(ok), small, large, unsigned(a), unsigned(b));
    ok &= rate(6, 0); delay(10);
    const unsigned stopped = stats[0].count.load(), continuing = stats[1].count.load();
    delay(140);
    Serial.printf("ISOLATION %u\r\n", unsigned(ok && stats[0].count.load() == stopped && stats[1].count.load() > continuing));
    ok &= rate(0, 0); delay(10);
    const unsigned before = stats[0].count.load() + stats[1].count.load(); delay(140);
    Serial.printf("QUIET %u\r\n", unsigned(ok && stats[0].count.load() + stats[1].count.load() == before));
  } else if (command == "reconnect") {
    if (started) host.end(); started = false; startHost();
  } else if (command == "stop") {
    if (started) host.end(); started = false; address.store(0);
    Serial.println("STOPPED");
  }
}
