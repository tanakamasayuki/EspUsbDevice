// DUT side (USB host) of the multi-cable USB MIDI peer test.
//
// The peer is a 4-cable EspUsbDevice; see peer_device/. What this side adds over
// the loopback test is getMidiPortInfo(), which reports the cable counts EspUsbHost
// decoded from the peer's descriptors. That is the only check that proves the
// device advertised its cables, rather than merely echoing a cable number back:
// the cable in a received message is read straight out of the packet header.

#include "EspUsbHost.h"

EspUsbHost usb;

static constexpr uint8_t CABLE_COUNT = 4;

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
      Serial.printf("MIDI_PORT_INFO ok=%u in=%u out=%u\n",
                    ok ? 1 : 0,
                    info.inCableCount,
                    info.outCableCount);
    }
    else if (command >= '0' && command < static_cast<char>('0' + CABLE_COUNT))
    {
      // midiSendNoteOn() has no cable argument, so the packet is assembled here:
      // the cable number is the high nibble of the header byte. The note number
      // tracks the cable so a packet landing on the wrong cable cannot pass.
      const uint8_t cable = static_cast<uint8_t>(command - '0');
      const uint8_t packet[] = {
          static_cast<uint8_t>((cable << 4) | 0x09),
          0x90,
          static_cast<uint8_t>(70 + cable),
          90,
      };
      Serial.printf("MIDI_TX_CABLE %u %u\n", cable, usb.midiSend(packet, sizeof(packet)) ? 1 : 0);
    }
  }
  delay(1);
}
