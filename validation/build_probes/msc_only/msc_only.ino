#include <EspUsbDevice.h>
EspUsbDevice device;
EspUsbDeviceMsc disk(device);
void setup() {
  disk.onRead([](uint32_t, uint32_t, void *buffer, uint32_t n) { memset(buffer, 0, n); return int32_t(n); });
  disk.begin(8, 512); disk.mediaPresent(true); device.begin();
}
void loop() { delay(1); }
