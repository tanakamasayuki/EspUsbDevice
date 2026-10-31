// MIDI controller: a potentiometer sends Control Change 74 and a button sends
// Note On/Off for C4. A failed send is retried instead of being lost.
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceMidi midi(device);

constexpr uint8_t MIDI_CHANNEL = 0;  // 0..15, shown as 1..16
constexpr uint8_t MIDI_NOTE_C4 = 60;
constexpr uint8_t MIDI_CC_CUTOFF = 74;
// Check these against the board's schematic. CONTROLLER_PIN must be an ADC pin.
#if defined(CONFIG_IDF_TARGET_ESP32P4)
constexpr int CONTROLLER_PIN = 16;  // ADC1 channel 0 on ESP32-P4
constexpr int BUTTON_PIN = 35;      // BOOT button on many ESP32-P4 boards
#else
constexpr int CONTROLLER_PIN = 5;
constexpr int BUTTON_PIN = 0;       // BOOT button on most ESP32-S2/S3 boards
#endif

uint16_t smoothedAdc = 0;
uint8_t sentCcValue = 255;   // last value the Host accepted
bool sentPressed = false;    // last button state the Host accepted

uint8_t readControllerValue() {
  smoothedAdc = uint16_t((uint32_t(smoothedAdc) * 15 + analogRead(CONTROLLER_PIN)) / 16);
  return uint8_t(map(smoothedAdc, 0, 4095, 0, 127));
}

void updateController() {
  const uint8_t ccValue = readControllerValue();
  if (ccValue == sentCcValue) return;
  if (midi.controlChange(MIDI_CHANNEL, MIDI_CC_CUTOFF, ccValue)) {
    sentCcValue = ccValue;  // Otherwise try again on the next loop.
    Serial.printf("MIDI_CC control=%u value=%u\n", MIDI_CC_CUTOFF, ccValue);
  }
}

void updateButton() {
  // No debounce: add one for a real switch.
  const bool pressed = digitalRead(BUTTON_PIN) == LOW;
  if (pressed == sentPressed) return;
  const bool ok = pressed ? midi.noteOn(MIDI_CHANNEL, MIDI_NOTE_C4, 96)
                          : midi.noteOff(MIDI_CHANNEL, MIDI_NOTE_C4, 0);
  if (ok) sentPressed = pressed;  // Keep the release pending until it is queued.
  Serial.printf("MIDI_BUTTON %s %u\n", pressed ? "ON" : "OFF", ok);
}

void setup() {
  Serial.begin(115200);  // Debug port; MIDI data uses the USB device port.
  delay(1500);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  smoothedAdc = analogRead(CONTROLLER_PIN);

  EspUsbMidiConfig midiConfig = midi.configuration();
  midiConfig.name = "EspUsbDevice MIDI Controller";
  midiConfig.txPackets = 16;
  // This controller ignores what the Host sends, so it takes packets in a
  // callback without a receive ring rather than leaving the Host blocked.
  midiConfig.rxMode = EspUsbMidiRxMode::Callback;
  midiConfig.rxPackets = 0;
  if (midi.configure(midiConfig) != ESP_OK ||
      midi.onPacket([](const EspUsbDeviceMidiPacket &) {}) != ESP_OK) {
    Serial.println("MIDI configuration rejected");
    return;
  }

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4018;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice MIDI Controller";
  config.serialNumber = "espusb-midi-controller";
  if (!device.begin(config)) {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("USB MIDI controller ready");
}

void loop() {
  if (device.ready()) {
    updateController();
    updateButton();
  } else {
    sentCcValue = 255;    // Resend the current position after reconnection.
    sentPressed = false;  // The Host dropped any held note.
  }
  delay(5);
}
