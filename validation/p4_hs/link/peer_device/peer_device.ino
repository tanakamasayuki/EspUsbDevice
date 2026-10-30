#include <Arduino.h>
#include <EspUsbDevice.h>
// Device on the HS controller. With vbusSensing the controller sees VBUS only
// through the PHY's B-session valid; without it the session is forced valid.
EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
// DWC2 GOTGCTL of the HS controller: bit 19 BSesVld, bit 18 ASesVld.
volatile uint32_t &gotgctl = *reinterpret_cast<volatile uint32_t *>(0x50000000);
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command.startsWith("start ")) {
    EspUsbDeviceConfig config; config.vid = 0x1209; config.pid = 0x0002;
    config.product = "EspUsbDevice HS link"; config.controller = EspUsbController::HighSpeed;
    config.vbusSensing = command.endsWith("1");
    Serial.printf("STARTED %u\r\n", unsigned(device.begin(config)));
  } else if (command == "state") {
    const uint32_t value = gotgctl;
    Serial.printf("STATE ready=%u bsesvld=%u gotgctl=%08x\r\n", unsigned(device.ready()), unsigned((value >> 19) & 1), unsigned(value));
  } else if (command == "stop") {
    Serial.println(!device.started() || device.end() == ESP_OK ? "STOPPED" : "STOP_FAILED");
  }
}
