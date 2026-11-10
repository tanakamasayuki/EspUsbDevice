#include <Arduino.h>
#include <EspUsbDevice.h>
#include "esp_heap_caps.h"
// Device side: N Vendor interfaces on the high-speed controller. Each bulk IN
// takes a 512-byte packet of the shared 4 KiB FIFO, so enough of them cannot fit.
EspUsbDevice device;
EspUsbDeviceVendor *vendors[7] = {};
EspUsbDeviceCdcSerial *cdcs[3] = {};
unsigned count = 0;

void release() {
  if (device.started()) device.end();
  for (auto *&v : vendors) { delete v; v = nullptr; }
  for (auto *&c : cdcs) { delete c; c = nullptr; }
  count = 0;
}
EspUsbBulkInBuffering mode(int value) {
  return value == 2 ? EspUsbBulkInBuffering::Double : value == 1 ? EspUsbBulkInBuffering::Single : EspUsbBulkInBuffering::Auto;
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
  } else if (command.startsWith("mix ")) {
    // mix <vendors> <cdcs> <vendor buffering> <device buffering>: Vendors are
    // registered first, so the first one's bulk IN is endpoint 1 (mask 0x2).
    release();
    int v = 0, c = 0, vm = 0, dm = 0;
    sscanf(command.c_str(), "mix %d %d %d %d", &v, &c, &vm, &dm);
    EspUsbVendorConfig config; config.rxCapacity = config.txCapacity = 512; config.bulkInBuffering = mode(vm);
    bool configured = true;
    for (int i = 0; i < v && i < 7; ++i) {
      vendors[i] = new EspUsbDeviceVendor(device);
      configured &= vendors[i]->configure(config) == ESP_OK;
    }
    for (int i = 0; i < c && i < 3; ++i) cdcs[i] = new EspUsbDeviceCdcSerial(device);
    EspUsbDeviceConfig dc; dc.product = "EspUsbDevice FIFO budget"; dc.controller = EspUsbController::HighSpeed;
    dc.bulkInBuffering = mode(dm);
    const bool begun = configured && device.begin(dc);
    Serial.printf("MIX begun=%u error=%s mask=0x%x\r\n", unsigned(begun), begun ? "ESP_OK" : device.lastErrorName(),
                  unsigned(device.bulkInDoubleBuffered()));
  } else if (command == "stop") {
    release();
    Serial.printf("STOPPED heap=%u\r\n", unsigned(heap_caps_check_integrity_all(true)));
  }
}
