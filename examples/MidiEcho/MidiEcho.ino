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
  if (midi.configure(config) != ESP_OK) return;
  device.begin();
}
void loop() {
  if (!device.ready()) { held = false; delay(1); return; }
  if (!held) held = midi.readPacket(pending);
  if (held && midi.writePacket(pending)) held = false;
  delay(1);
}
