#pragma once

#include "EspUsbAudioModel.h"

namespace espusb {
namespace internal {

enum class AudioControlSelector : uint8_t {
  SampleRate,
  ClockValid,
  Mute,
  Volume,
};

enum class AudioControlError : uint8_t {
  None,
  NotConfigured,
  UnknownEntity,
  UnsupportedControl,
  InvalidChannel,
  InvalidValue,
  ReadOnly,
  RateCapacity,
};

enum class AudioControlEntityKind : uint8_t {
  Unknown,
  Clock,
  Feature,
};

struct AudioControlRange {
  int32_t minimum = 0;
  int32_t maximum = 0;
  int32_t resolution = 0;
};

class AudioControlState {
public:
  static constexpr size_t MAX_SAMPLE_RATES = AudioStreamModel::MAX_FORMATS;

  bool configure(const AudioFunctionModel &function,
                 const AudioFunctionGraph &graph);
  void reset();

  bool current(uint8_t entityId, AudioControlSelector selector,
               uint8_t channel, int32_t &value) const;
  bool setCurrent(uint8_t entityId, AudioControlSelector selector,
                  uint8_t channel, int32_t value);
  bool range(uint8_t entityId, AudioControlSelector selector,
             uint8_t channel, AudioControlRange &value) const;

  size_t sampleRateCount() const { return sampleRateCount_; }
  uint32_t sampleRate(size_t index) const;
  uint32_t currentSampleRate() const { return currentSampleRate_; }
  AudioControlEntityKind entityKind(uint8_t entityId) const;
  AudioControlError error() const { return error_; }

private:
  struct FeatureState {
    uint8_t entityId = 0;
    AudioDirection direction = AudioDirection::Playback;
    uint8_t channels = 0;
    bool muteSupported = false;
    bool volumeSupported = false;
    bool muted[3] = {};
    int16_t volume[3] = {};
  };

  FeatureState *feature(uint8_t entityId);
  const FeatureState *feature(uint8_t entityId) const;
  bool sampleRateSupported(uint32_t rate) const;
  bool addSampleRate(uint32_t rate);
  bool validateMasterChannel(uint8_t channel) const;
  bool validateFeatureChannel(const FeatureState &state,
                              uint8_t channel) const;
  void setError(AudioControlError error) const;

  bool configured_ = false;
  bool clockValid_ = true;
  uint8_t clockEntityId_ = 0;
  uint32_t currentSampleRate_ = 0;
  uint32_t sampleRates_[MAX_SAMPLE_RATES] = {};
  size_t sampleRateCount_ = 0;
  FeatureState features_[2] = {};
  size_t featureCount_ = 0;
  mutable AudioControlError error_ = AudioControlError::NotConfigured;
};

} // namespace internal
} // namespace espusb
