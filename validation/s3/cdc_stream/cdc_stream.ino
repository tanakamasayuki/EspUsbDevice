#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>

EspUsbHost host;
std::atomic<uint8_t> address{0};
uint8_t input[2][256];
std::atomic<size_t> counts[2];
bool started = false;
std::atomic<unsigned> keyPresses{0}, keyReleases{0};

void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    if (info.vid == 0x303a && info.pid == 0x4000 && strcmp(info.product, "v3 CDC stream probe") == 0) address.store(info.address);
  });
  host.onDeviceDisconnected([](const EspUsbHostDeviceInfo &) { address.store(0); });
  host.onKeyboard([](const EspUsbHostKeyboardEvent &event) {
    if (event.keycode == 4) {
      if (event.pressed) ++keyPresses;
      if (event.released) ++keyReleases;
    }
  });
  host.onSerialData([](const EspUsbHostSerialData &data) {
    if (data.port >= 2 || !data.length) return;
    const size_t n = counts[data.port].load();
    if (n + data.length <= sizeof(input[0])) memcpy(input[data.port] + n, data.data, data.length);
    counts[data.port].store(n + data.length, std::memory_order_release);
  });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start" || command.startsWith("start ")) {
    const unsigned ports = command == "start" ? 2 : command.substring(6).toInt();
    started = host.begin();
    const uint32_t deadline = millis() + 10000;
    while (started && (!address.load() || !host.serialReady(address.load(), 0) || (ports == 2 && !host.serialReady(address.load(), 1))) && int32_t(deadline - millis()) > 0) delay(1);
    const bool ready = address.load() && host.serialPortCount(address.load()) == ports &&
        host.serialReady(address.load(), 0) && (ports == 1 || host.serialReady(address.load(), 1)) &&
        (ports == 2 || host.vendorOpen(address.load(), 3, ESP_USB_HOST_VENDOR_READ_ON_DEMAND));
    Serial.printf("HOST_READY %u ports=%u\r\n", unsigned(ready), unsigned(host.serialPortCount(address.load())));
  } else if (command.startsWith("line ")) {
    unsigned port = 0, baud = 0;
    sscanf(command.c_str(), "line %u %u", &port, &baud);
    EspUsbHostSerialConfig config; config.baud = baud;
    config.dataBits = 7; config.parity = ESP_USB_HOST_SERIAL_PARITY_EVEN; config.stopBits = ESP_USB_HOST_SERIAL_STOP_BITS_2;
    Serial.printf("LINE_SET %u\r\n", unsigned(host.setSerialConfig(config, address.load(), port)));
  } else if (command.startsWith("exchange ")) {
    unsigned port = 0, length = 0;
    sscanf(command.c_str(), "exchange %u %u", &port, &length);
    if (port > 2 || !length || length > 129) { Serial.println("INVALID_EXCHANGE"); return; }
    uint8_t data[129];
    for (size_t i = 0; i < length; ++i) data[i] = uint8_t(i * 37 + length);
    bool ok = true;
    size_t count = 0;
    if (port < 2) {
      counts[port].store(0, std::memory_order_release);
      ok = host.sendSerial(data, length, address.load(), port);
      const uint32_t deadline = millis() + 5000;
      while (ok && counts[port].load(std::memory_order_acquire) < length && int32_t(deadline - millis()) > 0) delay(1);
      count = counts[port].load(std::memory_order_acquire);
      ok &= count == length && memcmp(input[port], data, length) == 0;
    } else {
      ok = host.vendorWrite(data, length, address.load());
      uint8_t response[192];
      const uint32_t deadline = millis() + 5000;
      while (ok && count < length && int32_t(deadline - millis()) > 0) {
        size_t actual = 0;
        ok = host.vendorReadSync(response + count, length - count, &actual, 3000, address.load());
        count += actual;
      }
      ok &= count == length && memcmp(response, data, length) == 0;
    }
    Serial.printf("EXCHANGE port=%u length=%u actual=%u ok=%u\r\n", port, length, unsigned(count), unsigned(ok));
  } else if (command == "key_result") {
    const uint32_t deadline = millis() + 3000;
    while ((!keyPresses.load() || !keyReleases.load()) && int32_t(deadline - millis()) > 0) delay(1);
    Serial.printf("KEY_RESULT pressed=%u released=%u\r\n", keyPresses.load(), keyReleases.load());
  } else if (command == "stop") {
    if (started) host.end();
    started = false; address.store(0);
    Serial.println("STOPPED");
  }
}
