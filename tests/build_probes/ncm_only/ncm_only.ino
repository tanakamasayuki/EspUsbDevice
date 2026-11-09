#include <EspUsbDevice.h>
EspUsbDevice device;
EspUsbDeviceNet net(device);
void setup() { net.onFrame([](const uint8_t *p,size_t n) { net.sendFrame(p,n); }); device.begin(); }
void loop() { delay(1); }
