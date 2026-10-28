#include "EspUsbAudioControl.h"

namespace espusb {
namespace internal {

namespace {

constexpr int32_t VOLUME_MIN = -90 * 256;
constexpr int32_t VOLUME_MAX = 0;
constexpr int32_t VOLUME_RESOLUTION = 256;

} // namespace

void AudioControlState::reset()
{
  configured_ = false;
  clockValid_ = true;
  clockEntityId_ = 0;
  currentSampleRate_ = 0;
  sampleRateCount_ = 0;
  featureCount_ = 0;
  rateSource_ = nullptr;
  for (size_t i = 0; i < 2; ++i)
  {
    features_[i] = FeatureState{};
  }
  error_ = AudioControlError::NotConfigured;
}

bool AudioControlState::configure(const AudioFunctionModel &function,
                                  const AudioFunctionGraph &graph)
{
  reset();
  if (graph.error != AudioGraphError::None || graph.clockSourceId == 0)
  {
    setError(AudioControlError::UnknownEntity);
    return false;
  }
  clockEntityId_ = graph.clockSourceId;

  // A valid graph has identical rate sets in both directions. Borrow one
  // stream rather than storing another fixed-capacity copy of its rates.
  rateSource_ = function.playback().formatCount() ? &function.playback() : &function.capture();
  for (size_t i = 0; i < rateSource_->formatCount(); ++i) {
    bool duplicate = false;
    for (size_t j = 0; j < i; ++j)
      if (rateSource_->format(j)->sampleRate == rateSource_->format(i)->sampleRate) { duplicate = true; break; }
    if (!duplicate) ++sampleRateCount_;
  }
  if (sampleRateCount_ == 0)
  {
    setError(AudioControlError::InvalidValue);
    return false;
  }
  currentSampleRate_ = sampleRate(0);

  for (size_t i = 0; i < graph.entityCount; ++i)
  {
    const AudioEntity &entity = graph.entities[i];
    if (entity.kind != AudioEntityKind::FeatureUnit)
    {
      continue;
    }
    if (featureCount_ == 2)
    {
      setError(AudioControlError::UnknownEntity);
      return false;
    }
    FeatureState &state = features_[featureCount_++];
    state.entityId = entity.id;
    state.direction = entity.direction;
    state.channels = entity.channels;
    state.muteSupported = entity.muteControl;
    state.volumeSupported = entity.volumeControl;
  }
  configured_ = true;
  setError(AudioControlError::None);
  return true;
}

AudioControlState::FeatureState *AudioControlState::feature(uint8_t entityId)
{
  for (size_t i = 0; i < featureCount_; ++i)
  {
    if (features_[i].entityId == entityId)
    {
      return &features_[i];
    }
  }
  return nullptr;
}

const AudioControlState::FeatureState *
AudioControlState::feature(uint8_t entityId) const
{
  for (size_t i = 0; i < featureCount_; ++i)
  {
    if (features_[i].entityId == entityId)
    {
      return &features_[i];
    }
  }
  return nullptr;
}

bool AudioControlState::sampleRateSupported(uint32_t rate) const
{
  for (size_t i = 0; rateSource_ && i < rateSource_->formatCount(); ++i)
    if (rateSource_->format(i)->sampleRate == rate) return true;
  return false;
}

bool AudioControlState::validateMasterChannel(uint8_t channel) const
{
  if (channel != 0)
  {
    setError(AudioControlError::InvalidChannel);
    return false;
  }
  return true;
}

bool AudioControlState::validateFeatureChannel(
    const FeatureState &state, uint8_t channel) const
{
  if (channel > state.channels || channel >= 3)
  {
    setError(AudioControlError::InvalidChannel);
    return false;
  }
  return true;
}

bool AudioControlState::current(uint8_t entityId,
                                AudioControlSelector selector,
                                uint8_t channel, int32_t &value) const
{
  if (!configured_)
  {
    setError(AudioControlError::NotConfigured);
    return false;
  }
  if (entityId == clockEntityId_)
  {
    if (!validateMasterChannel(channel))
    {
      return false;
    }
    if (selector == AudioControlSelector::SampleRate)
    {
      value = static_cast<int32_t>(currentSampleRate_);
    }
    else if (selector == AudioControlSelector::ClockValid)
    {
      value = clockValid_ ? 1 : 0;
    }
    else
    {
      setError(AudioControlError::UnsupportedControl);
      return false;
    }
    setError(AudioControlError::None);
    return true;
  }

  const FeatureState *state = feature(entityId);
  if (!state)
  {
    setError(AudioControlError::UnknownEntity);
    return false;
  }
  if (!validateFeatureChannel(*state, channel))
  {
    return false;
  }
  if (selector == AudioControlSelector::Mute && state->muteSupported)
  {
    value = state->muted[channel] ? 1 : 0;
  }
  else if (selector == AudioControlSelector::Volume &&
           state->volumeSupported)
  {
    value = state->volume[channel];
  }
  else
  {
    setError(AudioControlError::UnsupportedControl);
    return false;
  }
  setError(AudioControlError::None);
  return true;
}

bool AudioControlState::setCurrent(uint8_t entityId,
                                   AudioControlSelector selector,
                                   uint8_t channel, int32_t value)
{
  if (!configured_)
  {
    setError(AudioControlError::NotConfigured);
    return false;
  }
  if (entityId == clockEntityId_)
  {
    if (!validateMasterChannel(channel))
    {
      return false;
    }
    if (selector == AudioControlSelector::ClockValid)
    {
      setError(AudioControlError::ReadOnly);
      return false;
    }
    if (selector != AudioControlSelector::SampleRate)
    {
      setError(AudioControlError::UnsupportedControl);
      return false;
    }
    if (value <= 0 || !sampleRateSupported(static_cast<uint32_t>(value)))
    {
      setError(AudioControlError::InvalidValue);
      return false;
    }
    currentSampleRate_ = static_cast<uint32_t>(value);
    setError(AudioControlError::None);
    return true;
  }

  FeatureState *state = feature(entityId);
  if (!state)
  {
    setError(AudioControlError::UnknownEntity);
    return false;
  }
  if (!validateFeatureChannel(*state, channel))
  {
    return false;
  }
  if (selector == AudioControlSelector::Mute && state->muteSupported)
  {
    if (value != 0 && value != 1)
    {
      setError(AudioControlError::InvalidValue);
      return false;
    }
    state->muted[channel] = value != 0;
  }
  else if (selector == AudioControlSelector::Volume &&
           state->volumeSupported)
  {
    if (value < VOLUME_MIN || value > VOLUME_MAX ||
        value % VOLUME_RESOLUTION != 0)
    {
      setError(AudioControlError::InvalidValue);
      return false;
    }
    state->volume[channel] = static_cast<int16_t>(value);
  }
  else
  {
    setError(AudioControlError::UnsupportedControl);
    return false;
  }
  setError(AudioControlError::None);
  return true;
}

bool AudioControlState::range(uint8_t entityId,
                              AudioControlSelector selector,
                              uint8_t channel, AudioControlRange &value) const
{
  if (!configured_)
  {
    setError(AudioControlError::NotConfigured);
    return false;
  }
  if (entityId == clockEntityId_)
  {
    if (!validateMasterChannel(channel))
    {
      return false;
    }
    if (selector != AudioControlSelector::SampleRate)
    {
      setError(AudioControlError::UnsupportedControl);
      return false;
    }
    // Discrete clock rates are exposed through sampleRateCount()/sampleRate().
    // A single range here represents only the current fixed-rate case.
    if (sampleRateCount_ != 1)
    {
      setError(AudioControlError::UnsupportedControl);
      return false;
    }
    value.minimum = static_cast<int32_t>(sampleRate(0));
    value.maximum = static_cast<int32_t>(sampleRate(0));
    value.resolution = 0;
    setError(AudioControlError::None);
    return true;
  }

  const FeatureState *state = feature(entityId);
  if (!state)
  {
    setError(AudioControlError::UnknownEntity);
    return false;
  }
  if (!validateFeatureChannel(*state, channel))
  {
    return false;
  }
  if (selector != AudioControlSelector::Volume || !state->volumeSupported)
  {
    setError(AudioControlError::UnsupportedControl);
    return false;
  }
  value.minimum = VOLUME_MIN;
  value.maximum = VOLUME_MAX;
  value.resolution = VOLUME_RESOLUTION;
  setError(AudioControlError::None);
  return true;
}

uint32_t AudioControlState::sampleRate(size_t index) const
{
  if (!rateSource_ || index >= sampleRateCount_) return 0;
  for (size_t i = 0; i < rateSource_->formatCount(); ++i) {
    const uint32_t rate = rateSource_->format(i)->sampleRate;
    bool duplicate = false;
    for (size_t j = 0; j < i; ++j)
      if (rateSource_->format(j)->sampleRate == rate) { duplicate = true; break; }
    if (!duplicate && !index--) return rate;
  }
  return 0;
}

AudioControlEntityKind
AudioControlState::entityKind(uint8_t entityId) const
{
  if (configured_ && entityId == clockEntityId_)
  {
    return AudioControlEntityKind::Clock;
  }
  for (size_t i = 0; configured_ && i < featureCount_; ++i)
  {
    if (features_[i].entityId == entityId)
    {
      return AudioControlEntityKind::Feature;
    }
  }
  return AudioControlEntityKind::Unknown;
}

void AudioControlState::setError(AudioControlError error) const
{
  error_ = error;
}

} // namespace internal
} // namespace espusb
