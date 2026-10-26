#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
EspUsbHost host;
std::atomic<uint8_t> address{0};
std::atomic<unsigned> reports{0}, invalid{0};
bool started = false;
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    if (info.vid == 0x303a && info.pid == 0x4000 && strcmp(info.product, "v3 descriptor storage") == 0) address.store(info.address);
  });
  host.onHIDInput([](const EspUsbHostHIDInput &event) {
    if (!event.data || event.length != 2 || event.data[0] < 1 || event.data[0] > 2 || event.data[1] != 0x40 + event.data[0]) { ++invalid; return; }
    reports.fetch_or(1u << event.data[0]);
  });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "vendor" || command == "hid") {
    address.store(0); reports.store(0); invalid.store(0);
    started = host.begin();
    const uint32_t deadline = millis() + 10000;
    while (started && !address.load() && int32_t(deadline - millis()) > 0) delay(1);
    const bool ready = started && address.load() && (command == "hid" || host.vendorOpen(address.load(), 0, ESP_USB_HOST_VENDOR_READ_ON_DEMAND));
    Serial.printf("HOST_READY %u\r\n", unsigned(ready));
  } else if (command.startsWith("exchange ")) {
    const unsigned length = command.substring(9).toInt();
    uint8_t data[129], response[129]; size_t count = 0;
    bool ok = length && length <= sizeof(data);
    for (size_t i = 0; ok && i < length; ++i) data[i] = uint8_t(i * 37 + length);
    ok &= host.vendorWrite(data, length, address.load());
    const uint32_t deadline = millis() + 3000;
    while (ok && count < length && int32_t(deadline - millis()) > 0) {
      size_t actual = 0;
      ok = host.vendorReadSync(response + count, length - count, &actual, 3000, address.load()); count += actual;
    }
    Serial.printf("ROUNDTRIP %u ok=%u\r\n", length, unsigned(ok && count == length && memcmp(data, response, length) == 0));
  } else if (command == "reports") {
    const uint32_t deadline = millis() + 3000;
    while (reports.load() != 6 && int32_t(deadline - millis()) > 0) delay(1);
    Serial.printf("REPORTS mask=%u invalid=%u\r\n", reports.load(), invalid.load());
  } else if (command == "stop") {
    if (started) host.end();
    started = false; address.store(0);
    Serial.println("STOPPED");
  }
}
