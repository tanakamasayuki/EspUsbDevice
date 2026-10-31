#include <EspUsbDevice.h>
EspUsbDevice device;
EspUsbDeviceCdcSerial console(device);
void setup() {
  EspUsbBulkStreamConfig config;
  config.rxCapacity = 256;
  config.txCapacity = 512;
  config.transferBytes = 64;
  // Failures are reported on the separate debug connection.
  Serial.begin(115200);
  const esp_err_t configured = console.configure(config);
  if (configured != ESP_OK) { Serial.printf("configure failed: %s\n", esp_err_to_name(configured)); return; }
  if (!device.begin()) Serial.printf("begin failed: %s\n", device.lastErrorName());
}
// A consumer can use Arduino Stream directly; no wrapper object is needed.
void echo(Stream &io) {
  while (io.available() && io.availableForWrite()) {
    const int value = io.read();
    if (value >= 0) io.write(uint8_t(value));
  }
}
void loop() {
  echo(console);
  delay(1);
}
