// Peer side of the multi-cable USB MIDI peer test: a 4-cable EspUsbDevice.
//
// tests/peer/usb_midi/peer_device keeps covering the default single-cable device.

#include "EspUsbDevice.h"

static constexpr uint8_t CABLE_COUNT = 4;

EspUsbDevice device;
EspUsbDeviceMidi MIDI(device, CABLE_COUNT);

void setup()
{
  Serial.begin(115200);
  delay(500);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4017;
  config.manufacturer = "EspUsbDevice";
  config.product = "EspUsbDevice MIDI Cables";
  Serial.printf("DEVICE_BEGIN %u\n", device.begin(config) ? 1 : 0);
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == '?')
    {
      Serial.printf("DEVICE_READY cables=%u bytes=%u\n",
                    MIDI.cableCount(),
                    MIDI.descriptorLength());
    }
    else if (command >= '0' && command < static_cast<char>('0' + CABLE_COUNT))
    {
      // Note number tracks the cable, so a packet arriving on the wrong cable is
      // distinguishable from one on the right cable with the wrong payload.
      const uint8_t cable = static_cast<uint8_t>(command - '0');
      const uint8_t note = static_cast<uint8_t>(60 + cable);
      Serial.printf("DEVICE_TX_CABLE %u %u\n",
                    cable,
                    MIDI.noteOn(0, note, 100, cable) ? 1 : 0);
    }
    else if (command == 'x')
    {
      // Cable 4 does not exist on a 4-cable device. Sending it anyway would put a
      // packet on a port the host was never told about, so the helper refuses.
      Serial.printf("DEVICE_TX_UNKNOWN_CABLE %u\n",
                    MIDI.noteOn(0, 60, 100, CABLE_COUNT) ? 1 : 0);
    }
  }

  EspUsbDeviceMidiPacket packet;
  if (MIDI.readPacket(packet))
  {
    Serial.printf("DEVICE_RX cable=%u cin=%02x status=%02x data1=%u data2=%u\n",
                  packet.header >> 4,
                  packet.header & 0x0f,
                  packet.byte1,
                  packet.byte2,
                  packet.byte3);
  }
  delay(1);
}
