// USB MIDI 1.0 device: plays a C major arpeggio with a Control Change on each
// step, and logs the USB-MIDI event packets the Host sends.
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceMidi midi(device);  // one cable in each direction

const uint8_t notes[] = {60, 64, 67, 72};
uint8_t stepIndex = 0;
bool noteSounding = false;
uint32_t lastStepMs = 0;

void sendStep() {
  const uint8_t previous = notes[(stepIndex + 3) % 4];
  const uint8_t current = notes[stepIndex];
  // channel is 0..15 (shown as 1..16 by most applications); cable defaults to 0.
  // true means the event was queued for the Host, not that the Host played it.
  if (noteSounding) midi.noteOff(0, previous, 0);
  noteSounding = midi.noteOn(0, current, 96);
  const bool ccOk = midi.controlChange(0, 74, 32 + stepIndex * 24);
  Serial.printf("MIDI_TX note=%u ok=%u cc_ok=%u\n", current, noteSounding, ccOk);
  stepIndex = (stepIndex + 1) % 4;
}

void logPacket(const EspUsbDeviceMidiPacket &packet) {
  // header: high nibble = cable, low nibble = Code Index Number (CIN).
  Serial.printf("MIDI_RX cable=%u cin=0x%x b1=0x%02x b2=%u b3=%u\n",
      packet.header >> 4, packet.header & 0x0f, packet.byte1, packet.byte2,
      packet.byte3);
}

void setup() {
  Serial.begin(115200);  // Debug port; MIDI data uses the USB device port.
  delay(1500);

  EspUsbMidiConfig midiConfig = midi.configuration();  // keeps the cable counts
  midiConfig.name = "EspUsbDevice MIDI";
  midiConfig.rxPackets = 32;  // events, 4 bytes each
  midiConfig.txPackets = 32;
  if (midi.configure(midiConfig) != ESP_OK) {
    Serial.println("MIDI configuration rejected");
    return;
  }

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4017;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice MIDI";
  config.serialNumber = "espusb-midi";
  if (!device.begin(config)) {
    Serial.printf("USB_BEGIN_FAILED %s midi=%s\n", device.lastErrorName(),
        esp_err_to_name(midi.startError()));
    return;
  }
  Serial.printf("USB MIDI ready rx=%u tx=%u events\n",
      unsigned(midi.receiveCapacity()), unsigned(midi.sendCapacity()));
}

void loop() {
  if (!device.ready()) {
    noteSounding = false;  // A reset or unplug ends the Host's note state too.
  } else if (millis() - lastStepMs >= 1000) {
    lastStepMs = millis();
    sendStep();
  }

  EspUsbDeviceMidiPacket packet;
  while (midi.readPacket(packet)) logPacket(packet);

  static uint32_t reportedErrors = 0;
  if (midi.receiveErrors() != reportedErrors) {
    reportedErrors = midi.receiveErrors();
    Serial.printf("MIDI_RX dropped=%lu (unknown cable, reserved CIN or partial)\n",
        (unsigned long)reportedErrors);
  }
  delay(1);
}
