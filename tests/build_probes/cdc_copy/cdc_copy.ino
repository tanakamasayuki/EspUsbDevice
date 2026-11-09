#include <EspUsbDevice.h>
EspUsbDevice device;
EspUsbDeviceCdcSerial serial(device);
void setup() { device.begin(); }
void loop() { if (serial.mounted()) serial.write(uint8_t(0x42)); delay(1); }
