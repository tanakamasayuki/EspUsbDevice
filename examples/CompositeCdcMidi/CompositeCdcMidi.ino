#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceCdcSerial console(device, "Console");
EspUsbDeviceMidi midi(device);
EspUsbDeviceMidiPacket pending;
bool held = false;

void setup() {
  EspUsbBulkStreamConfig serialConfig;
  serialConfig.rxCapacity = serialConfig.txCapacity = 128;
  serialConfig.transferBytes = 64;
  EspUsbMidiConfig midiConfig;
  midiConfig.inCables = midiConfig.outCables = 1;
  midiConfig.rxPackets = midiConfig.txPackets = 16;
  midiConfig.transferPackets = 4;
  if (console.configure(serialConfig) != ESP_OK ||
      midi.configure(midiConfig) != ESP_OK) return;
  EspUsbDeviceConfig config;
  config.product = "CDC and MIDI echo";
  device.begin(config);
}

void loop() {
  // Each Function has independent storage and backpressure.
  while (console.available() && console.writeAvailable()) {
    const int value = console.read();
    if (value >= 0) console.write(uint8_t(value));
  }
  if (!device.ready()) held = false;
  else {
    if (!held) held = midi.readPacket(pending);
    if (held && midi.writePacket(pending)) held = false;
  }
  delay(1);
}
