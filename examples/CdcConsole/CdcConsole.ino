#include <EspUsbDevice.h>
EspUsbDevice device;
EspUsbDeviceCdcSerial console(device);
void setup() {
  EspUsbBulkStreamConfig config;
  config.rxCapacity = 256;
  config.txCapacity = 512;
  config.transferBytes = 64;
  if (console.configure(config) != ESP_OK) return;
  device.begin();
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
