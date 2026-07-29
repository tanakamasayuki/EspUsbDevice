#pragma once

#include <stddef.h>
#include <stdint.h>

#include "EspUsbDescriptorModel.h"

namespace espusb {
namespace internal {

enum class AudioProtocol : uint8_t {
  Uac1,
  Uac2,
};

enum class AudioDirection : uint8_t {
  Playback,
  Capture,
};

enum class AudioFormatError : uint8_t {
  None,
  InvalidSampleRate,
  InvalidChannelCount,
  InvalidSubslotSize,
  InvalidBitResolution,
  PacketTooLarge,
  BufferTooSmall,
  FormatCapacity,
  DuplicateFormat,
};

enum class AudioEntityKind : uint8_t {
  ClockSource,
  UsbStreamingTerminal,
  PhysicalTerminal,
  FeatureUnit,
};

enum class AudioGraphError : uint8_t {
  None,
  NoStreams,
  InconsistentChannels,
  IncompatibleClockRates,
  EntityCapacity,
  LayoutAllocation,
};

struct AudioPcmFormat {
  uint32_t sampleRate = 0;
  uint8_t channels = 0;
  uint8_t subslotBytes = 0;
  uint8_t validBits = 0;
};

struct AudioPacketRequirement {
  uint32_t bytesPerSecond = 0;
  uint16_t maxPacketSize = 0;
  uint16_t framesPerSecond = 0;
};

struct AudioBusLimits {
  uint16_t maxIsoPacketSize = 0;
  uint16_t softwareBufferSize = 0;
  uint8_t clockToleranceSampleFrames = 1;
};

AudioBusLimits defaultAudioBusLimits(UsbSpeed speed);
AudioFormatError validateAudioFormat(const AudioPcmFormat &format,
                                     UsbSpeed speed,
                                     const AudioBusLimits &limits,
                                     AudioPacketRequirement *requirement = nullptr);

class AudioStreamModel {
public:
  static constexpr size_t MAX_FORMATS = 8;

  explicit AudioStreamModel(AudioDirection direction);

  bool addFormat(const AudioPcmFormat &format, UsbSpeed speed,
                 const AudioBusLimits &limits);
  AudioDirection direction() const { return direction_; }
  size_t formatCount() const { return formatCount_; }
  const AudioPcmFormat *format(size_t index) const;
  AudioFormatError error() const { return error_; }

private:
  AudioDirection direction_;
  AudioPcmFormat formats_[MAX_FORMATS] = {};
  size_t formatCount_ = 0;
  AudioFormatError error_ = AudioFormatError::None;
};

class AudioFunctionModel {
public:
  explicit AudioFunctionModel(AudioProtocol protocol = AudioProtocol::Uac2);

  AudioStreamModel &playback() { return playback_; }
  const AudioStreamModel &playback() const { return playback_; }
  AudioStreamModel &capture() { return capture_; }
  const AudioStreamModel &capture() const { return capture_; }
  AudioProtocol protocol() const { return protocol_; }

private:
  AudioProtocol protocol_;
  AudioStreamModel playback_{AudioDirection::Playback};
  AudioStreamModel capture_{AudioDirection::Capture};
};

struct AudioEntity {
  uint8_t id = 0;
  AudioEntityKind kind = AudioEntityKind::ClockSource;
  AudioDirection direction = AudioDirection::Playback;
  uint8_t sourceId = 0;
  uint8_t clockSourceId = 0;
  uint8_t channels = 0;
  bool muteControl = false;
  bool volumeControl = false;
};

struct AudioStreamLayout {
  bool present = false;
  AudioDirection direction = AudioDirection::Playback;
  uint8_t interfaceNumber = 0;
  uint8_t terminalLink = 0;
  uint8_t dataEndpoint = 0;
  uint8_t feedbackEndpoint = 0;
};

struct AudioFunctionGraph {
  static constexpr size_t MAX_ENTITIES = 8;

  AudioProtocol protocol = AudioProtocol::Uac2;
  uint8_t controlInterface = 0;
  uint8_t clockSourceId = 0;
  AudioEntity entities[MAX_ENTITIES] = {};
  size_t entityCount = 0;
  AudioStreamLayout playback{};
  AudioStreamLayout capture{false, AudioDirection::Capture};
  AudioGraphError error = AudioGraphError::None;
};

struct AudioFunctionGraphConfig {
  bool playbackMute = true;
  bool playbackVolume = true;
  bool playbackFeedback = true;
  bool captureMute = true;
  bool captureVolume = true;
};

bool buildAudioFunctionGraph(const AudioFunctionModel &function,
                             DescriptorLayout &layout,
                             const AudioFunctionGraphConfig &config,
                             AudioFunctionGraph &graph);

} // namespace internal
} // namespace espusb
