#include <EspUsbDevice.h>

// Link probe only: declaring Device must not retain the Vendor implementation
// when no Vendor function is declared. This sketch is not uploaded by pytest.
EspUsbDevice device;
void setup() {}
void loop() { delay(1); }
