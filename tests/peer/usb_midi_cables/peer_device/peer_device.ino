// Peer side of the multi-cable USB MIDI peer test: an asymmetric EspUsbDevice with
// 4 cables device-to-host and 5 host-to-device.
//
// tests/peer/usb_midi/peer_device keeps covering the default single-cable device,
// and tests/loopback/usb_midi_cables the symmetric multi-cable case.
//
// Asymmetric on purpose. The MIDI class names embedded jacks from the device's
// side, which is the opposite of the endpoint direction they belong to, so a host
// that swaps the two directions cannot be caught by a symmetric device - nor by any
// round trip, since a received packet's cable number comes from its own header. The
// counts have to differ for "which endpoint's count is which direction" to mean
// anything.
//
// More receive cables than send cables, so there is a cable that exists for
// receiving and not for sending - which is what makes the send helpers' range
// (inCableCount(), not outCableCount()) observable.
//
// 5 is also the most cables this rig can enumerate in one direction. The ESP-IDF
// USB Host refuses a configuration descriptor longer than its enumeration control
// transfer ("Configuration descriptor larger than control transfer max length") and
// CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE is 256 in the precompiled Arduino
// libraries, with no way to raise it from a sketch. This 4/5 device is 204 bytes of
// MIDI descriptor plus the 9-byte configuration header = 213. A symmetric 6-cable
// device is 261 and does not enumerate at all. The library itself supports 16 per
// direction, which is legal USB and works against a PC host -
// tests/unit/midi_descriptor covers every combination.

#include "EspUsbDevice.h"

// Host-view, as USB endpoint directions and EspUsbHostMidiPortInfo are: IN is
// device to host (what this device sends), OUT is host to device.
static constexpr uint8_t IN_CABLES = 4;
static constexpr uint8_t OUT_CABLES = 5;

EspUsbDevice device;
EspUsbDeviceMidi MIDI(device, IN_CABLES, OUT_CABLES);

// Cables visited out of order, so a bug that applies the first packet's cable to
// the rest of a transfer is visible: the notes stay sequential while the cables do
// not. Within IN_CABLES so the same list works in both directions.
static const uint8_t INTERLEAVED_CABLES[] = {3, 0, 2, 1};

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
      Serial.printf("DEVICE_READY in=%u out=%u bytes=%u\n",
                    MIDI.inCableCount(),
                    MIDI.outCableCount(),
                    MIDI.descriptorLength());
    }
    else if (command == 'A')
    {
      // Note number tracks the cable, so a packet arriving on the wrong cable is
      // distinguishable from one on the right cable with the wrong payload.
      for (uint8_t cable = 0; cable < IN_CABLES; cable++)
      {
        const uint8_t note = static_cast<uint8_t>(60 + cable);
        Serial.printf("DEVICE_TX_CABLE %u %u\n",
                      cable,
                      MIDI.noteOn(0, note, 100, cable) ? 1 : 0);
        // Paced so the other board is not asked to print every line back to back
        // at 115200 baud. Packing several cables into one transfer is the 'I'
        // command's job.
        delay(20);
      }
    }
    else if (command == 'I')
    {
      // Written back to back with nothing in between, so TinyUSB coalesces them
      // into as few bulk transfers as it can - which is what puts several cables
      // inside one transfer for the host to split apart.
      bool ok = true;
      for (uint8_t i = 0; i < sizeof(INTERLEAVED_CABLES); i++)
      {
        ok = ok && MIDI.noteOn(0, static_cast<uint8_t>(100 + i), 100, INTERLEAVED_CABLES[i]);
      }
      Serial.printf("DEVICE_TX_INTERLEAVE %u\n", ok ? 1 : 0);
    }
    else if (command == 'S')
    {
      // SysEx on a cable other than 0. Every packet of a SysEx message carries the
      // cable number again, so reassembly is where a cable can get lost.
      // CIN 0x4 = SysEx starts / continues, CIN 0x6 = ends with two bytes.
      const EspUsbDeviceMidiPacket start = {0x34, 0xf0, 0x7d, 0x01};
      const EspUsbDeviceMidiPacket end = {0x36, 0x02, 0xf7, 0x00};
      const bool ok = MIDI.writePacket(start) && MIDI.writePacket(end);
      Serial.printf("DEVICE_TX_SYSEX %u\n", ok ? 1 : 0);
    }
    else if (command == 'x')
    {
      // Cable 5 is past this device's cables in both directions. Sending on it
      // would put a packet on a port the host was never told about, so it fails.
      Serial.printf("DEVICE_TX_UNKNOWN_CABLE %u\n",
                    MIDI.noteOn(0, 60, 100, OUT_CABLES) ? 1 : 0);
    }
    else if (command == 'X')
    {
      // Cable 4 exists for receiving but not for sending, so the send helpers must
      // refuse it: the range that matters to a sender is inCableCount(). This is
      // the case a symmetric device cannot express.
      Serial.printf("DEVICE_TX_RECEIVE_ONLY_CABLE %u\n",
                    MIDI.noteOn(0, 60, 100, IN_CABLES) ? 1 : 0);
    }
  }

  // Drained rather than read one per pass, so several packets that arrive in one
  // transfer are all reported before the next command is handled.
  EspUsbDeviceMidiPacket packet;
  while (MIDI.readPacket(packet))
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
