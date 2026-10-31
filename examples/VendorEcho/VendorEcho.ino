#include <EspUsbDevice.h>
EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
uint8_t pending[64];
size_t length = 0, sent = 0;
void setup() {
  EspUsbBulkStreamConfig config;
  config.rxCapacity = config.txCapacity = 256;
  config.transferBytes = 64;
  // Failures are reported on the separate debug connection.
  Serial.begin(115200);
  const esp_err_t configured = vendor.configure(config);
  if (configured != ESP_OK) { Serial.printf("configure failed: %s\n", esp_err_to_name(configured)); return; }
  if (!device.begin()) Serial.printf("begin failed: %s\n", device.lastErrorName());
}
void loop() {
  if (!device.ready()) { length = sent = 0; delay(1); return; }
  if (!length) { length = vendor.read(pending, sizeof(pending)); sent = 0; }
  if (length) {
    sent += vendor.write(pending + sent, length - sent);
    if (sent == length) length = sent = 0;
  }
  delay(1);
}
