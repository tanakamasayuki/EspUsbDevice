#include <Arduino.h>
#include <EspUsbDevice.h>
#include <algorithm>
#include "esp_heap_caps.h"
// Device side: a Vendor interface built with each full-speed bulk packet size
// USB 2.0 5.8.3 allows (8/16/32/64), echoing what it receives; and the sizes it
// does not allow, which begin() refuses before descriptors exist.
EspUsbDevice device;
EspUsbDeviceVendor *vendor = nullptr;

void pump() {
  uint8_t data[64];
  while (vendor && device.ready() && vendor->available() > 0 && vendor->writeAvailable()) {
    const size_t n = vendor->read(data, std::min(sizeof(data), vendor->writeAvailable()));
    if (!n || vendor->write(data, n) != n) break;
  }
}
void stopVendor() {
  if (device.started()) device.end();
  delete vendor; vendor = nullptr;
}
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  pump();
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "refuse") {
    bool ok = true;
    unsigned refused = 0;
    EspUsbDeviceConfig identity; identity.startMode = EspUsbDeviceStartMode::DescriptorsOnly;
    for (uint16_t size : {1, 7, 10, 48, 63}) {
      EspUsbDevice local; EspUsbDeviceVendor bad(local, size);
      const bool rejected = !local.begin(identity) && local.lastError() == ESP_ERR_INVALID_SIZE && !local.started();
      ok &= rejected; refused += rejected;
    }
    // Values above 64 select 64, as CDC does with 512.
    for (uint16_t size : {8, 64, 512}) {
      EspUsbDevice local; EspUsbDeviceVendor good(local, size);
      ok &= local.begin(identity) && local.end() == ESP_OK;
    }
    Serial.printf("REFUSED ok=%u sizes=%u heap=%u\r\n", unsigned(ok), refused, unsigned(heap_caps_check_integrity_all(true)));
  } else if (command.startsWith("start ")) {
    stopVendor();
    vendor = new EspUsbDeviceVendor(device, uint16_t(command.substring(6).toInt()));
    EspUsbVendorConfig config; config.rxCapacity = config.txCapacity = 2048; config.transferBytes = 512;
    bool ok = vendor->configure(config) == ESP_OK;
    EspUsbDeviceConfig dc; dc.product = "EspUsbDevice packet size";
    ok = ok && device.begin(dc);
    Serial.printf("STARTED ok=%u\r\n", unsigned(ok));
  } else if (command == "stop") {
    stopVendor();
    Serial.printf("STOPPED heap=%u\r\n", unsigned(heap_caps_check_integrity_all(true)));
  }
}
