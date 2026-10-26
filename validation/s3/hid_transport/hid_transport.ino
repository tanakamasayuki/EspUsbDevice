#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>
EspUsbHost host;
std::atomic<uint8_t> address{0};
std::atomic<unsigned> expected{0}, received{0};
std::atomic<bool> valid{true};
unsigned reportBytes = 0;
bool started = false, keyboardMode = false;
uint8_t raw[1100];
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    if (info.vid == 0x303a && info.pid == 0x4000 && strcmp(info.product, "v3 HID transport") == 0) address.store(info.address);
  });
  host.onHIDInput([](const EspUsbHostHIDInput &event) {
    const unsigned offset = received.load(), wanted = expected.load();
    if (!wanted) return;
    if (!event.data || event.length > wanted - std::min(offset, wanted) || offset + event.length > sizeof(raw)) { valid.store(false); return; }
    memcpy(raw + offset, event.data, event.length);
    received.store(offset + event.length);
  });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command.startsWith("start ") || command == "keyboard") {
    keyboardMode = command == "keyboard";
    reportBytes = keyboardMode ? 0 : command.substring(6).toInt();
    address.store(0); expected.store(0); received.store(0); valid.store(true);
    started = host.begin();
    const uint32_t deadline = millis() + 10000;
    while (started && !address.load() && int32_t(deadline - millis()) > 0) delay(1);
    Serial.printf("HOST_READY %u\r\n", unsigned(started && address.load()));
  } else if (command.startsWith("arm ")) {
    expected.store(0); received.store(0); valid.store(true);
    expected.store(command.substring(4).toInt());
    Serial.println("ARMED");
  } else if (command == "input") {
    const uint32_t deadline = millis() + 3000;
    while (received.load() < expected.load() && int32_t(deadline - millis()) > 0) delay(1);
    bool ok = valid.load() && received.load() == expected.load();
    if (keyboardMode) ok &= expected.load() == 8 ? raw[2] == 4 : expected.load() == 29 && (raw[1] & 0x10);
    else {
      ok &= raw[0] == 6;
      for (unsigned i = 0; i < reportBytes; ++i) ok &= raw[i + 1] == uint8_t(i * 37 + reportBytes);
    }
    Serial.printf("INPUT %u bytes=%u\r\n", unsigned(ok), received.load());
    expected.store(0);
  } else if (command == "output") {
    uint8_t data[1024]; data[0] = 6;
    for (unsigned i = 0; i < reportBytes; ++i) data[i + 1] = uint8_t(i * 37 + reportBytes);
    size_t actual = 0;
    bool ok = host.sendHIDVendorOutput(data, reportBytes + 1, address.load());
    ok &= host.vendorControlTransfer(0x21, 9, 0x0206, 0, data, reportBytes + 1, &actual, address.load(), 1000);
    ok &= host.vendorControlTransfer(0x21, 9, 0x0206, 0, data + 1, reportBytes, &actual, address.load(), 1000);
    ok &= host.vendorControlTransfer(0x21, 9, 0x0306, 0, data, reportBytes + 1, &actual, address.load(), 1000);
    // Invalid report ID, invalid type, and mismatched prefix must stall.
    ok &= !host.vendorControlTransfer(0x21, 9, 0x02ff, 0, data, reportBytes + 1, &actual, address.load(), 1000);
    ok &= !host.vendorControlTransfer(0x21, 9, 0x0106, 0, data, reportBytes + 1, &actual, address.load(), 1000);
    data[0] = 7;
    ok &= !host.vendorControlTransfer(0x21, 9, 0x0206, 0, data, reportBytes + 1, &actual, address.load(), 1000);
    uint8_t rate = 0;
    ok &= host.vendorControlTransfer(0x21, 10, 0x0706, 0, nullptr, 0, &actual, address.load(), 1000);
    ok &= host.vendorControlTransfer(0xa1, 2, 6, 0, &rate, 1, &actual, address.load(), 1000) && rate == 7;
    Serial.printf("OUTPUT_SENT %u\r\n", unsigned(ok));
  } else if (command.startsWith("protocol ")) {
    const unsigned protocol = command.substring(9).toInt();
    size_t actual = 0; uint8_t current = 255, leds = 5;
    bool ok = host.vendorControlTransfer(0x21, 11, protocol, 0, nullptr, 0, &actual, address.load(), 1000);
    ok &= host.vendorControlTransfer(0xa1, 3, 0, 0, &current, 1, &actual, address.load(), 1000) && current == protocol;
    ok &= host.vendorControlTransfer(0x21, 9, 0x0200, 0, &leds, 1, &actual, address.load(), 1000);
    Serial.printf("PROTOCOL_SET %u value=%u\r\n", unsigned(ok), unsigned(current));
  } else if (command == "stop") {
    if (started) host.end(); started = false; address.store(0); expected.store(0);
    Serial.println("STOPPED");
  }
}
