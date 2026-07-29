#include "EspUsbAudioModel.h"

namespace espusb {
namespace internal {

AudioBusLimits defaultAudioBusLimits(UsbSpeed speed)
{
  AudioBusLimits limits;
  limits.maxIsoPacketSize = speed == UsbSpeed::High ? 1024 : 1023;
  limits.softwareBufferSize = speed == UsbSpeed::High ? 1600 : 1023;
  return limits;
}

AudioFormatError validateAudioFormat(const AudioPcmFormat &format,
                                     UsbSpeed speed,
                                     const AudioBusLimits &limits,
                                     AudioPacketRequirement *requirement)
{
  if (format.sampleRate == 0)
  {
    return AudioFormatError::InvalidSampleRate;
  }
  if (format.channels == 0 || format.channels > 2)
  {
    return AudioFormatError::InvalidChannelCount;
  }
  if (format.subslotBytes < 2 || format.subslotBytes > 4)
  {
    return AudioFormatError::InvalidSubslotSize;
  }
  if ((format.validBits != 16 && format.validBits != 24 &&
       format.validBits != 32) ||
      format.validBits > format.subslotBytes * 8)
  {
    return AudioFormatError::InvalidBitResolution;
  }

  const uint32_t sampleFrameBytes =
      static_cast<uint32_t>(format.channels) * format.subslotBytes;
  const uint32_t framesPerSecond =
      speed == UsbSpeed::High ? 8000U : 1000U;
  const uint64_t bytesPerSecond =
      static_cast<uint64_t>(format.sampleRate) * sampleFrameBytes;
  const uint64_t packet =
      ((bytesPerSecond + framesPerSecond - 1) / framesPerSecond) +
      static_cast<uint64_t>(limits.clockToleranceSampleFrames) *
          sampleFrameBytes;
  if (bytesPerSecond > UINT32_MAX || packet > UINT16_MAX)
  {
    return AudioFormatError::PacketTooLarge;
  }

  AudioPacketRequirement calculated;
  calculated.bytesPerSecond = static_cast<uint32_t>(bytesPerSecond);
  calculated.maxPacketSize = static_cast<uint16_t>(packet);
  calculated.framesPerSecond = static_cast<uint16_t>(framesPerSecond);
  if (requirement)
  {
    *requirement = calculated;
  }
  if (calculated.maxPacketSize > limits.maxIsoPacketSize)
  {
    return AudioFormatError::PacketTooLarge;
  }
  if (calculated.maxPacketSize > limits.softwareBufferSize)
  {
    return AudioFormatError::BufferTooSmall;
  }
  return AudioFormatError::None;
}

AudioStreamModel::AudioStreamModel(AudioDirection direction)
    : direction_(direction)
{
}

bool AudioStreamModel::addFormat(const AudioPcmFormat &format, UsbSpeed speed,
                                 const AudioBusLimits &limits)
{
  error_ = validateAudioFormat(format, speed, limits);
  if (error_ != AudioFormatError::None)
  {
    return false;
  }
  for (size_t i = 0; i < formatCount_; ++i)
  {
    const AudioPcmFormat &existing = formats_[i];
    if (existing.sampleRate == format.sampleRate &&
        existing.channels == format.channels &&
        existing.subslotBytes == format.subslotBytes &&
        existing.validBits == format.validBits)
    {
      error_ = AudioFormatError::DuplicateFormat;
      return false;
    }
  }
  if (formatCount_ == MAX_FORMATS)
  {
    error_ = AudioFormatError::FormatCapacity;
    return false;
  }
  formats_[formatCount_++] = format;
  return true;
}

const AudioPcmFormat *AudioStreamModel::format(size_t index) const
{
  return index < formatCount_ ? &formats_[index] : nullptr;
}

AudioFunctionModel::AudioFunctionModel(AudioProtocol protocol)
    : protocol_(protocol)
{
}

namespace {

AudioEntity *addEntity(AudioFunctionGraph &graph, AudioEntityKind kind,
                       AudioDirection direction)
{
  if (graph.entityCount == AudioFunctionGraph::MAX_ENTITIES)
  {
    graph.error = AudioGraphError::EntityCapacity;
    return nullptr;
  }
  AudioEntity &entity = graph.entities[graph.entityCount++];
  entity.id = static_cast<uint8_t>(graph.entityCount);
  entity.kind = kind;
  entity.direction = direction;
  return &entity;
}

bool allocateStreamInterface(DescriptorLayout &layout,
                             AudioStreamLayout &stream)
{
  stream.interfaceNumber = layout.allocateInterfaces();
  if (!layout.ok())
  {
    return false;
  }
  stream.present = true;
  return true;
}

} // namespace

bool buildAudioFunctionGraph(const AudioFunctionModel &function,
                             DescriptorLayout &layout,
                             const AudioFunctionGraphConfig &config,
                             AudioFunctionGraph &graph)
{
  graph = AudioFunctionGraph{};
  graph.protocol = function.protocol();
  graph.playback.direction = AudioDirection::Playback;
  graph.capture.direction = AudioDirection::Capture;

  const bool hasPlayback = function.playback().formatCount() != 0;
  const bool hasCapture = function.capture().formatCount() != 0;
  if (!hasPlayback && !hasCapture)
  {
    graph.error = AudioGraphError::NoStreams;
    return false;
  }
  const auto channelsAreConsistent = [](const AudioStreamModel &stream) {
    const AudioPcmFormat *first = stream.format(0);
    if (!first)
    {
      return true;
    }
    for (size_t i = 1; i < stream.formatCount(); ++i)
    {
      if (stream.format(i)->channels != first->channels)
      {
        return false;
      }
    }
    return true;
  };
  if (!channelsAreConsistent(function.playback()) ||
      !channelsAreConsistent(function.capture()))
  {
    graph.error = AudioGraphError::InconsistentChannels;
    return false;
  }
  if (hasPlayback && hasCapture)
  {
    const auto containsRate = [](const AudioStreamModel &stream,
                                 uint32_t rate) {
      for (size_t i = 0; i < stream.formatCount(); ++i)
      {
        if (stream.format(i)->sampleRate == rate)
        {
          return true;
        }
      }
      return false;
    };
    for (size_t i = 0; i < function.playback().formatCount(); ++i)
    {
      if (!containsRate(function.capture(),
                        function.playback().format(i)->sampleRate))
      {
        graph.error = AudioGraphError::IncompatibleClockRates;
        return false;
      }
    }
    for (size_t i = 0; i < function.capture().formatCount(); ++i)
    {
      if (!containsRate(function.playback(),
                        function.capture().format(i)->sampleRate))
      {
        graph.error = AudioGraphError::IncompatibleClockRates;
        return false;
      }
    }
  }

  graph.controlInterface = layout.allocateInterfaces();
  if (!layout.ok())
  {
    graph.error = AudioGraphError::LayoutAllocation;
    return false;
  }

  AudioEntity *clock =
      addEntity(graph, AudioEntityKind::ClockSource, AudioDirection::Playback);
  if (!clock)
  {
    return false;
  }
  graph.clockSourceId = clock->id;

  if (hasPlayback)
  {
    if (!allocateStreamInterface(layout, graph.playback))
    {
      graph.error = AudioGraphError::LayoutAllocation;
      return false;
    }
    graph.playback.dataEndpoint =
        layout.allocateEndpoint(EndpointDirection::Out);
    if (config.playbackFeedback)
    {
      graph.playback.feedbackEndpoint =
          layout.allocateEndpoint(EndpointDirection::In);
    }
    if (!layout.ok())
    {
      graph.error = AudioGraphError::LayoutAllocation;
      return false;
    }

    AudioEntity *usb = addEntity(
        graph, AudioEntityKind::UsbStreamingTerminal, AudioDirection::Playback);
    if (!usb)
    {
      return false;
    }
    usb->clockSourceId = graph.clockSourceId;
    usb->channels = function.playback().format(0)->channels;
    graph.playback.terminalLink = usb->id;

    uint8_t sourceId = usb->id;
    if (config.playbackMute || config.playbackVolume)
    {
      AudioEntity *feature = addEntity(
          graph, AudioEntityKind::FeatureUnit, AudioDirection::Playback);
      if (!feature)
      {
        return false;
      }
      feature->sourceId = sourceId;
      feature->channels = usb->channels;
      feature->muteControl = config.playbackMute;
      feature->volumeControl = config.playbackVolume;
      sourceId = feature->id;
    }

    AudioEntity *speaker = addEntity(
        graph, AudioEntityKind::PhysicalTerminal, AudioDirection::Playback);
    if (!speaker)
    {
      return false;
    }
    speaker->sourceId = sourceId;
    speaker->clockSourceId = graph.clockSourceId;
    speaker->channels = usb->channels;
  }

  if (hasCapture)
  {
    if (!allocateStreamInterface(layout, graph.capture))
    {
      graph.error = AudioGraphError::LayoutAllocation;
      return false;
    }
    graph.capture.dataEndpoint =
        layout.allocateEndpoint(EndpointDirection::In);
    if (!layout.ok())
    {
      graph.error = AudioGraphError::LayoutAllocation;
      return false;
    }

    AudioEntity *microphone = addEntity(
        graph, AudioEntityKind::PhysicalTerminal, AudioDirection::Capture);
    if (!microphone)
    {
      return false;
    }
    microphone->clockSourceId = graph.clockSourceId;
    microphone->channels = function.capture().format(0)->channels;

    uint8_t sourceId = microphone->id;
    if (config.captureMute || config.captureVolume)
    {
      AudioEntity *feature = addEntity(
          graph, AudioEntityKind::FeatureUnit, AudioDirection::Capture);
      if (!feature)
      {
        return false;
      }
      feature->sourceId = sourceId;
      feature->channels = microphone->channels;
      feature->muteControl = config.captureMute;
      feature->volumeControl = config.captureVolume;
      sourceId = feature->id;
    }

    AudioEntity *usb = addEntity(
        graph, AudioEntityKind::UsbStreamingTerminal, AudioDirection::Capture);
    if (!usb)
    {
      return false;
    }
    usb->sourceId = sourceId;
    usb->clockSourceId = graph.clockSourceId;
    usb->channels = microphone->channels;
    graph.capture.terminalLink = usb->id;
  }

  return true;
}

} // namespace internal
} // namespace espusb
