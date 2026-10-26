#include <EspUsbDevice.h>
EspUsbDevice device;
EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Runtime);
void setup() { dfu.onDetach([]() {}); device.begin(); }
void loop() { delay(1); }
