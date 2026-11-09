#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
#include "text_vectors.h"
EspUsbHost host;
std::atomic<uint8_t> address{0};
std::atomic<unsigned> reports{0}, invalid{0}, descriptorBytes{0};
std::atomic<bool> descriptorValid{false};
bool started = false;
bool wireText() {
  uint8_t data[260]; size_t actual = 0;
  const auto getString = [&](uint8_t index, uint16_t language = 0x409) {
    memset(data, 0xa5, sizeof(data)); actual = 0;
    return host.vendorControlTransfer(0x80, 6, 0x0300 | index, language, data, 256, &actual, address.load(), 1000);
  };
  bool ok = getString(0, 0) && actual == 4 && data[0] == 4 && data[2] == 9 && data[3] == 4;
  ok &= getString(1) && actual == 14 && data[0] == 14 && data[1] == 3 && memcmp(data + 2, text_probe::manufacturer, 12) == 0;
  ok &= getString(3) && actual == 254 && data[0] == 254 && data[1] == 3 && data[254] == 0xa5;
  for (unsigned i = 0; i < 126; ++i) ok &= data[2 + 2 * i] == 's' && data[3 + 2 * i] == 0;
  ok &= getString(5) && actual == 6 && data[0] == 6 && memcmp(data + 2, text_probe::name, 4) == 0;
  ok &= getString(6) && actual == 26;
  const char *mac = "0202846A9600";
  for (unsigned i = 0; i < 12; ++i) ok &= data[2 + 2*i] == mac[i] && data[3 + 2*i] == 0;
  ok &= getString(7) && actual == 20;
  for (unsigned i = 0; i < 9; ++i) ok &= data[2 + 2*i] == "auxiliary"[i] && data[3 + 2*i] == 0;
  ok &= !getString(8) && getString(6);
  ok &= !getString(255) && getString(1);
  ok &= !getString(1, 0x411) && getString(1);
  actual = 0;
  ok &= host.vendorControlTransfer(0x80, 6, 0x0303, 0x409, data, 2, &actual, address.load(), 1000) && actual == 2 && data[0] == 254;
  char url[261]; text_probe::makeUrl(url);
  const auto getUrl = [&]() {
    memset(data, 0xa5, sizeof(data)); actual = 0;
    return host.vendorControlIn(1, 1, 2, data, 256, &actual, address.load(), 1000) &&
        actual == 255 && data[0] == 255 && data[1] == 3 && data[2] == 1 &&
        memcmp(data + 3, url + 8, 252) == 0 && data[255] == 0xa5;
  };
  ok &= getUrl();
  ok &= !host.vendorControlIn(1, 2, 2, data, 256, &actual, address.load(), 1000) && getUrl();
  ok &= !host.vendorControlIn(1, 1, 1, data, 256, &actual, address.load(), 1000) && getUrl();
  ok &= !host.vendorControlTransfer(0xc1, 1, 1, 2, data, 256, &actual, address.load(), 1000) && getUrl();
  ok &= !host.vendorControlOut(1, 1, 2, nullptr, 0, address.load(), 1000) && getUrl();
  actual = 0;
  ok &= host.vendorControlIn(1, 1, 2, data, 3, &actual, address.load(), 1000) && actual == 3 && data[0] == 255;
  return ok;
}
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    if (info.vid == 0x303a && info.pid == 0x4000 && strcmp(info.product, "v3 descriptor storage") == 0) address.store(info.address);
  });
  host.onHIDReportDescriptor([](const EspUsbHostHIDReportDescriptor &descriptor) {
    descriptorValid.store(descriptor.length == descriptor.reportedLength && descriptor.length > 255);
    descriptorBytes.store(descriptor.length);
  });
  host.onHIDInput([](const EspUsbHostHIDInput &event) {
    if (!event.data || event.length != 2 || event.data[0] < 1 || event.data[0] > 2 || event.data[1] != 0x40 + event.data[0]) { ++invalid; return; }
    reports.fetch_or(1u << event.data[0]);
  });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "vendor" || command == "hid" || command == "text") {
    address.store(0); reports.store(0); invalid.store(0); descriptorBytes.store(0); descriptorValid.store(false);
    started = host.begin();
    const uint32_t deadline = millis() + 10000;
    while (started && !address.load() && int32_t(deadline - millis()) > 0) delay(1);
    const bool ready = started && address.load() && (command == "hid" || host.vendorOpen(address.load(), 0, ESP_USB_HOST_VENDOR_READ_ON_DEMAND));
    Serial.printf("HOST_READY %u\r\n", unsigned(ready));
  } else if (command == "hid_descriptor") {
    const uint32_t deadline = millis() + 3000;
    while (!descriptorBytes.load() && int32_t(deadline - millis()) > 0) delay(1);
    Serial.printf("HID_DESCRIPTOR ok=%u bytes=%u\r\n", unsigned(descriptorValid.load()), descriptorBytes.load());
  } else if (command == "read_text") {
    Serial.printf("TEXT_WIRE %u\r\n", unsigned(wireText()));
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
