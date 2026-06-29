#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceMidi MIDI(device);

static uint32_t lastNoteMs = 0;
static uint8_t stepIndex = 0;
static const uint8_t NOTES[] = {60, 64, 67, 72};

static void sendStep()
{
  const uint8_t previous = NOTES[(stepIndex + 3) % 4];
  const uint8_t current = NOTES[stepIndex];

  MIDI.noteOff(0, previous, 0);
  MIDI.noteOn(0, current, 96);
  MIDI.controlChange(0, 74, 32 + (stepIndex * 24));

  Serial.printf("MIDI_TX note=%u\n", current);
  stepIndex = (stepIndex + 1) % 4;
}

static void logPacket(const EspUsbDeviceMidiPacket &packet)
{
  Serial.printf("MIDI_RX header=0x%02x cin=0x%02x b1=0x%02x b2=%u b3=%u\n",
                packet.header,
                packet.header & 0x0f,
                packet.byte1,
                packet.byte2,
                packet.byte3);
}

void setup()
{
  Serial.begin(115200);
  delay(1500);

  EspUsbDeviceConfig config;
  config.port = ESP_USB_DEVICE_PORT_FULL_SPEED;
  config.speed = ESP_USB_DEVICE_SPEED_FULL;
  config.vid = 0x303a;
  config.pid = 0x4017;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice MIDI";
  config.serialNumber = "espusb-midi";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB MIDI ready");
}

void loop()
{
  const uint32_t now = millis();
  if (now - lastNoteMs >= 1000)
  {
    lastNoteMs = now;
    sendStep();
  }

  EspUsbDeviceMidiPacket packet;
  while (MIDI.readPacket(packet))
  {
    logPacket(packet);
  }

  delay(1);
}
