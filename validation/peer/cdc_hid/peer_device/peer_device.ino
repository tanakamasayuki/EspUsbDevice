#include <Arduino.h>
#include <EspUsbDevice.h>
#include <atomic>
#include "esp_heap_caps.h"
// Device side: CDC ACM echo and a boot keyboard in one configuration.
EspUsbDevice device;
EspUsbDeviceCdcSerial serialPort(device);
EspUsbDeviceHidKeyboard keyboard(device);
std::atomic<size_t> echoed{0};
std::atomic<unsigned> bad{0};
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  serialPort.onRx([](size_t) {
    uint8_t data[512];
    while (serialPort.available() && serialPort.writeAvailable()) {
      const size_t n = serialPort.read(data, min(sizeof(data), serialPort.writeAvailable()));
      if (!n) break;
      if (serialPort.write(data, n) != n) ++bad;
      echoed.fetch_add(n);
    }
  });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start") {
    echoed = 0; bad = 0;
    EspUsbBulkStreamConfig config; config.rxCapacity = 8192; config.txCapacity = 8192; config.transferBytes = 512;
    bool ok = serialPort.configure(config) == ESP_OK;
    EspUsbDeviceConfig dc; dc.product = "EspUsbDevice CDC HID"; dc.controller = EspUsbController::HighSpeed;
    ok &= device.begin(dc);
    Serial.printf("STARTED %u\r\n", unsigned(ok));
  } else if (command == "coding") {
    Serial.printf("CODING baud=%u\r\n", unsigned(serialPort.lineCoding().baud));
  } else if (command == "echoed") {
    Serial.printf("ECHOED %u bad=%u\r\n", unsigned(echoed.load()), bad.load());
  } else if (command.startsWith("type ")) {
    bool ok = true; const unsigned n = command.substring(5).toInt();
    for (unsigned i = 0; i < n && ok; ++i) {
      ok &= keyboard.pressKey('a'); delay(30);
      ok &= keyboard.releaseAll(); delay(30);
    }
    Serial.printf("TYPED ok=%u\r\n", unsigned(ok));
  } else if (command == "stop") {
    const bool stopped = !device.started() || device.end() == ESP_OK;
    Serial.printf("STOPPED %u heap=%u\r\n", unsigned(stopped), unsigned(heap_caps_check_integrity_all(true)));
  }
}
