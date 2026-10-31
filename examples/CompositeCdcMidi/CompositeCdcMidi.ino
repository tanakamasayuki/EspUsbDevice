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
  // Failures are reported on the separate debug connection.
  Serial.begin(115200);
  esp_err_t configured = console.configure(serialConfig);
  if (configured == ESP_OK) configured = midi.configure(midiConfig);
  if (configured != ESP_OK) { Serial.printf("configure failed: %s\n", esp_err_to_name(configured)); return; }
  EspUsbDeviceConfig config;
  config.product = "CDC and MIDI echo";
  if (!device.begin(config)) Serial.printf("begin failed: %s\n", device.lastErrorName());
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
