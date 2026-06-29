#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceMidi MIDI(device);

static constexpr uint8_t MIDI_CHANNEL = 0;
static constexpr uint8_t MIDI_NOTE_C4 = 60;
static constexpr uint8_t MIDI_CC_CUTOFF = 74;
static constexpr int CONTROLLER_PIN = 5;
static constexpr int BUTTON_PIN = 0;

static uint16_t smoothedAdc = 0;
static uint8_t lastCcValue = 255;
static bool buttonPressed = false;

static uint8_t readControllerValue()
{
  const uint16_t value = analogRead(CONTROLLER_PIN);
  smoothedAdc = static_cast<uint16_t>((static_cast<uint32_t>(smoothedAdc) * 15 + value) / 16);
  return static_cast<uint8_t>(map(smoothedAdc, 0, 4095, 0, 127));
}

static void updateController()
{
  const uint8_t ccValue = readControllerValue();
  if (ccValue == lastCcValue)
  {
    return;
  }
  lastCcValue = ccValue;

  if (MIDI.controlChange(MIDI_CHANNEL, MIDI_CC_CUTOFF, ccValue))
  {
    Serial.printf("MIDI_CC control=%u value=%u\n", MIDI_CC_CUTOFF, ccValue);
  }
}

static void updateButton()
{
  const bool pressed = digitalRead(BUTTON_PIN) == LOW;
  if (pressed == buttonPressed)
  {
    return;
  }
  buttonPressed = pressed;

  const bool ok = pressed ? MIDI.noteOn(MIDI_CHANNEL, MIDI_NOTE_C4, 96)
                          : MIDI.noteOff(MIDI_CHANNEL, MIDI_NOTE_C4, 0);
  Serial.printf("MIDI_BUTTON %s %u\n", pressed ? "ON" : "OFF", ok ? 1 : 0);
}

void setup()
{
  Serial.begin(115200);
  delay(1500);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  smoothedAdc = analogRead(CONTROLLER_PIN);

  EspUsbDeviceConfig config;
  config.port = ESP_USB_DEVICE_PORT_FULL_SPEED;
  config.speed = ESP_USB_DEVICE_SPEED_FULL;
  config.vid = 0x303a;
  config.pid = 0x4018;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice MIDI Controller";
  config.serialNumber = "espusb-midi-controller";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB MIDI controller ready");
}

void loop()
{
  updateController();
  updateButton();
  delay(5);
}
