#include "EspUsbDevice.h"
#include "class/midi/midi.h"
#include "class/audio/audio.h"
#include "device/usbd.h"
#include "internal/EspUsbDeviceEndpoint.h"
#include <string.h>
#include <utility>
static_assert(sizeof(EspUsbDeviceMidiPacket) == 4, "USB-MIDI events are four bytes");

EspUsbDeviceMidi::EspUsbDeviceMidi(EspUsbDevice &device, uint8_t cables)
    : EspUsbDeviceMidi(device, cables, cables) {}
EspUsbDeviceMidi::EspUsbDeviceMidi(EspUsbDevice &device, uint8_t inCables, uint8_t outCables)
    : EspUsbDeviceBulkStream(device) {
  midiConfig_.inCables = inCables; midiConfig_.outCables = outCables;
}
EspUsbDeviceMidi::~EspUsbDeviceMidi() { end(); }

esp_err_t EspUsbDeviceMidi::configure(const EspUsbMidiConfig &c) {
  if (state_) return ESP_ERR_INVALID_STATE;
  if (!c.inCables || c.inCables > MAX_CABLES || !c.outCables || c.outCables > MAX_CABLES ||
      c.rxPackets > SIZE_MAX / 8 || c.txPackets > SIZE_MAX / 8 ||
      (c.rxMode != EspUsbMidiRxMode::Queue && c.rxMode != EspUsbMidiRxMode::Callback && c.rxMode != EspUsbMidiRxMode::Disabled) ||
      (c.rxMode != EspUsbMidiRxMode::Queue && (c.rxPackets || c.rxBuffer))) return ESP_ERR_INVALID_ARG;
  if (c.transferPackets > UINT16_MAX / 4) return ESP_ERR_INVALID_SIZE;
  EspUsbBulkStreamConfig bulk;
  bulk.rxCapacity = c.rxPackets * 4; bulk.txCapacity = c.txPackets * 4;
  uint16_t automatic = 64;
#if defined(CONFIG_IDF_TARGET_ESP32P4) || defined(CONFIG_IDF_TARGET_ESP32S31)
  if (device_.config().controller != EspUsbController::FullSpeed) automatic = 512;
#endif
  bulk.transferBytes = c.txPackets ? uint16_t(c.transferPackets ? c.transferPackets * 4 : automatic) : 0;
  bulk.rxBuffer = reinterpret_cast<uint8_t *>(c.rxBuffer); bulk.txBuffer = reinterpret_cast<uint8_t *>(c.txBuffer);
  bulk.rxMode = c.rxMode == EspUsbMidiRxMode::Queue ? EspUsbBulkRxMode::Stream : c.rxMode == EspUsbMidiRxMode::Callback ? EspUsbBulkRxMode::Callback : EspUsbBulkRxMode::Disabled;
  bulk.autoFlush = c.autoFlush; bulk.streamTermination = c.streamTermination;
  const auto error = EspUsbDeviceBulkStream::configure(bulk);
  if (error == ESP_OK) midiConfig_ = c;
  return error;
}
bool EspUsbDeviceMidi::begin() {
  if (state_ || !registered_) { startError_ = ESP_ERR_INVALID_STATE; return false; }
  startError_ = configure(midiConfig_);
  if (startError_ != ESP_OK) return false;
  if (midiConfig_.rxMode == EspUsbMidiRxMode::Callback && !packetCallback_) { startError_ = ESP_ERR_INVALID_ARG; return false; }
  EspUsbDeviceBulkStream::onRxData([this](const uint8_t *data, size_t bytes) {
    for (size_t i = 0; i < bytes; i += 4) {
      EspUsbDeviceMidiPacket packet; memcpy(&packet, data + i, 4);
      if (packetCallback_) packetCallback_(packet);
    }
  });
  receiveErrors_.store(0, std::memory_order_relaxed);
  return EspUsbDeviceBulkStream::begin();
}
esp_err_t EspUsbDeviceMidi::onPacket(PacketCallback callback) {
  if (state_) return ESP_ERR_INVALID_STATE;
  packetCallback_ = std::move(callback); return ESP_OK;
}
size_t EspUsbDeviceMidi::availablePackets() { return size_t(EspUsbDeviceBulkStream::available()) / 4; }
size_t EspUsbDeviceMidi::availableForWritePackets() const { return EspUsbDeviceBulkStream::writeAvailable() / 4; }
size_t EspUsbDeviceMidi::receiveCapacity() const { return EspUsbDeviceBulkStream::readCapacity() / 4; }
size_t EspUsbDeviceMidi::sendCapacity() const { return EspUsbDeviceBulkStream::writeCapacity() / 4; }
size_t EspUsbDeviceMidi::transferCapacity() const { return copyTransferCapacity() / 4; }
bool EspUsbDeviceMidi::readPacket(EspUsbDeviceMidiPacket &packet) {
  return EspUsbDeviceBulkStream::read(reinterpret_cast<uint8_t *>(&packet), 4) == 4;
}
size_t EspUsbDeviceMidi::write(const uint8_t *data, size_t bytes) {
  if (!data || bytes % 4) return 0;
  return EspUsbDeviceBulkStream::write(data, bytes);
}
bool EspUsbDeviceMidi::validateBulkTx(const uint8_t *data, size_t bytes) const {
  for (size_t i = 0; i < bytes; i += 4) if ((data[i] >> 4) >= midiConfig_.inCables || (data[i] & 15) < 2) return false;
  return true;
}
bool EspUsbDeviceMidi::writePacket(const EspUsbDeviceMidiPacket &packet) {
  return write(reinterpret_cast<const uint8_t *>(&packet), 4) == 4;
}
size_t EspUsbDeviceMidi::filterBulkRx(uint8_t *data, size_t bytes) {
  size_t accepted = 0;
  if (bytes % 4) receiveErrors_.fetch_add(1, std::memory_order_relaxed);
  for (size_t i = 0; i + 4 <= bytes; i += 4) {
    if ((data[i] >> 4) >= midiConfig_.outCables || (data[i] & 15) < 2) {
      receiveErrors_.fetch_add(1, std::memory_order_relaxed); continue;
    }
    if (accepted != i) memmove(data + accepted, data + i, 4);
    accepted += 4;
  }
  return accepted;
}
uint16_t EspUsbDeviceMidi::openBulkInterface(uint8_t port, const uint8_t *data, uint16_t length,
    uint8_t &in, uint8_t &out, uint16_t &mps) {
  const size_t expected = descriptorLength();
  if (length < expected || expected < 34 || data[0] != 9 || data[1] != 4 || data[4] || data[5] != 1 || data[6] != 1 ||
      data[9] != 9 || data[10] != 0x24 || data[11] != 1 || data[16] != 1 || data[17] != interfaceNumber_ + 1 ||
      data[18] != 9 || data[19] != 4 || data[20] != interfaceNumber_ + 1 || data[22] != 2 || data[23] != 1 || data[24] != 3 ||
      data[27] != 7 || data[28] != 0x24 || data[29] != 1 || (data[32] | uint16_t(data[33]) << 8) != expected - 27) return 0;
  uint8_t endpoints[14] = {}; unsigned count = 0;
  bool pending = false; uint8_t cables = 0, firstJack = 0;
  for (size_t offset = 34; offset < expected;) {
    const auto *d = data + offset;
    if (d[0] < 2 || d[0] > expected - offset) return 0;
    if (pending) {
      if (d[1] != 0x25 || d[0] != 4 + cables || d[2] != 1 || d[3] != cables) return 0;
      for (unsigned i = 0; i < cables; ++i) if (d[4 + i] != firstJack + 4 * i) return 0;
      pending = false;
    } else if (d[1] == 5) {
      if (count == 2 || d[0] != 9) return 0;
      memcpy(endpoints + 7 * count, d, 7); endpoints[7 * count++] = 7;
      cables = d[2] & 0x80 ? midiConfig_.inCables : midiConfig_.outCables;
      firstJack = d[2] & 0x80 ? 3 : 1; pending = true;
    } else if (d[1] != 0x24) return 0;
    offset += d[0];
  }
  return count == 2 && !pending && openBulkPair(port, endpoints, in, out, mps) ? uint16_t(expected) : 0;
}

// TinyUSB's TUD_MIDI_DESC_JACK_DESC() always emits all four jacks of a cable, so
// it can only describe a cable that exists in both directions. These two emit the
// pair a one-directional cable needs, keeping the same field layout, jack ID
// formula and In-jacks-before-Out-jacks order as that macro.
//
// An embedded jack always takes its data from an external jack of the opposite
// name, which is what makes the jack graph complete: a host that walks it must not
// find an embedded jack with no source.
#define MIDI_ONE_WAY_JACK_LEN (6 + 9)

// Device to host only: the embedded OUT jack the bulk IN endpoint will list, fed
// by an external IN jack.
#define MIDI_DESC_JACK_IN_ONLY(_cablenum) \
  /* MS In Jack (External) */ \
  6, TUSB_DESC_CS_INTERFACE, MIDI_CS_INTERFACE_IN_JACK, MIDI_JACK_EXTERNAL, TUD_MIDI_JACKID_IN_EXT(_cablenum), 0, \
  /* MS Out Jack (Embedded), connected to In Jack External */ \
  9, TUSB_DESC_CS_INTERFACE, MIDI_CS_INTERFACE_OUT_JACK, MIDI_JACK_EMBEDDED, TUD_MIDI_JACKID_OUT_EMB(_cablenum), 1, TUD_MIDI_JACKID_IN_EXT(_cablenum), 1, 0

// Host to device only: the embedded IN jack the bulk OUT endpoint will list,
// feeding an external OUT jack.
#define MIDI_DESC_JACK_OUT_ONLY(_cablenum) \
  /* MS In Jack (Embedded) */ \
  6, TUSB_DESC_CS_INTERFACE, MIDI_CS_INTERFACE_IN_JACK, MIDI_JACK_EMBEDDED, TUD_MIDI_JACKID_IN_EMB(_cablenum), 0, \
  /* MS Out Jack (External), connected to In Jack Embedded */ \
  9, TUSB_DESC_CS_INTERFACE, MIDI_CS_INTERFACE_OUT_JACK, MIDI_JACK_EXTERNAL, TUD_MIDI_JACKID_OUT_EXT(_cablenum), 1, TUD_MIDI_JACKID_IN_EMB(_cablenum), 1, 0

uint16_t EspUsbDeviceMidi::descriptorLength() const
{
  if (!midiConfig_.inCables || midiConfig_.inCables > MAX_CABLES || !midiConfig_.outCables || midiConfig_.outCables > MAX_CABLES) return 0;
  // Cables present in both directions get the full four-jack group; the surplus
  // cables of the longer direction only need the two jacks of that direction.
  const uint8_t both = midiConfig_.inCables < midiConfig_.outCables ? midiConfig_.inCables : midiConfig_.outCables;
  const uint8_t surplus = static_cast<uint8_t>(
      (midiConfig_.inCables > midiConfig_.outCables ? midiConfig_.inCables : midiConfig_.outCables) - both);
  return static_cast<uint16_t>(TUD_MIDI_DESC_HEAD_LEN +
                               TUD_MIDI_DESC_JACK_LEN * both +
                               MIDI_ONE_WAY_JACK_LEN * surplus +
                               TUD_MIDI_DESC_EP_LEN(midiConfig_.outCables) +
                               TUD_MIDI_DESC_EP_LEN(midiConfig_.inCables));
}

// TUD_MIDI_DESCRIPTOR() is hardcoded to one cable, so the pieces it is built
// from are emitted here instead. Most of them are already parameterized by the
// cable count - including each MS endpoint descriptor's bNumEmbMIDIJack - so what
// changes per cable is how many jack descriptors and trailing jack IDs get
// written.
//
// The class document names embedded jacks from the device's side, which is the
// opposite of the endpoint direction they belong to: the bulk IN endpoint (device
// to host) carries the Embedded MIDI OUT Jacks, and the bulk OUT endpoint the
// Embedded MIDI IN Jacks. Hence midiConfig_.inCables pairs with JACKID_OUT_EMB below.
uint16_t EspUsbDeviceMidi::configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize)
{
  if (endpointNumber == 0 || endpointNumber > 15 || !descriptorLength())
  {
    return 0;
  }
  if (!dst) return descriptorLength();
  const uint8_t inCables = midiConfig_.inCables;
  const uint8_t outCables = midiConfig_.outCables;
  const uint8_t both = inCables < outCables ? inCables : outCables;
  interfaceNumber_ = interfaceNumber;
  const uint8_t epOut = endpointNumber;
  const uint8_t epIn = static_cast<uint8_t>(0x80 | endpointNumber);
  uint16_t offset = 0;

  {
    // The macro derives wTotalLength from a single cable count, which cannot
    // describe an asymmetric interface, so it is overwritten below with the
    // length this function actually writes. For a symmetric interface the two
    // agree. Each direction's embedded jack associations are emitted below.
    const uint8_t head[] = {TUD_MIDI_DESC_HEAD(interfaceNumber, stringIndex_, inCables)};
    memcpy(&dst[offset], head, sizeof(head));
    dst[26] = stringIndex_;
    offset = static_cast<uint16_t>(offset + sizeof(head));
  }
  // Descriptor cable numbers are 1-based; the packet header's cable field is
  // 0-based, so cable N here is what a packet addresses as N-1.
  for (uint8_t cable = 1; cable <= both; cable++)
  {
    const uint8_t jack[] = {TUD_MIDI_DESC_JACK_DESC(cable, 0)};
    memcpy(&dst[offset], jack, sizeof(jack));
    offset = static_cast<uint16_t>(offset + sizeof(jack));
  }
  // Cables that exist in one direction only. Jack IDs keep coming from the same
  // per-cable formula, so the IDs of the missing direction are simply skipped -
  // jack IDs have to be unique, not contiguous.
  for (uint8_t cable = static_cast<uint8_t>(both + 1); cable <= inCables; cable++)
  {
    const uint8_t jack[] = {MIDI_DESC_JACK_IN_ONLY(cable)};
    memcpy(&dst[offset], jack, sizeof(jack));
    offset = static_cast<uint16_t>(offset + sizeof(jack));
  }
  for (uint8_t cable = static_cast<uint8_t>(both + 1); cable <= outCables; cable++)
  {
    const uint8_t jack[] = {MIDI_DESC_JACK_OUT_ONLY(cable)};
    memcpy(&dst[offset], jack, sizeof(jack));
    offset = static_cast<uint16_t>(offset + sizeof(jack));
  }
  {
    const uint8_t out[] = {TUD_MIDI_DESC_EP(epOut, endpointSize, outCables)};
    memcpy(&dst[offset], out, sizeof(out));
    offset = static_cast<uint16_t>(offset + sizeof(out));
  }
  for (uint8_t cable = 1; cable <= outCables; cable++)
  {
    dst[offset++] = TUD_MIDI_JACKID_IN_EMB(cable);
  }
  {
    const uint8_t in[] = {TUD_MIDI_DESC_EP(epIn, endpointSize, inCables)};
    memcpy(&dst[offset], in, sizeof(in));
    offset = static_cast<uint16_t>(offset + sizeof(in));
  }
  for (uint8_t cable = 1; cable <= inCables; cable++)
  {
    dst[offset++] = TUD_MIDI_JACKID_OUT_EMB(cable);
  }
  // wTotalLength covers the MS header and everything after it, and sits in the
  // last two bytes of the head.
  const uint16_t msTotal = static_cast<uint16_t>(offset - (TUD_MIDI_DESC_HEAD_LEN - 7));
  dst[TUD_MIDI_DESC_HEAD_LEN - 2] = static_cast<uint8_t>(msTotal & 0xff);
  dst[TUD_MIDI_DESC_HEAD_LEN - 1] = static_cast<uint8_t>(msTotal >> 8);
  return offset;
}

uint16_t EspUsbDeviceMidi::configurationDescriptorForSpeed(
    uint8_t *dst, size_t capacity, uint8_t interfaceNumber,
    uint8_t endpointNumber, bool highSpeed)
{
  if (!dst) return configurationDescriptor(nullptr, interfaceNumber, endpointNumber, highSpeed ? 512 : 64);
  // A 16-cable descriptor is 572 bytes, so unlike the fixed-size functions this
  // one can genuinely outgrow the buffer. Fail before writing rather than after.
  if (descriptorLength() > capacity)
  {
    return 0;
  }
  return configurationDescriptor(dst, interfaceNumber, endpointNumber,
                                 highSpeed ? 512 : 64);
}

bool EspUsbDeviceMidi::noteOn(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t cable)
{
  if (cable >= MAX_CABLES)
  {
    return false;
  }
  EspUsbDeviceMidiPacket packet = {header(ESP_USB_DEVICE_MIDI_CIN_NOTE_ON, cable), status(ESP_USB_DEVICE_MIDI_CIN_NOTE_ON, channel), clamp7(note), clamp7(velocity)};
  return writePacket(packet);
}

bool EspUsbDeviceMidi::noteOff(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t cable)
{
  if (cable >= MAX_CABLES)
  {
    return false;
  }
  EspUsbDeviceMidiPacket packet = {header(ESP_USB_DEVICE_MIDI_CIN_NOTE_OFF, cable), status(ESP_USB_DEVICE_MIDI_CIN_NOTE_OFF, channel), clamp7(note), clamp7(velocity)};
  return writePacket(packet);
}

bool EspUsbDeviceMidi::controlChange(uint8_t channel, uint8_t control, uint8_t value, uint8_t cable)
{
  if (cable >= MAX_CABLES)
  {
    return false;
  }
  EspUsbDeviceMidiPacket packet = {header(ESP_USB_DEVICE_MIDI_CIN_CONTROL_CHANGE, cable), status(ESP_USB_DEVICE_MIDI_CIN_CONTROL_CHANGE, channel), clamp7(control), clamp7(value)};
  return writePacket(packet);
}

bool EspUsbDeviceMidi::programChange(uint8_t channel, uint8_t program, uint8_t cable)
{
  if (cable >= MAX_CABLES)
  {
    return false;
  }
  EspUsbDeviceMidiPacket packet = {header(ESP_USB_DEVICE_MIDI_CIN_PROGRAM_CHANGE, cable), status(ESP_USB_DEVICE_MIDI_CIN_PROGRAM_CHANGE, channel), clamp7(program), 0};
  return writePacket(packet);
}

bool EspUsbDeviceMidi::polyPressure(uint8_t channel, uint8_t note, uint8_t pressure, uint8_t cable)
{
  if (cable >= MAX_CABLES)
  {
    return false;
  }
  EspUsbDeviceMidiPacket packet = {header(ESP_USB_DEVICE_MIDI_CIN_POLY_KEYPRESS, cable), status(ESP_USB_DEVICE_MIDI_CIN_POLY_KEYPRESS, channel), clamp7(note), clamp7(pressure)};
  return writePacket(packet);
}

bool EspUsbDeviceMidi::channelPressure(uint8_t channel, uint8_t pressure, uint8_t cable)
{
  if (cable >= MAX_CABLES)
  {
    return false;
  }
  EspUsbDeviceMidiPacket packet = {header(ESP_USB_DEVICE_MIDI_CIN_CHANNEL_PRESSURE, cable), status(ESP_USB_DEVICE_MIDI_CIN_CHANNEL_PRESSURE, channel), clamp7(pressure), 0};
  return writePacket(packet);
}

bool EspUsbDeviceMidi::pitchBend(uint8_t channel, uint16_t value, uint8_t cable)
{
  if (cable >= MAX_CABLES)
  {
    return false;
  }
  if (value > 16383)
  {
    value = 16383;
  }
  EspUsbDeviceMidiPacket packet = {
      header(ESP_USB_DEVICE_MIDI_CIN_PITCH_BEND_CHANGE, cable),
      status(ESP_USB_DEVICE_MIDI_CIN_PITCH_BEND_CHANGE, channel),
      static_cast<uint8_t>(value & 0x7f),
      static_cast<uint8_t>((value >> 7) & 0x7f),
  };
  return writePacket(packet);
}

uint8_t EspUsbDeviceMidi::header(uint8_t codeIndex, uint8_t cable)
{
  return static_cast<uint8_t>((cable << 4) | (codeIndex & 0x0f));
}

uint8_t EspUsbDeviceMidi::status(uint8_t codeIndex, uint8_t channel)
{
  if (channel > 15)
  {
    channel = 15;
  }
  return static_cast<uint8_t>((codeIndex << 4) | channel);
}

uint8_t EspUsbDeviceMidi::clamp7(uint8_t value)
{
  return value > 127 ? 127 : value;
}

