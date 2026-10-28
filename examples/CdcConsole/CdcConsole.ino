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
void loop() {
  // Backpressure: consume a received byte only when it can be queued for TX.
  while (console.available() && console.writeAvailable()) {
    const int value = console.read();
    if (value >= 0) console.write(uint8_t(value));
  }
  delay(1);
}
