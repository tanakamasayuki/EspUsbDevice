#include "EspUsbAudioDescriptor.h"

namespace espusb {
namespace internal {

namespace {

constexpr uint8_t DESC_INTERFACE_ASSOCIATION = 0x0b;
constexpr uint8_t DESC_CS_INTERFACE = 0x24;
constexpr uint8_t DESC_CS_ENDPOINT = 0x25;
constexpr uint8_t USB_CLASS_AUDIO = 0x01;
constexpr uint8_t AUDIO_SUBCLASS_CONTROL = 0x01;
constexpr uint8_t AUDIO_SUBCLASS_STREAMING = 0x02;
constexpr uint8_t AUDIO_PROTOCOL_UAC1 = 0x00;
constexpr uint8_t AUDIO_PROTOCOL_UAC2 = 0x20;

constexpr uint8_t AC_HEADER = 0x01;
constexpr uint8_t AC_INPUT_TERMINAL = 0x02;
constexpr uint8_t AC_OUTPUT_TERMINAL = 0x03;
constexpr uint8_t AC_FEATURE_UNIT = 0x06;
constexpr uint8_t AC_CLOCK_SOURCE = 0x0a;
constexpr uint8_t AS_GENERAL = 0x01;
constexpr uint8_t AS_FORMAT_TYPE = 0x02;
constexpr uint8_t EP_GENERAL = 0x01;

constexpr uint16_t TERMINAL_USB_STREAMING = 0x0101;
constexpr uint16_t TERMINAL_MICROPHONE = 0x0201;
constexpr uint16_t TERMINAL_SPEAKER = 0x0301;

constexpr uint8_t FUNCTION_SPEAKER = 0x01;
constexpr uint8_t FUNCTION_MICROPHONE = 0x03;
constexpr uint8_t FUNCTION_HEADSET = 0x04;

bool writeU32(DescriptorBuffer &buffer, uint32_t value)
{
  return buffer.writeU16(static_cast<uint16_t>(value & 0xffffU)) &&
         buffer.writeU16(static_cast<uint16_t>(value >> 16));
}

uint32_t channelConfig(uint8_t channels)
{
  return channels == 1 ? 0x00000004U : 0x00000003U;
}

uint8_t functionCategory(const AudioFunctionGraph &graph)
{
  if (graph.playback.present && graph.capture.present)
  {
    return FUNCTION_HEADSET;
  }
  return graph.playback.present ? FUNCTION_SPEAKER : FUNCTION_MICROPHONE;
}

uint8_t streamString(const AudioDescriptorConfig &config,
                     AudioDirection direction)
{
  return direction == AudioDirection::Playback
             ? config.playbackStringIndex
             : config.captureStringIndex;
}

bool writeInputTerminal(DescriptorBuffer &buffer, const AudioEntity &entity)
{
  const bool usb = entity.kind == AudioEntityKind::UsbStreamingTerminal;
  return buffer.writeU8(17) && buffer.writeU8(DESC_CS_INTERFACE) &&
         buffer.writeU8(AC_INPUT_TERMINAL) && buffer.writeU8(entity.id) &&
         buffer.writeU16(usb ? TERMINAL_USB_STREAMING : TERMINAL_MICROPHONE) &&
         buffer.writeU8(0) && buffer.writeU8(entity.clockSourceId) &&
         buffer.writeU8(entity.channels) &&
         writeU32(buffer, channelConfig(entity.channels)) &&
         buffer.writeU8(0) && buffer.writeU16(0) && buffer.writeU8(0);
}

bool writeOutputTerminal(DescriptorBuffer &buffer, const AudioEntity &entity)
{
  const bool usb = entity.kind == AudioEntityKind::UsbStreamingTerminal;
  return buffer.writeU8(12) && buffer.writeU8(DESC_CS_INTERFACE) &&
         buffer.writeU8(AC_OUTPUT_TERMINAL) && buffer.writeU8(entity.id) &&
         buffer.writeU16(usb ? TERMINAL_USB_STREAMING : TERMINAL_SPEAKER) &&
         buffer.writeU8(0) && buffer.writeU8(entity.sourceId) &&
         buffer.writeU8(entity.clockSourceId) &&
         buffer.writeU16(0) && buffer.writeU8(0);
}

bool writeFeatureUnit(DescriptorBuffer &buffer, const AudioEntity &entity)
{
  const uint8_t length =
      static_cast<uint8_t>(6U + (static_cast<uint16_t>(entity.channels) + 1U) * 4U);
  uint32_t masterControls = 0;
  if (entity.muteControl)
  {
    masterControls |= 0x03U;
  }
  if (entity.volumeControl)
  {
    masterControls |= 0x0cU;
  }
  if (!(buffer.writeU8(length) && buffer.writeU8(DESC_CS_INTERFACE) &&
        buffer.writeU8(AC_FEATURE_UNIT) && buffer.writeU8(entity.id) &&
        buffer.writeU8(entity.sourceId) && writeU32(buffer, masterControls)))
  {
    return false;
  }
  for (uint8_t channel = 0; channel < entity.channels; ++channel)
  {
    if (!writeU32(buffer, 0))
    {
      return false;
    }
  }
  return buffer.writeU8(0);
}

bool writeEntity(DescriptorBuffer &buffer, const AudioEntity &entity)
{
  switch (entity.kind)
  {
  case AudioEntityKind::ClockSource:
    // Internal programmable clock; sample frequency is RW and validity is R.
    return buffer.writeU8(8) && buffer.writeU8(DESC_CS_INTERFACE) &&
           buffer.writeU8(AC_CLOCK_SOURCE) && buffer.writeU8(entity.id) &&
           buffer.writeU8(0x03) && buffer.writeU8(0x07) &&
           buffer.writeU8(0) && buffer.writeU8(0);
  case AudioEntityKind::FeatureUnit:
    return writeFeatureUnit(buffer, entity);
  case AudioEntityKind::UsbStreamingTerminal:
    return entity.direction == AudioDirection::Playback
               ? writeInputTerminal(buffer, entity)
               : writeOutputTerminal(buffer, entity);
  case AudioEntityKind::PhysicalTerminal:
    return entity.direction == AudioDirection::Capture
               ? writeInputTerminal(buffer, entity)
               : writeOutputTerminal(buffer, entity);
  }
  return false;
}

bool writeStreamingInterface(DescriptorBuildContext &context,
                             const AudioStreamModel &model,
                             const AudioStreamLayout &layout,
                             const AudioDescriptorConfig &config)
{
  const AudioPcmFormat *format = model.format(0);
  AudioPacketRequirement packet;
  if (!format ||
      validateAudioFormat(*format, context.speed(),
                          defaultAudioBusLimits(context.speed()), &packet) !=
          AudioFormatError::None)
  {
    return false;
  }

  const uint8_t endpointCount =
      static_cast<uint8_t>(1U + (layout.feedbackEndpoint ? 1U : 0U));
  if (!context.writeInterface(layout.interfaceNumber, 0, 0, USB_CLASS_AUDIO,
                              AUDIO_SUBCLASS_STREAMING, AUDIO_PROTOCOL_UAC2,
                              streamString(config, layout.direction)) ||
      !context.writeInterface(layout.interfaceNumber, 1, endpointCount,
                              USB_CLASS_AUDIO, AUDIO_SUBCLASS_STREAMING,
                              AUDIO_PROTOCOL_UAC2,
                              streamString(config, layout.direction)))
  {
    return false;
  }

  DescriptorBuffer &buffer = context.buffer();
  if (!(buffer.writeU8(16) && buffer.writeU8(DESC_CS_INTERFACE) &&
        buffer.writeU8(AS_GENERAL) && buffer.writeU8(layout.terminalLink) &&
        buffer.writeU8(0) && buffer.writeU8(0x01) &&
        writeU32(buffer, 0x00000001U) &&
        buffer.writeU8(format->channels) &&
        writeU32(buffer, channelConfig(format->channels)) &&
        buffer.writeU8(0) &&
        buffer.writeU8(6) && buffer.writeU8(DESC_CS_INTERFACE) &&
        buffer.writeU8(AS_FORMAT_TYPE) && buffer.writeU8(0x01) &&
        buffer.writeU8(format->subslotBytes) &&
        buffer.writeU8(format->validBits)))
  {
    return false;
  }

  if (!context.writeEndpoint(layout.dataEndpoint, 0x05,
                             packet.maxPacketSize, packet.maxPacketSize, 1, 1) ||
      !(buffer.writeU8(8) && buffer.writeU8(DESC_CS_ENDPOINT) &&
        buffer.writeU8(EP_GENERAL) && buffer.writeU8(0) &&
        buffer.writeU8(0) && buffer.writeU8(0) &&
        buffer.writeU16(0)))
  {
    return false;
  }

  if (layout.feedbackEndpoint &&
      !context.writeEndpoint(layout.feedbackEndpoint, 0x11, 4, 4, 1, 4))
  {
    return false;
  }
  return true;
}

bool writeUac1InputTerminal(DescriptorBuffer &buffer,
                            const AudioEntity &entity)
{
  const bool usb = entity.kind == AudioEntityKind::UsbStreamingTerminal;
  return buffer.writeU8(12) && buffer.writeU8(DESC_CS_INTERFACE) &&
         buffer.writeU8(AC_INPUT_TERMINAL) && buffer.writeU8(entity.id) &&
         buffer.writeU16(usb ? TERMINAL_USB_STREAMING : TERMINAL_MICROPHONE) &&
         buffer.writeU8(0) && buffer.writeU8(entity.channels) &&
         buffer.writeU16(static_cast<uint16_t>(channelConfig(entity.channels))) &&
         buffer.writeU8(0) && buffer.writeU8(0);
}

bool writeUac1OutputTerminal(DescriptorBuffer &buffer,
                             const AudioEntity &entity)
{
  const bool usb = entity.kind == AudioEntityKind::UsbStreamingTerminal;
  return buffer.writeU8(9) && buffer.writeU8(DESC_CS_INTERFACE) &&
         buffer.writeU8(AC_OUTPUT_TERMINAL) && buffer.writeU8(entity.id) &&
         buffer.writeU16(usb ? TERMINAL_USB_STREAMING : TERMINAL_SPEAKER) &&
         buffer.writeU8(0) && buffer.writeU8(entity.sourceId) &&
         buffer.writeU8(0);
}

bool writeUac1FeatureUnit(DescriptorBuffer &buffer,
                          const AudioEntity &entity)
{
  const uint8_t length =
      static_cast<uint8_t>(7U + static_cast<uint16_t>(entity.channels) + 1U);
  uint8_t controls = 0;
  if (entity.muteControl)
  {
    controls |= 0x01;
  }
  if (entity.volumeControl)
  {
    controls |= 0x02;
  }
  if (!(buffer.writeU8(length) && buffer.writeU8(DESC_CS_INTERFACE) &&
        buffer.writeU8(AC_FEATURE_UNIT) && buffer.writeU8(entity.id) &&
        buffer.writeU8(entity.sourceId) && buffer.writeU8(1) &&
        buffer.writeU8(controls)))
  {
    return false;
  }
  for (uint8_t channel = 0; channel < entity.channels; ++channel)
  {
    if (!buffer.writeU8(0))
    {
      return false;
    }
  }
  return buffer.writeU8(0);
}

bool writeUac1Entity(DescriptorBuffer &buffer, const AudioEntity &entity)
{
  switch (entity.kind)
  {
  case AudioEntityKind::ClockSource:
    return true;
  case AudioEntityKind::FeatureUnit:
    return writeUac1FeatureUnit(buffer, entity);
  case AudioEntityKind::UsbStreamingTerminal:
    return entity.direction == AudioDirection::Playback
               ? writeUac1InputTerminal(buffer, entity)
               : writeUac1OutputTerminal(buffer, entity);
  case AudioEntityKind::PhysicalTerminal:
    return entity.direction == AudioDirection::Capture
               ? writeUac1InputTerminal(buffer, entity)
               : writeUac1OutputTerminal(buffer, entity);
  }
  return false;
}

bool writeUac1StreamingInterface(DescriptorBuildContext &context,
                                 const AudioStreamModel &model,
                                 const AudioStreamLayout &layout,
                                 const AudioDescriptorConfig &config)
{
  const AudioPcmFormat *format = model.format(0);
  AudioPacketRequirement packet;
  if (!format ||
      validateAudioFormat(*format, context.speed(),
                          defaultAudioBusLimits(context.speed()), &packet) !=
          AudioFormatError::None)
  {
    return false;
  }

  const uint8_t endpointCount =
      static_cast<uint8_t>(1U + (layout.feedbackEndpoint ? 1U : 0U));
  if (!context.writeInterface(layout.interfaceNumber, 0, 0, USB_CLASS_AUDIO,
                              AUDIO_SUBCLASS_STREAMING, AUDIO_PROTOCOL_UAC1,
                              streamString(config, layout.direction)) ||
      !context.writeInterface(layout.interfaceNumber, 1, endpointCount,
                              USB_CLASS_AUDIO, AUDIO_SUBCLASS_STREAMING,
                              AUDIO_PROTOCOL_UAC1,
                              streamString(config, layout.direction)))
  {
    return false;
  }

  DescriptorBuffer &buffer = context.buffer();
  const uint32_t rate = format->sampleRate;
  if (!(buffer.writeU8(7) && buffer.writeU8(DESC_CS_INTERFACE) &&
        buffer.writeU8(AS_GENERAL) && buffer.writeU8(layout.terminalLink) &&
        buffer.writeU8(1) && buffer.writeU16(0x0001) &&
        buffer.writeU8(11) && buffer.writeU8(DESC_CS_INTERFACE) &&
        buffer.writeU8(AS_FORMAT_TYPE) && buffer.writeU8(0x01) &&
        buffer.writeU8(format->channels) &&
        buffer.writeU8(format->subslotBytes) &&
        buffer.writeU8(format->validBits) && buffer.writeU8(1) &&
        buffer.writeU8(static_cast<uint8_t>(rate & 0xffU)) &&
        buffer.writeU8(static_cast<uint8_t>((rate >> 8) & 0xffU)) &&
        buffer.writeU8(static_cast<uint8_t>((rate >> 16) & 0xffU))))
  {
    return false;
  }

  const uint16_t maxPacket = packet.maxPacketSize;
  const uint8_t dataAttributes =
      layout.direction == AudioDirection::Playback ? 0x09 : 0x05;
  if (!(buffer.writeU8(9) && buffer.writeU8(0x05) &&
        buffer.writeU8(layout.dataEndpoint) &&
        buffer.writeU8(dataAttributes) &&
        buffer.writeU16(maxPacket) && buffer.writeU8(1) &&
        buffer.writeU8(0) &&
        buffer.writeU8(layout.feedbackEndpoint) &&
        buffer.writeU8(7) && buffer.writeU8(DESC_CS_ENDPOINT) &&
        buffer.writeU8(EP_GENERAL) && buffer.writeU8(0x01) &&
        buffer.writeU8(0) && buffer.writeU16(0)))
  {
    return false;
  }

  if (layout.feedbackEndpoint &&
      !(buffer.writeU8(9) && buffer.writeU8(0x05) &&
        buffer.writeU8(layout.feedbackEndpoint) && buffer.writeU8(0x01) &&
        buffer.writeU16(3) && buffer.writeU8(1) &&
        buffer.writeU8(1) && buffer.writeU8(0)))
  {
    return false;
  }
  return true;
}

void setError(AudioDescriptorError *error, AudioDescriptorError value)
{
  if (error)
  {
    *error = value;
  }
}

} // namespace

bool writeUac1Function(DescriptorBuildContext &context,
                       const AudioFunctionModel &function,
                       const AudioFunctionGraph &graph,
                       const AudioDescriptorConfig &config,
                       AudioDescriptorError *error)
{
  setError(error, AudioDescriptorError::None);
  if (graph.error != AudioGraphError::None ||
      (!graph.playback.present && !graph.capture.present))
  {
    setError(error, AudioDescriptorError::InvalidGraph);
    return false;
  }
  if (function.protocol() != AudioProtocol::Uac1 ||
      graph.protocol != AudioProtocol::Uac1)
  {
    setError(error, AudioDescriptorError::UnsupportedProtocol);
    return false;
  }
  if ((graph.playback.present && function.playback().formatCount() != 1) ||
      (graph.capture.present && function.capture().formatCount() != 1))
  {
    setError(error, AudioDescriptorError::UnsupportedFormatCount);
    return false;
  }

  DescriptorBuffer &buffer = context.buffer();
  const uint8_t streamCount =
      static_cast<uint8_t>((graph.playback.present ? 1U : 0U) +
                           (graph.capture.present ? 1U : 0U));
  const uint8_t iad[] = {
      8, DESC_INTERFACE_ASSOCIATION, graph.controlInterface,
      static_cast<uint8_t>(1U + streamCount), USB_CLASS_AUDIO, 0,
      AUDIO_PROTOCOL_UAC1, config.functionStringIndex,
  };
  if (!buffer.append(iad, sizeof(iad)) ||
      !context.writeInterface(graph.controlInterface, 0, 0, USB_CLASS_AUDIO,
                              AUDIO_SUBCLASS_CONTROL, AUDIO_PROTOCOL_UAC1,
                              config.controlStringIndex))
  {
    setError(error, AudioDescriptorError::BufferOverflow);
    return false;
  }

  const size_t acStart = buffer.size();
  if (!(buffer.writeU8(static_cast<uint8_t>(8U + streamCount)) &&
        buffer.writeU8(DESC_CS_INTERFACE) && buffer.writeU8(AC_HEADER) &&
        buffer.writeU16(0x0100) && buffer.writeU16(0) &&
        buffer.writeU8(streamCount)))
  {
    setError(error, AudioDescriptorError::BufferOverflow);
    return false;
  }
  if (graph.playback.present &&
      !buffer.writeU8(graph.playback.interfaceNumber))
  {
    setError(error, AudioDescriptorError::BufferOverflow);
    return false;
  }
  if (graph.capture.present &&
      !buffer.writeU8(graph.capture.interfaceNumber))
  {
    setError(error, AudioDescriptorError::BufferOverflow);
    return false;
  }
  for (size_t i = 0; i < graph.entityCount; ++i)
  {
    if (!writeUac1Entity(buffer, graph.entities[i]))
    {
      setError(error, AudioDescriptorError::BufferOverflow);
      return false;
    }
  }
  const size_t acLength = buffer.size() - acStart;
  if (acLength > 0xffffU ||
      !buffer.patchU16(acStart + 5, static_cast<uint16_t>(acLength)))
  {
    setError(error, AudioDescriptorError::BufferOverflow);
    return false;
  }

  if (graph.playback.present &&
      !writeUac1StreamingInterface(context, function.playback(),
                                   graph.playback, config))
  {
    setError(error, buffer.ok() ? AudioDescriptorError::InvalidFormat
                                : AudioDescriptorError::BufferOverflow);
    return false;
  }
  if (graph.capture.present &&
      !writeUac1StreamingInterface(context, function.capture(),
                                   graph.capture, config))
  {
    setError(error, buffer.ok() ? AudioDescriptorError::InvalidFormat
                                : AudioDescriptorError::BufferOverflow);
    return false;
  }
  return true;
}

bool writeUac2Function(DescriptorBuildContext &context,
                       const AudioFunctionModel &function,
                       const AudioFunctionGraph &graph,
                       const AudioDescriptorConfig &config,
                       AudioDescriptorError *error)
{
  setError(error, AudioDescriptorError::None);
  if (graph.error != AudioGraphError::None ||
      (!graph.playback.present && !graph.capture.present))
  {
    setError(error, AudioDescriptorError::InvalidGraph);
    return false;
  }
  if (function.protocol() != AudioProtocol::Uac2 ||
      graph.protocol != AudioProtocol::Uac2)
  {
    setError(error, AudioDescriptorError::UnsupportedProtocol);
    return false;
  }
  if ((graph.playback.present && function.playback().formatCount() != 1) ||
      (graph.capture.present && function.capture().formatCount() != 1))
  {
    setError(error, AudioDescriptorError::UnsupportedFormatCount);
    return false;
  }

  DescriptorBuffer &buffer = context.buffer();
  const uint8_t streamCount =
      static_cast<uint8_t>((graph.playback.present ? 1U : 0U) +
                           (graph.capture.present ? 1U : 0U));
  const uint8_t iad[] = {
      8, DESC_INTERFACE_ASSOCIATION, graph.controlInterface,
      static_cast<uint8_t>(1U + streamCount), USB_CLASS_AUDIO, 0,
      AUDIO_PROTOCOL_UAC2, config.functionStringIndex,
  };
  if (!buffer.append(iad, sizeof(iad)) ||
      !context.writeInterface(graph.controlInterface, 0, 0, USB_CLASS_AUDIO,
                              AUDIO_SUBCLASS_CONTROL, AUDIO_PROTOCOL_UAC2,
                              config.controlStringIndex))
  {
    setError(error, AudioDescriptorError::BufferOverflow);
    return false;
  }

  const size_t acStart = buffer.size();
  if (!(buffer.writeU8(9) && buffer.writeU8(DESC_CS_INTERFACE) &&
        buffer.writeU8(AC_HEADER) && buffer.writeU16(0x0200) &&
        buffer.writeU8(functionCategory(graph)) &&
        buffer.writeU16(0) && buffer.writeU8(0)))
  {
    setError(error, AudioDescriptorError::BufferOverflow);
    return false;
  }
  for (size_t i = 0; i < graph.entityCount; ++i)
  {
    if (!writeEntity(buffer, graph.entities[i]))
    {
      setError(error, AudioDescriptorError::BufferOverflow);
      return false;
    }
  }
  const size_t acLength = buffer.size() - acStart;
  if (acLength > 0xffffU ||
      !buffer.patchU16(acStart + 6, static_cast<uint16_t>(acLength)))
  {
    setError(error, AudioDescriptorError::BufferOverflow);
    return false;
  }

  if (graph.playback.present &&
      !writeStreamingInterface(context, function.playback(), graph.playback,
                               config))
  {
    setError(error, buffer.ok() ? AudioDescriptorError::InvalidFormat
                                : AudioDescriptorError::BufferOverflow);
    return false;
  }
  if (graph.capture.present &&
      !writeStreamingInterface(context, function.capture(), graph.capture,
                               config))
  {
    setError(error, buffer.ok() ? AudioDescriptorError::InvalidFormat
                                : AudioDescriptorError::BufferOverflow);
    return false;
  }
  return true;
}

} // namespace internal
} // namespace espusb
