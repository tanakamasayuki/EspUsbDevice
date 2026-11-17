#include <Arduino.h>
#include <EspUsbDevice.h>
// Device side: a Vendor whose endpoints cannot be opened, so SET_CONFIGURATION
// is answered with STALL and the device stays addressed - the way a
// configuration that did not fit the FIFO behaved before begin() refused it.
class UnopenableVendor : public EspUsbDeviceVendor {
public:
  using EspUsbDeviceVendor::EspUsbDeviceVendor;
protected:
  uint16_t openBulkInterface(uint8_t, const uint8_t *, uint16_t, uint8_t &, uint8_t &, uint16_t &) override { return 0; }
};
EspUsbDevice device;
UnopenableVendor vendor(device);
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start") {
    EspUsbDeviceConfig dc; dc.product = "EspUsbDevice config fail"; dc.controller = EspUsbController::HighSpeed;
    const bool begun = device.begin(dc);
    Serial.printf("STARTED begun=%u\r\n", unsigned(begun));
  } else if (command == "state") {
    Serial.printf("STATE ready=%u\r\n", unsigned(device.ready()));
  } else if (command == "stop") {
    Serial.printf("STOPPED %d\r\n", int(device.end()));
  }
}
