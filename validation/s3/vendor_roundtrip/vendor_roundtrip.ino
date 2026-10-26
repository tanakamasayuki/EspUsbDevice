#include <Arduino.h>
#include <EspUsbHost.h>
#include <atomic>

EspUsbHost host;
std::atomic<uint8_t> address{0};
bool started = false;
bool opened = false;

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    // Identity is specific to this probe, not every vendor interface.
    if (info.vid == 0x303a && info.pid == 0x4000 &&
        strcmp(info.product, "v3 Vendor API probe") == 0) {
      address.store(info.address);
    }
  });
  host.onDeviceDisconnected([](const EspUsbHostDeviceInfo &) {
    address.store(0);
  });
}

void loop() {
  if (!Serial.available()) {
    delay(1);
    return;
  }
  String command = Serial.readStringUntil('\n');
  command.trim();
  if (command == "start" || command.startsWith("start ")) {
    unsigned interfaceNumber = 0xff;
    if (command.startsWith("start ")) interfaceNumber = command.substring(6).toInt();
    started = host.begin();
    const uint32_t deadline = millis() + 10000;
    while (started && !address.load() && int32_t(deadline - millis()) > 0) delay(1);
    opened = started && address.load() && host.vendorOpen(address.load(), interfaceNumber, ESP_USB_HOST_VENDOR_READ_ON_DEMAND);
    if (opened) {
      Serial.printf("HOST_READY mps_in=%u mps_out=%u\r\n",
                    host.vendorInPacketSize(address.load()),
                    host.vendorOutPacketSize(address.load()));
    } else {
      Serial.println("HOST_START_FAILED");
    }
  } else if (command.startsWith("send ")) {
    unsigned length = 0, zlp = 0;
    sscanf(command.c_str(), "send %u %u", &length, &zlp);
    uint8_t data[129];
    if (!opened || length > sizeof(data)) { Serial.println("INVALID_SEND"); return; }
    for (size_t i = 0; i < length; ++i) data[i] = uint8_t(i * 37 + length);
    bool ok = !length || host.vendorWrite(data, length, address.load());
    if (ok && zlp) ok = host.vendorWriteZlp(address.load());
    Serial.printf("SENT %u zlp=%u ok=%u\r\n", length, zlp, unsigned(ok));
  } else if (command.startsWith("exchange ") || command.startsWith("receive ")) {
    const bool exchange = command.startsWith("exchange ");
    unsigned requested = 0, terminate = 0;
    sscanf(command.c_str() + (exchange ? 9 : 8), "%u %u", &requested, &terminate);
    const size_t length = requested;
    uint8_t sent[129], received[256];
    if (!opened || length == 0 || length > sizeof(sent)) {
      Serial.println("INVALID_EXCHANGE");
      return;
    }
    for (size_t i = 0; i < length; ++i) sent[i] = uint8_t(i * 37 + length);
    if (exchange && !host.vendorWrite(sent, length, address.load())) {
      Serial.println("WRITE_FAILED");
      return;
    }
    size_t count = 0;
    const uint32_t deadline = millis() + 3000;
    while (count < length && int32_t(deadline - millis()) > 0) {
      size_t actual = 0;
      if (!host.vendorReadSync(received + count, length - count + (terminate ? 64 : 0), &actual, 3000, address.load())) break;
      count += actual;
      delay(1);
    }
    if (count == length && memcmp(sent, received, length) == 0) {
      Serial.printf("%s %u\r\n", exchange ? "ROUNDTRIP_OK" : "READ_OK", unsigned(length));
    } else {
      Serial.printf("ROUNDTRIP_FAILED expected=%u actual=%u\r\n",
                    unsigned(length), unsigned(count));
    }
  } else if (command == "stop") {
    if (started) host.end();
    started = opened = false;
    address.store(0);
    Serial.println("STOPPED");
  }
}
