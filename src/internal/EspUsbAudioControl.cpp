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
  for (size_t i = 0; i < MAX_SAMPLE_RATES; ++i)
  {
    sampleRates_[i] = 0;
  }
  for (size_t i = 0; i < 2; ++i)
  {
    features_[i] = FeatureState{};
  }
  error_ = AudioControlError::NotConfigured;
}

bool AudioControlState::addSampleRate(uint32_t rate)
{
  if (sampleRateSupported(rate))
  {
    return true;
  }
  if (sampleRateCount_ == MAX_SAMPLE_RATES)
  {
    setError(AudioControlError::RateCapacity);
    return false;
  }
  sampleRates_[sampleRateCount_++] = rate;
  return true;
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

  const AudioStreamModel *streams[] = {
      &function.playback(),
      &function.capture(),
  };
  for (const AudioStreamModel *stream : streams)
  {
    for (size_t i = 0; i < stream->formatCount(); ++i)
    {
      if (!addSampleRate(stream->format(i)->sampleRate))
      {
        return false;
      }
    }
  }
  if (sampleRateCount_ == 0)
  {
    setError(AudioControlError::InvalidValue);
    return false;
  }
  currentSampleRate_ = sampleRates_[0];

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
  for (size_t i = 0; i < sampleRateCount_; ++i)
  {
    if (sampleRates_[i] == rate)
    {
      return true;
    }
  }
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
    value.minimum = static_cast<int32_t>(sampleRates_[0]);
    value.maximum = static_cast<int32_t>(sampleRates_[0]);
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
  return index < sampleRateCount_ ? sampleRates_[index] : 0;
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
