#include <EspUsbDevice.h>
EspUsbDevice device;
EspUsbDeviceMidi midi(device);
void setup() { device.begin(); }
void loop() { if (midi.mounted()) midi.noteOn(0, 60, 100); delay(1); }
