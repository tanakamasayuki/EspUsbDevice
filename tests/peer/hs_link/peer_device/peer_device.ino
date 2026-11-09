#include <Arduino.h>
#include <EspUsbDevice.h>
#include <Preferences.h>
// Device on the HS controller. With vbusSensing the controller sees VBUS only
// through the PHY's B-session valid; without it the session is forced valid.
EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
// DWC2 GOTGCTL of the HS controller: bit 19 BSesVld, bit 18 ASesVld.
volatile uint32_t &gotgctl = *reinterpret_cast<volatile uint32_t *>(0x50000000);
Preferences settings;
bool start(bool vbusSensing) {
  EspUsbDeviceConfig config; config.vid = 0x1209; config.pid = 0x0002;
  config.product = "EspUsbDevice HS link"; config.controller = EspUsbController::HighSpeed;
  config.vbusSensing = vbusSensing;
  return device.begin(config);
}
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  // Autostart lets the Device come up on a board whose own serial is unpowered
  // (its hub port switched off), to see whether the peer's VBUS runs it.
  settings.begin("hslink");
  if (settings.getBool("autostart")) Serial.printf("AUTOSTARTED %u\r\n", unsigned(start(false)));
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command.startsWith("autostart ")) {
    settings.putBool("autostart", command.endsWith("1"));
    Serial.printf("AUTOSTART %u\r\n", unsigned(settings.getBool("autostart")));
  } else if (command.startsWith("start ")) {
    Serial.printf("STARTED %u\r\n", unsigned(start(command.endsWith("1"))));
  } else if (command == "state") {
    const uint32_t value = gotgctl;
    Serial.printf("STATE ready=%u bsesvld=%u gotgctl=%08x\r\n", unsigned(device.ready()), unsigned((value >> 19) & 1), unsigned(value));
  } else if (command == "stop") {
    Serial.println(!device.started() || device.end() == ESP_OK ? "STOPPED" : "STOP_FAILED");
  }
}
