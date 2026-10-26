#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
void setup() { device.begin(); }
void loop() {
  if (vendor.mounted()) vendor.write(uint8_t(0x42));
  delay(1);
}
