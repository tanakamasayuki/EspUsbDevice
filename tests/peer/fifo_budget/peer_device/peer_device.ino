#include <Arduino.h>
#include <EspUsbDevice.h>
#include "esp_heap_caps.h"
// Device side: N Vendor interfaces on the high-speed controller. Each bulk IN
// takes a 512-byte packet of the shared 4 KiB FIFO, so enough of them cannot fit.
EspUsbDevice device;
EspUsbDeviceVendor *vendors[7] = {};
unsigned count = 0;

void release() {
  if (device.started()) device.end();
  for (auto *&v : vendors) { delete v; v = nullptr; }
  count = 0;
}
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command.startsWith("start ")) {
    release();
    count = command.substring(6).toInt();
    EspUsbVendorConfig config; config.rxCapacity = config.txCapacity = 512;
    bool configured = true;
    for (unsigned i = 0; i < count; ++i) {
      vendors[i] = new EspUsbDeviceVendor(device);
      configured &= vendors[i]->configure(config) == ESP_OK;
    }
    EspUsbDeviceConfig dc; dc.product = "EspUsbDevice FIFO budget"; dc.controller = EspUsbController::HighSpeed;
    const bool begun = configured && device.begin(dc);
    Serial.printf("STARTED begun=%u error=%s\r\n", unsigned(begun), begun ? "ESP_OK" : device.lastErrorName());
  } else if (command == "stop") {
    release();
    Serial.printf("STOPPED heap=%u\r\n", unsigned(heap_caps_check_integrity_all(true)));
  }
}
