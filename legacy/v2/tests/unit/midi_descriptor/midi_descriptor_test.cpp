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
//
// Asymmetric counts get the same treatment for one more reason: the class document
// names embedded jacks from the device's side, which is the opposite of the
// endpoint direction they belong to. Swapping the two is invisible on a symmetric
// interface, and no round-trip test can see it either, because a received packet's
// cable number is read from its own header.

#include <cstdint>
#include <cstddef>
#include <cstdio>

#include "espusbdevice_midi_real.h"

namespace
{
int failures = 0;

int currentIn = 0;
int currentOut = 0;

void check(bool condition, const char *name)
{
  if (!condition)
  {
    printf("FAIL %s\n", name);
    failures++;
  }
}

// Every failure is reported with the cable counts it happened at, because the same
// assertion runs for every combination.
void checkCables(bool condition, const char *name)
{
  if (!condition)
  {
    printf("FAIL %s (in=%d out=%d)\n", name, currentIn, currentOut);
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

// Jack IDs come from a per-cable formula rather than a running counter, so a
// one-directional cable leaves gaps. Uniqueness is what matters, and it is checked
// across the whole descriptor rather than within one cable's group.
uint8_t jackIdSeen[256];
// Which embedded jacks were declared, so the endpoint jack ID lists can be checked
// to reference declared jacks and to reference each exactly once.
bool inEmbDeclared[256];
bool outEmbDeclared[256];

uint8_t jackIdInEmb(int cable) { return static_cast<uint8_t>((cable - 1) * 4 + 1); }
uint8_t jackIdInExt(int cable) { return static_cast<uint8_t>((cable - 1) * 4 + 2); }
uint8_t jackIdOutEmb(int cable) { return static_cast<uint8_t>((cable - 1) * 4 + 3); }
uint8_t jackIdOutExt(int cable) { return static_cast<uint8_t>((cable - 1) * 4 + 4); }

void noteJackId(uint8_t id)
{
  checkCables(id != 0, "jack_id_nonzero");
  checkCables(jackIdSeen[id] == 0, "jack_id_unique");
  jackIdSeen[id] = 1;
}

// MS In Jack: bLength, CS_INTERFACE, IN_JACK, bJackType, bJackID, iJack.
const uint8_t *checkInJack(const uint8_t *jack, uint8_t type, uint8_t id, const char *name)
{
  checkCables(jack[0] == 6 && jack[1] == DESC_CS_INTERFACE && jack[2] == CS_IN_JACK &&
                  jack[3] == type && jack[4] == id,
              name);
  noteJackId(id);
  if (type == JACK_EMBEDDED)
  {
    inEmbDeclared[id] = true;
  }
  return jack + 6;
}

// MS Out Jack: bLength, CS_INTERFACE, OUT_JACK, bJackType, bJackID,
// bNrInputPins, baSourceID, baSourcePin, iJack. Every embedded jack has to have a
// source, or a host walking the jack graph finds a dead end.
const uint8_t *checkOutJack(const uint8_t *jack, uint8_t type, uint8_t id, uint8_t source,
                            const char *name)
{
  checkCables(jack[0] == 9 && jack[1] == DESC_CS_INTERFACE && jack[2] == CS_OUT_JACK &&
                  jack[3] == type && jack[4] == id && jack[5] == 1 && jack[6] == source &&
                  jack[7] == 1,
              name);
  noteJackId(id);
  if (type == JACK_EMBEDDED)
  {
    outEmbDeclared[id] = true;
  }
  return jack + 9;
}

// in = cables device to host, out = cables host to device, both host-view, as the
// endpoint directions and EspUsbHostMidiPortInfo are.
void verify(int in, int out)
{
  currentIn = in;
  currentOut = out;
  inCableCount_ = static_cast<uint8_t>(in);
  outCableCount_ = static_cast<uint8_t>(out);
  for (int i = 0; i < 256; i++)
  {
    jackIdSeen[i] = 0;
    inEmbDeclared[i] = false;
    outEmbDeclared[i] = false;
  }

  uint8_t buffer[1024];
  for (size_t i = 0; i < sizeof(buffer); i++)
  {
    buffer[i] = 0xcd;
  }

  const int both = in < out ? in : out;
  const int surplus = (in > out ? in : out) - both;
  const uint16_t expected = static_cast<uint16_t>(
      34 + 30 * both + 15 * surplus + (13 + out) + (13 + in));
  checkCables(midiDescriptorLength() == expected, "descriptorLength");

  const uint16_t length = midiConfigurationDescriptor(buffer, ITF, EP, EP_SIZE);
  checkCables(length == expected, "written_length");
  // Nothing may be written past what descriptorLength() promised, because that is
  // the only figure the caller used to size the buffer.
  checkCables(buffer[length] == 0xcd, "no_overrun");

  uint16_t offset = 0;

  // Audio Control interface: no endpoints of its own, it only exists to own the
  // MIDI Streaming interface below.
  const uint8_t *ac = &buffer[offset];
  checkCables(ac[0] == 9 && ac[1] == DESC_INTERFACE, "ac_header");
  checkCables(ac[2] == ITF && ac[3] == 0, "ac_interface_number");
  checkCables(ac[4] == 0, "ac_endpoint_count");
  checkCables(ac[5] == CLASS_AUDIO && ac[6] == SUBCLASS_AUDIO_CONTROL, "ac_class");
  offset += 9;

  const uint8_t *acHeader = &buffer[offset];
  checkCables(acHeader[0] == 9 && acHeader[1] == DESC_CS_INTERFACE, "ac_cs_header");
  checkCables(le16(&acHeader[3]) == 0x0100, "ac_bcdADC");
  checkCables(le16(&acHeader[5]) == 9, "ac_wTotalLength");
  checkCables(acHeader[7] == 1, "ac_bInCollection");
  checkCables(acHeader[8] == ITF + 1, "ac_collection_target");
  offset += 9;

  // MIDI Streaming interface: two bulk endpoints regardless of cable counts.
  const uint8_t *ms = &buffer[offset];
  checkCables(ms[0] == 9 && ms[1] == DESC_INTERFACE, "ms_header");
  checkCables(ms[2] == ITF + 1 && ms[3] == 0, "ms_interface_number");
  checkCables(ms[4] == 2, "ms_endpoint_count");
  checkCables(ms[5] == CLASS_AUDIO && ms[6] == SUBCLASS_MIDI_STREAMING, "ms_class");
  offset += 9;

  const uint8_t *msHeader = &buffer[offset];
  checkCables(msHeader[0] == 7 && msHeader[1] == DESC_CS_INTERFACE, "ms_cs_header");
  checkCables(le16(&msHeader[3]) == 0x0100, "ms_bcdMSC");
  // Counted from the MS header itself to the end of the descriptor: a host that
  // trusts a short value silently drops the trailing cables. The template macro
  // cannot compute this for an asymmetric interface, so the builder overwrites it.
  checkCables(le16(&msHeader[5]) == length - 27, "ms_wTotalLength");
  offset += 7;

  // Cables present in both directions: the full four-jack group, in TinyUSB's
  // order. Descriptor cable numbers are 1-based; a packet addresses cable N here
  // as N-1.
  for (int cable = 1; cable <= both; cable++)
  {
    const uint8_t *jack = &buffer[offset];
    jack = checkInJack(jack, JACK_EMBEDDED, jackIdInEmb(cable), "jack_in_embedded");
    jack = checkInJack(jack, JACK_EXTERNAL, jackIdInExt(cable), "jack_in_external");
    // The embedded out jack is fed by this cable's external in jack and vice versa;
    // crossing these makes the port look one-directional to a host.
    jack = checkOutJack(jack, JACK_EMBEDDED, jackIdOutEmb(cable), jackIdInExt(cable),
                        "jack_out_embedded");
    jack = checkOutJack(jack, JACK_EXTERNAL, jackIdOutExt(cable), jackIdInEmb(cable),
                        "jack_out_external");
    checkCables(jack == &buffer[offset] + 30, "jack_group_length");
    offset += 30;
  }

  // Surplus device-to-host cables: external in jack feeding an embedded out jack.
  // The jack IDs of the absent direction are simply never used.
  for (int cable = both + 1; cable <= in; cable++)
  {
    const uint8_t *jack = &buffer[offset];
    jack = checkInJack(jack, JACK_EXTERNAL, jackIdInExt(cable), "in_only_external");
    jack = checkOutJack(jack, JACK_EMBEDDED, jackIdOutEmb(cable), jackIdInExt(cable),
                        "in_only_embedded");
    checkCables(jack == &buffer[offset] + 15, "in_only_length");
    offset += 15;
  }

  // Surplus host-to-device cables: embedded in jack feeding an external out jack.
  for (int cable = both + 1; cable <= out; cable++)
  {
    const uint8_t *jack = &buffer[offset];
    jack = checkInJack(jack, JACK_EMBEDDED, jackIdInEmb(cable), "out_only_embedded");
    jack = checkOutJack(jack, JACK_EXTERNAL, jackIdOutExt(cable), jackIdInEmb(cable),
                        "out_only_external");
    checkCables(jack == &buffer[offset] + 15, "out_only_length");
    offset += 15;
  }

  // Each endpoint is followed by its MS endpoint descriptor, whose trailing jack ID
  // list is the cable-number-to-port mapping the host uses. The bulk OUT endpoint
  // carries host-to-device data into the Embedded MIDI IN Jacks, and the bulk IN
  // endpoint carries device-to-host data out of the Embedded MIDI OUT Jacks - the
  // jack names are the reverse of the endpoint direction.
  for (int direction = 0; direction < 2; direction++)
  {
    const bool isIn = direction == 1;
    const int cables = isIn ? in : out;

    const uint8_t *ep = &buffer[offset];
    // Audio v1.0 endpoints are 9 bytes, not the usual 7.
    checkCables(ep[0] == 9 && ep[1] == DESC_ENDPOINT, "ep_header");
    checkCables(ep[2] == ((isIn ? 0x80 : 0x00) | EP), "ep_address");
    checkCables((ep[3] & 0x03) == 2, "ep_bulk");
    checkCables(le16(&ep[4]) == EP_SIZE, "ep_packet_size");
    offset += 9;

    const uint8_t *csEp = &buffer[offset];
    checkCables(csEp[0] == 4 + cables, "cs_ep_length");
    checkCables(csEp[1] == DESC_CS_ENDPOINT && csEp[2] == 0x01, "cs_ep_type");
    checkCables(csEp[3] == cables, "cs_ep_jack_count");
    for (int cable = 1; cable <= cables; cable++)
    {
      const uint8_t want = isIn ? jackIdOutEmb(cable) : jackIdInEmb(cable);
      const uint8_t got = csEp[4 + cable - 1];
      checkCables(got == want, "cs_ep_jack_id");
      // The jack has to have been declared above, and listed only here: an endpoint
      // pointing at an undeclared jack, or two endpoints claiming one jack, is a
      // descriptor a host can read either way.
      bool *declared = isIn ? &outEmbDeclared[got] : &inEmbDeclared[got];
      checkCables(*declared, "cs_ep_jack_declared");
      *declared = false;
    }
    offset += 4 + cables;
  }

  checkCables(offset == length, "consumed_all");

  // No declared embedded jack may be left unreferenced by an endpoint, which is
  // what would happen if a one-directional cable emitted the jacks of both
  // directions.
  for (int i = 0; i < 256; i++)
  {
    checkCables(!inEmbDeclared[i], "every_in_embedded_jack_is_listed");
    checkCables(!outEmbDeclared[i], "every_out_embedded_jack_is_listed");
  }

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
  checkCables(walk == length, "blength_walk_lands_on_end");
  // 4 head + 4 jacks per two-way cable + 2 per one-way cable + 2 per endpoint.
  checkCables(walked == 4 + 4 * both + 2 * surplus + 4, "blength_walk_count");
}

} // namespace

int main()
{
  // Symmetric, every count.
  for (int cables = 1; cables <= 16; cables++)
  {
    verify(cables, cables);
  }

  // Asymmetric, every combination. This is what pins the direction of each
  // endpoint's jack list: with in == out the two are interchangeable.
  for (int in = 1; in <= 16; in++)
  {
    for (int out = 1; out <= 16; out++)
    {
      if (in != out)
      {
        verify(in, out);
      }
    }
  }

  // The default stays byte-for-byte TinyUSB's single-cable template, so existing
  // sketches see exactly the descriptor they saw before cable support existed.
  // This is also what proves overwriting wTotalLength is a no-op when symmetric.
  {
    inCableCount_ = 1;
    outCableCount_ = 1;
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

  // An asymmetric interface must not be describable as a symmetric one: the two
  // endpoints' jack counts have to differ, in the right direction.
  {
    uint8_t buffer[1024] = {};
    inCableCount_ = 5;
    outCableCount_ = 2;
    const uint16_t length = midiConfigurationDescriptor(buffer, ITF, EP, EP_SIZE);
    // 34 head + 30 * 2 two-way + 15 * 3 one-way + (13 + 2) + (13 + 5).
    check(length == 172, "asymmetric_length");
    // Walk to the endpoints: head 34 + jacks 105.
    const uint8_t *outEp = &buffer[34 + 105];
    check(outEp[2] == EP, "asymmetric_out_endpoint_address");
    check(outEp[9 + 3] == 2, "asymmetric_out_endpoint_has_two_jacks");
    const uint8_t *inEp = outEp + 9 + 4 + 2;
    check(inEp[2] == (0x80 | EP), "asymmetric_in_endpoint_address");
    check(inEp[9 + 3] == 5, "asymmetric_in_endpoint_has_five_jacks");
  }

  // Sixteen cables is 572 bytes, so unlike the fixed-size functions this one can
  // outgrow the configuration buffer. It has to refuse before writing, not after.
  {
    uint8_t buffer[1024];
    for (size_t i = 0; i < sizeof(buffer); i++)
    {
      buffer[i] = 0xcd;
    }
    inCableCount_ = 16;
    outCableCount_ = 16;
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

    inCableCount_ = 1;
    outCableCount_ = 1;
    check(midiConfigurationDescriptorForSpeed(buffer, 91, ITF, EP, false) == 0,
          "one_cable_rejects_91_bytes");
    check(midiConfigurationDescriptorForSpeed(buffer, 92, ITF, EP, false) == 92,
          "one_cable_accepts_92_bytes");
  }

  // Bad arguments must not write anything.
  {
    inCableCount_ = 4;
    outCableCount_ = 4;
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
