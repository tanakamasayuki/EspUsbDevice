// Host-side unit test for the multi-cable USB MIDI configuration descriptor.
//
// The builder and TinyUSB's descriptor macros are extracted from src/ by
// test_midi_descriptor.py into "espusbdevice_midi_real.h", so these checks run
// against the shipped code.
//
// A wrong cable count here does not stop a host from enumerating the device - it
// just shows the wrong number of MIDI ports, or lands packets on a port the host
// never advertised - so the descriptor is checked field by field rather than only
// end to end. The fields that matter: the MS header's wTotalLength (a host that
// trusts it stops parsing early and loses cables), each MS endpoint descriptor's
// bNumEmbMIDIJack plus its trailing jack ID list (this is what maps a packet's
// cable number to a port), and jack ID uniqueness (duplicates make the jack graph
// ambiguous).

#include <cstdint>
#include <cstddef>
#include <cstdio>

#include "espusbdevice_midi_real.h"

namespace
{
int failures = 0;

void check(bool condition, const char *name)
{
  if (!condition)
  {
    printf("FAIL %s\n", name);
    failures++;
  }
}

void checkCable(bool condition, const char *name, int cables)
{
  if (!condition)
  {
    printf("FAIL %s (cables=%d)\n", name, cables);
    failures++;
  }
}

uint16_t le16(const uint8_t *data)
{
  return static_cast<uint16_t>(data[0]) | static_cast<uint16_t>(data[1] << 8);
}

constexpr uint8_t DESC_INTERFACE = 0x04;
constexpr uint8_t DESC_ENDPOINT = 0x05;
constexpr uint8_t DESC_CS_INTERFACE = 0x24;
constexpr uint8_t DESC_CS_ENDPOINT = 0x25;
constexpr uint8_t CLASS_AUDIO = 0x01;
constexpr uint8_t SUBCLASS_AUDIO_CONTROL = 0x01;
constexpr uint8_t SUBCLASS_MIDI_STREAMING = 0x03;
constexpr uint8_t CS_IN_JACK = 0x02;
constexpr uint8_t CS_OUT_JACK = 0x03;
constexpr uint8_t JACK_EMBEDDED = 0x01;
constexpr uint8_t JACK_EXTERNAL = 0x02;

constexpr uint8_t ITF = 3;
constexpr uint8_t EP = 2;
constexpr uint16_t EP_SIZE = 64;

// Every jack ID seen, so duplicates across the whole descriptor are caught rather
// than only within one cable's group.
bool jackIdSeen[256];

void verify(int cables)
{
  cableCount_ = static_cast<uint8_t>(cables);
  for (int i = 0; i < 256; i++)
  {
    jackIdSeen[i] = false;
  }

  uint8_t buffer[1024];
  for (size_t i = 0; i < sizeof(buffer); i++)
  {
    buffer[i] = 0xcd;
  }

  const uint16_t expected =
      static_cast<uint16_t>(34 + 30 * cables + (13 + cables) * 2);
  checkCable(midiDescriptorLength() == expected, "descriptorLength", cables);

  const uint16_t length = midiConfigurationDescriptor(buffer, ITF, EP, EP_SIZE);
  checkCable(length == expected, "written_length", cables);
  // Nothing may be written past what descriptorLength() promised, because that is
  // the only figure the caller used to size the buffer.
  checkCable(buffer[length] == 0xcd, "no_overrun", cables);

  uint16_t offset = 0;

  // Audio Control interface: no endpoints of its own, it only exists to own the
  // MIDI Streaming interface below.
  const uint8_t *ac = &buffer[offset];
  checkCable(ac[0] == 9 && ac[1] == DESC_INTERFACE, "ac_header", cables);
  checkCable(ac[2] == ITF && ac[3] == 0, "ac_interface_number", cables);
  checkCable(ac[4] == 0, "ac_endpoint_count", cables);
  checkCable(ac[5] == CLASS_AUDIO && ac[6] == SUBCLASS_AUDIO_CONTROL,
             "ac_class", cables);
  offset += 9;

  const uint8_t *acHeader = &buffer[offset];
  checkCable(acHeader[0] == 9 && acHeader[1] == DESC_CS_INTERFACE,
             "ac_cs_header", cables);
  checkCable(le16(&acHeader[3]) == 0x0100, "ac_bcdADC", cables);
  checkCable(le16(&acHeader[5]) == 9, "ac_wTotalLength", cables);
  checkCable(acHeader[7] == 1, "ac_bInCollection", cables);
  checkCable(acHeader[8] == ITF + 1, "ac_collection_target", cables);
  offset += 9;

  // MIDI Streaming interface: two bulk endpoints regardless of cable count.
  const uint8_t *ms = &buffer[offset];
  checkCable(ms[0] == 9 && ms[1] == DESC_INTERFACE, "ms_header", cables);
  checkCable(ms[2] == ITF + 1 && ms[3] == 0, "ms_interface_number", cables);
  checkCable(ms[4] == 2, "ms_endpoint_count", cables);
  checkCable(ms[5] == CLASS_AUDIO && ms[6] == SUBCLASS_MIDI_STREAMING,
             "ms_class", cables);
  offset += 9;

  const uint8_t *msHeader = &buffer[offset];
  checkCable(msHeader[0] == 7 && msHeader[1] == DESC_CS_INTERFACE,
             "ms_cs_header", cables);
  checkCable(le16(&msHeader[3]) == 0x0100, "ms_bcdMSC", cables);
  // Counted from the MS header itself to the end of the descriptor: a host that
  // trusts a short value silently drops the trailing cables.
  checkCable(le16(&msHeader[5]) == length - 27, "ms_wTotalLength", cables);
  offset += 7;

  // One jack group per cable. Descriptor cable numbers are 1-based.
  for (int cable = 1; cable <= cables; cable++)
  {
    const uint8_t inEmb = static_cast<uint8_t>((cable - 1) * 4 + 1);
    const uint8_t inExt = static_cast<uint8_t>((cable - 1) * 4 + 2);
    const uint8_t outEmb = static_cast<uint8_t>((cable - 1) * 4 + 3);
    const uint8_t outExt = static_cast<uint8_t>((cable - 1) * 4 + 4);

    const uint8_t *jack = &buffer[offset];
    checkCable(jack[0] == 6 && jack[1] == DESC_CS_INTERFACE &&
                   jack[2] == CS_IN_JACK && jack[3] == JACK_EMBEDDED &&
                   jack[4] == inEmb,
               "jack_in_embedded", cables);
    checkCable(jack[6] == 6 && jack[8] == CS_IN_JACK &&
                   jack[9] == JACK_EXTERNAL && jack[10] == inExt,
               "jack_in_external", cables);
    // Embedded out jack is fed by this cable's external in jack, and vice versa;
    // crossing these is what makes the port appear one-directional to a host.
    checkCable(jack[12] == 9 && jack[14] == CS_OUT_JACK &&
                   jack[15] == JACK_EMBEDDED && jack[16] == outEmb &&
                   jack[17] == 1 && jack[18] == inExt,
               "jack_out_embedded", cables);
    checkCable(jack[21] == 9 && jack[23] == CS_OUT_JACK &&
                   jack[24] == JACK_EXTERNAL && jack[25] == outExt &&
                   jack[26] == 1 && jack[27] == inEmb,
               "jack_out_external", cables);

    const uint8_t ids[] = {inEmb, inExt, outEmb, outExt};
    for (uint8_t id : ids)
    {
      checkCable(id != 0, "jack_id_nonzero", cables);
      checkCable(!jackIdSeen[id], "jack_id_unique", cables);
      jackIdSeen[id] = true;
    }
    offset += 30;
  }

  // Each endpoint is followed by its MS endpoint descriptor, whose trailing jack
  // ID list is the cable-number-to-port mapping the host uses.
  const uint8_t directions[] = {0x00, 0x80};
  for (uint8_t direction : directions)
  {
    const bool in = direction != 0;
    const uint8_t *ep = &buffer[offset];
    // Audio v1.0 endpoints are 9 bytes, not the usual 7.
    checkCable(ep[0] == 9 && ep[1] == DESC_ENDPOINT, "ep_header", cables);
    checkCable(ep[2] == (direction | EP), "ep_address", cables);
    checkCable((ep[3] & 0x03) == 2, "ep_bulk", cables);
    checkCable(le16(&ep[4]) == EP_SIZE, "ep_packet_size", cables);
    offset += 9;

    const uint8_t *csEp = &buffer[offset];
    checkCable(csEp[0] == 4 + cables, "cs_ep_length", cables);
    checkCable(csEp[1] == DESC_CS_ENDPOINT && csEp[2] == 0x01, "cs_ep_type",
               cables);
    checkCable(csEp[3] == cables, "cs_ep_jack_count", cables);
    for (int cable = 1; cable <= cables; cable++)
    {
      // The OUT endpoint carries data into the embedded in jacks; the IN endpoint
      // carries data out of the embedded out jacks.
      const uint8_t want = static_cast<uint8_t>((cable - 1) * 4 + (in ? 3 : 1));
      checkCable(csEp[4 + cable - 1] == want, "cs_ep_jack_id", cables);
    }
    offset += 4 + cables;
  }

  checkCable(offset == length, "consumed_all", cables);

  // A host walks the descriptor by bLength; if any of them disagrees with the
  // layout above it reads garbage from the first mismatch onwards.
  uint16_t walk = 0;
  int walked = 0;
  while (walk + 2 <= length)
  {
    const uint8_t bLength = buffer[walk];
    if (bLength < 2 || walk + bLength > length)
    {
      break;
    }
    walk = static_cast<uint16_t>(walk + bLength);
    walked++;
  }
  checkCable(walk == length, "blength_walk_lands_on_end", cables);
  checkCable(walked == 4 + 4 * cables + 4, "blength_walk_count", cables);
}

} // namespace

int main()
{
  for (int cables = 1; cables <= 16; cables++)
  {
    verify(cables);
  }

  // The default stays byte-for-byte TinyUSB's single-cable template, so existing
  // sketches see exactly the descriptor they saw before cable support existed.
  {
    cableCount_ = 1;
    uint8_t mine[256] = {};
    const uint16_t length = midiConfigurationDescriptor(mine, ITF, EP, EP_SIZE);
    const uint8_t theirs[] = {
        TUD_MIDI_DESCRIPTOR(ITF, 0, EP, 0x80 | EP, EP_SIZE),
    };
    check(length == sizeof(theirs), "one_cable_matches_tinyusb_length");
    check(length == TUD_MIDI_DESC_LEN, "one_cable_matches_TUD_MIDI_DESC_LEN");
    bool identical = true;
    for (size_t i = 0; i < sizeof(theirs); i++)
    {
      if (mine[i] != theirs[i])
      {
        identical = false;
        printf("byte %zu: got 0x%02x want 0x%02x\n", i, mine[i], theirs[i]);
      }
    }
    check(identical, "one_cable_matches_tinyusb_bytes");
  }

  // Sixteen cables is 572 bytes, so unlike the fixed-size functions this one can
  // outgrow the configuration buffer. It has to refuse before writing, not after.
  {
    uint8_t buffer[1024];
    for (size_t i = 0; i < sizeof(buffer); i++)
    {
      buffer[i] = 0xcd;
    }
    cableCount_ = 16;
    check(midiDescriptorLength() == 572, "sixteen_cables_is_572_bytes");
    check(midiConfigurationDescriptorForSpeed(buffer, 571, ITF, EP, false) == 0,
          "rejects_short_capacity");
    check(buffer[0] == 0xcd, "rejects_without_writing");
    check(midiConfigurationDescriptorForSpeed(buffer, 572, ITF, EP, false) == 572,
          "accepts_exact_capacity");
    // High speed only changes wMaxPacketSize, never the length, because the
    // composite builder patches bulk endpoints in place and compares lengths.
    check(midiConfigurationDescriptorForSpeed(buffer, 572, ITF, EP, true) == 572,
          "high_speed_same_length");
    check(le16(&buffer[34 + 30 * 16 + 4]) == 512, "high_speed_packet_size");

    cableCount_ = 1;
    check(midiConfigurationDescriptorForSpeed(buffer, 91, ITF, EP, false) == 0,
          "one_cable_rejects_91_bytes");
    check(midiConfigurationDescriptorForSpeed(buffer, 92, ITF, EP, false) == 92,
          "one_cable_accepts_92_bytes");
  }

  // Bad arguments must not write anything.
  {
    cableCount_ = 4;
    uint8_t buffer[1024] = {};
    check(midiConfigurationDescriptor(nullptr, ITF, EP, EP_SIZE) == 0,
          "rejects_null_destination");
    check(midiConfigurationDescriptor(buffer, ITF, 0, EP_SIZE) == 0,
          "rejects_endpoint_zero");
    check(buffer[0] == 0, "endpoint_zero_writes_nothing");
  }

  if (failures != 0)
  {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("OK\n");
  return 0;
}
