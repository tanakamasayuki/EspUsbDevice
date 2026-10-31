#include "EspUsbDevice.h"
#include "internal/EspUsbDeviceBus.h"
#include "internal/EspUsbDeviceEndpoint.h"
#include "internal/EspUsbDescriptorModel.h"
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

esp_err_t EspUsbDeviceMidi::configure(const EspUsbMidiConfig &requested) {
  if (state_) return ESP_ERR_INVALID_STATE;
  EspUsbMidiConfig c = requested;
  if (!c.inCables) c.inCables = midiConfig_.inCables;
  if (!c.outCables) c.outCables = midiConfig_.outCables;
  if (!c.inCables || c.inCables > MAX_CABLES || !c.outCables || c.outCables > MAX_CABLES ||
      c.rxPackets > SIZE_MAX / 8 || c.txPackets > SIZE_MAX / 8 ||
      (c.rxMode != EspUsbMidiRxMode::Queue && c.rxMode != EspUsbMidiRxMode::Callback && c.rxMode != EspUsbMidiRxMode::Disabled) ||
      (c.rxMode != EspUsbMidiRxMode::Queue && (c.rxPackets || c.rxBuffer))) return ESP_ERR_INVALID_ARG;
  if (c.transferPackets > UINT16_MAX / 4) return ESP_ERR_INVALID_SIZE;
  EspUsbBulkStreamConfig bulk;
  bulk.rxCapacity = c.rxPackets * 4; bulk.txCapacity = c.txPackets * 4;
  const uint16_t automatic = maximumBulkPacketSize();
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

uint16_t EspUsbDeviceMidi::descriptorLength() const
{
  const auto in = midiConfig_.inCables, out = midiConfig_.outCables;
  if (!in || in > MAX_CABLES || !out || out > MAX_CABLES) return 0;
  // AC/MS headers (34), a 15-byte jack pair per direction/cable, and
  // two endpoint headers (13 each) plus one association byte per cable.
  return uint16_t(60 + 16 * (in + out));
}

uint16_t EspUsbDeviceMidi::configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber,
    uint8_t endpointNumber, uint16_t endpointSize)
{
  const uint16_t length = descriptorLength();
  if (!endpointNumber || endpointNumber > 15 || !length) return 0;
  if (!dst) return length;
  interfaceNumber_ = interfaceNumber;
  const uint8_t in = midiConfig_.inCables, out = midiConfig_.outCables;
  const uint16_t streamingBytes = uint16_t(length - 27);
  const uint8_t head[] = {
    // Audio Control interface and its header, followed by MIDI Streaming.
    9, 4, interfaceNumber, 0, 0, 1, 1, 0, stringIndex_,
    9, 0x24, 1, 0, 1, 9, 0, 1, uint8_t(interfaceNumber + 1),
    9, 4, uint8_t(interfaceNumber + 1), 0, 2, 1, 3, 0, stringIndex_,
    7, 0x24, 1, 0, 1, uint8_t(streamingBytes), uint8_t(streamingBytes >> 8),
  };
  espusb::internal::DescriptorBuffer buffer(dst, length);
  buffer.append(head, sizeof(head));
  auto inputJack = [&](uint8_t kind, uint8_t id) {
    const uint8_t bytes[] = {6, 0x24, 2, kind, id, 0};
    buffer.append(bytes, sizeof(bytes));
  };
  auto outputJack = [&](uint8_t kind, uint8_t id, uint8_t source) {
    const uint8_t bytes[] = {9, 0x24, 3, kind, id, 1, source, 1, 0};
    buffer.append(bytes, sizeof(bytes));
  };
  const uint8_t cables = in > out ? in : out;
  // Descriptor cable IDs are one-based. Embedded OUT jacks feed device IN;
  // embedded IN jacks receive device OUT. Missing directions have no jacks.
  for (uint8_t cable = 1; cable <= cables; ++cable) {
    const uint8_t base = uint8_t((cable - 1) * 4);
    if (cable <= out) inputJack(1, uint8_t(base + 1));
    if (cable <= in) inputJack(2, uint8_t(base + 2));
    if (cable <= in) outputJack(1, uint8_t(base + 3), uint8_t(base + 2));
    if (cable <= out) outputJack(2, uint8_t(base + 4), uint8_t(base + 1));
  }
  auto endpoint = [&](uint8_t address, uint8_t count, uint8_t firstJack) {
    const uint8_t bytes[] = {
      9, 5, address, 2, uint8_t(endpointSize), uint8_t(endpointSize >> 8), 0, 0, 0,
      uint8_t(4 + count), 0x25, 1, count,
    };
    buffer.append(bytes, sizeof(bytes));
    for (uint8_t cable = 0; cable < count; ++cable) buffer.writeU8(uint8_t(firstJack + 4 * cable));
  };
  endpoint(endpointNumber, out, 1);
  endpoint(uint8_t(0x80 | endpointNumber), in, 3);
  return buffer.ok() && buffer.size() == length ? length : 0;
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
  if (cable >= MAX_CABLES || channel > 15 || note > 127 || velocity > 127)
  {
    return rejectMessage();
  }
  EspUsbDeviceMidiPacket packet = {header(ESP_USB_DEVICE_MIDI_CIN_NOTE_ON, cable), status(ESP_USB_DEVICE_MIDI_CIN_NOTE_ON, channel), note, velocity};
  return writePacket(packet);
}

bool EspUsbDeviceMidi::noteOff(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t cable)
{
  if (cable >= MAX_CABLES || channel > 15 || note > 127 || velocity > 127)
  {
    return rejectMessage();
  }
  EspUsbDeviceMidiPacket packet = {header(ESP_USB_DEVICE_MIDI_CIN_NOTE_OFF, cable), status(ESP_USB_DEVICE_MIDI_CIN_NOTE_OFF, channel), note, velocity};
  return writePacket(packet);
}

bool EspUsbDeviceMidi::controlChange(uint8_t channel, uint8_t control, uint8_t value, uint8_t cable)
{
  if (cable >= MAX_CABLES || channel > 15 || control > 127 || value > 127)
  {
    return rejectMessage();
  }
  EspUsbDeviceMidiPacket packet = {header(ESP_USB_DEVICE_MIDI_CIN_CONTROL_CHANGE, cable), status(ESP_USB_DEVICE_MIDI_CIN_CONTROL_CHANGE, channel), control, value};
  return writePacket(packet);
}

bool EspUsbDeviceMidi::programChange(uint8_t channel, uint8_t program, uint8_t cable)
{
  if (cable >= MAX_CABLES || channel > 15 || program > 127)
  {
    return rejectMessage();
  }
  EspUsbDeviceMidiPacket packet = {header(ESP_USB_DEVICE_MIDI_CIN_PROGRAM_CHANGE, cable), status(ESP_USB_DEVICE_MIDI_CIN_PROGRAM_CHANGE, channel), program, 0};
  return writePacket(packet);
}

bool EspUsbDeviceMidi::polyPressure(uint8_t channel, uint8_t note, uint8_t pressure, uint8_t cable)
{
  if (cable >= MAX_CABLES || channel > 15 || note > 127 || pressure > 127)
  {
    return rejectMessage();
  }
  EspUsbDeviceMidiPacket packet = {header(ESP_USB_DEVICE_MIDI_CIN_POLY_KEYPRESS, cable), status(ESP_USB_DEVICE_MIDI_CIN_POLY_KEYPRESS, channel), note, pressure};
  return writePacket(packet);
}

bool EspUsbDeviceMidi::channelPressure(uint8_t channel, uint8_t pressure, uint8_t cable)
{
  if (cable >= MAX_CABLES || channel > 15 || pressure > 127)
  {
    return rejectMessage();
  }
  EspUsbDeviceMidiPacket packet = {header(ESP_USB_DEVICE_MIDI_CIN_CHANNEL_PRESSURE, cable), status(ESP_USB_DEVICE_MIDI_CIN_CHANNEL_PRESSURE, channel), pressure, 0};
  return writePacket(packet);
}

bool EspUsbDeviceMidi::pitchBend(uint8_t channel, uint16_t value, uint8_t cable)
{
  if (cable >= MAX_CABLES || channel > 15 || value > 16383)
  {
    return rejectMessage();
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
  return static_cast<uint8_t>((codeIndex << 4) | channel);
}

bool EspUsbDeviceMidi::rejectMessage()
{
  // A channel is 4 bits and a data byte 7 bits (MIDI 1.0, USB MIDI 1.0 4).
  // Clamping would send a different message, so nothing is sent.
  device_.setLastError(ESP_ERR_INVALID_ARG);
  return false;
}

