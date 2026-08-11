// DUT side (USB host) of the multi-cable USB MIDI peer test.
//
// The peer is an asymmetric EspUsbDevice - 4 cables device-to-host, 5 the other
// way; see peer_device/ for why. What this side adds over
// the loopback test is getMidiPortInfo(), which reports the cable counts
// EspUsbHost decoded from the peer's descriptors. That is the only check that
// proves the device advertised its cables, rather than merely echoing a cable
// number back: the cable in a received message is read straight out of the packet
// header, so it would come back correct even from a one-cable descriptor.

#include "EspUsbHost.h"

EspUsbHost usb;

// Host-view: IN is device to host, OUT is host to device. This side sends on the
// OUT cables and receives on the IN ones.
static constexpr uint8_t IN_CABLES = 4;
static constexpr uint8_t OUT_CABLES = 5;

// Cables visited out of order, so a bug that applies the first packet's cable to
// the rest of a transfer is visible: the notes stay sequential while the cables
// do not.
static const uint8_t INTERLEAVED_CABLES[] = {3, 0, 2, 1};

static bool sendNoteOn(uint8_t cable, uint8_t note)
{
  const uint8_t packet[] = {
      static_cast<uint8_t>((cable << 4) | 0x09),
      0x90,
      note,
      90,
  };
  return usb.midiSend(packet, sizeof(packet));
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          Serial.printf("HOST_CONNECTED vid=%04x pid=%04x supported=%u interfaces=%u\n",
                                        device.vid,
                                        device.pid,
                                        device.supported ? 1 : 0,
                                        device.configurationInterfaceCount);
                        });

  usb.onMidiMessage([](const EspUsbHostMidiMessage &message)
                    {
                      Serial.printf("MIDI_RX cable=%u cin=%02x status=%02x data1=%u data2=%u\n",
                                    message.cable,
                                    message.codeIndex,
                                    message.status,
                                    message.data1,
                                    message.data2);
                    });

  if (!usb.begin())
  {
    Serial.printf("HOST_BEGIN_FAILED %s\n", usb.lastErrorName());
  }
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == 'i')
    {
      EspUsbHostMidiPortInfo info;
      const bool ok = usb.getMidiPortInfo(info);
      // The interface number is reported so the test can confirm the counts and
      // the interface they came from describe the same interface.
      Serial.printf("MIDI_PORT_INFO ok=%u in=%u out=%u iface=%u\n",
                    ok ? 1 : 0,
                    info.inCableCount,
                    info.outCableCount,
                    info.interfaceNumber);
    }
    else if (command == 'A')
    {
      // midiSendNoteOn() has no cable argument, so the packets are assembled by
      // hand: the cable number is the high nibble of the header byte.
      for (uint8_t cable = 0; cable < OUT_CABLES; cable++)
      {
        Serial.printf("MIDI_TX_CABLE %u %u\n",
                      cable,
                      sendNoteOn(cable, static_cast<uint8_t>(70 + cable)) ? 1 : 0);
        // Paced so the other board is not asked to print every line back to back
        // at 115200 baud. Packing several cables into one transfer is the 'I'
        // command's job.
        delay(20);
      }
    }
    else if (command == 'I')
    {
      // One bulk transfer carrying four packets on four different cables, which
      // is what the device has to split apart rather than reading the cable once.
      uint8_t packets[sizeof(INTERLEAVED_CABLES) * 4];
      for (uint8_t i = 0; i < sizeof(INTERLEAVED_CABLES); i++)
      {
        packets[i * 4 + 0] = static_cast<uint8_t>((INTERLEAVED_CABLES[i] << 4) | 0x09);
        packets[i * 4 + 1] = 0x90;
        packets[i * 4 + 2] = static_cast<uint8_t>(110 + i);
        packets[i * 4 + 3] = 90;
      }
      Serial.printf("MIDI_TX_INTERLEAVE %u\n", usb.midiSend(packets, sizeof(packets)) ? 1 : 0);
    }
    else if (command == 'S')
    {
      // SysEx on a cable other than 0, sent as raw packets because
      // midiSendSysEx() has no cable argument. CIN 0x4 = SysEx starts /
      // continues, CIN 0x6 = ends with two bytes.
      const uint8_t packets[] = {
          0x34, 0xf0, 0x7d, 0x01,
          0x36, 0x02, 0xf7, 0x00,
      };
      Serial.printf("MIDI_TX_SYSEX %u\n", usb.midiSend(packets, sizeof(packets)) ? 1 : 0);
    }
  }
  delay(1);
}
