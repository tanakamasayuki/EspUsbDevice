#include <EspUsbDevice.h>
EspUsbDevice device;
EspUsbDeviceMidi midi(device);
EspUsbDeviceMidiPacket pending;
bool held = false;
void setup() {
  EspUsbMidiConfig config;
  config.inCables = config.outCables = 1;
  config.rxPackets = config.txPackets = 16;
  config.transferPackets = 4;
  // Failures are reported on the separate debug connection.
  Serial.begin(115200);
  const esp_err_t configured = midi.configure(config);
  if (configured != ESP_OK) { Serial.printf("configure failed: %s\n", esp_err_to_name(configured)); return; }
  if (!device.begin()) Serial.printf("begin failed: %s\n", device.lastErrorName());
}
void loop() {
  if (!device.ready()) { held = false; delay(1); return; }
  if (!held) held = midi.readPacket(pending);
  if (held && midi.writePacket(pending)) held = false;
  delay(1);
}
