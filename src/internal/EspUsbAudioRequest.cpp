#include "EspUsbAudioRequest.h"

namespace espusb {
namespace internal {

namespace {

constexpr uint8_t REQUEST_CUR = 0x01;
constexpr uint8_t REQUEST_RANGE = 0x02;
constexpr uint8_t SELECTOR_ONE = 0x01;
constexpr uint8_t SELECTOR_TWO = 0x02;

void setError(AudioRequestError *error, AudioRequestError value)
{
  if (error)
  {
    *error = value;
  }
}

bool writeU32(DescriptorBuffer &buffer, uint32_t value)
{
  return buffer.writeU16(static_cast<uint16_t>(value & 0xffffU)) &&
         buffer.writeU16(static_cast<uint16_t>(value >> 16));
}

uint32_t readU32(const uint8_t *data)
{
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

int16_t readI16(const uint8_t *data)
{
  return static_cast<int16_t>(
      static_cast<uint16_t>(data[0]) |
      static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8));
}

bool resolveSelector(const AudioControlState &state, uint8_t entityId,
                     uint8_t selector, AudioControlSelector &resolved)
{
  switch (state.entityKind(entityId))
  {
  case AudioControlEntityKind::Clock:
    if (selector == SELECTOR_ONE)
    {
      resolved = AudioControlSelector::SampleRate;
      return true;
    }
    if (selector == SELECTOR_TWO)
    {
      resolved = AudioControlSelector::ClockValid;
      return true;
    }
    return false;
  case AudioControlEntityKind::Feature:
    if (selector == SELECTOR_ONE)
    {
      resolved = AudioControlSelector::Mute;
      return true;
    }
    if (selector == SELECTOR_TWO)
    {
      resolved = AudioControlSelector::Volume;
      return true;
    }
    return false;
  case AudioControlEntityKind::Unknown:
    return false;
  }
  return false;
}

bool validateTarget(const AudioControlState &state, uint8_t controlInterface,
                    const Uac2EntityRequest &request,
                    AudioControlSelector &selector,
                    AudioRequestError *error)
{
  if (request.interfaceNumber != controlInterface)
  {
    setError(error, AudioRequestError::InvalidInterface);
    return false;
  }
  if (state.entityKind(request.entityId) == AudioControlEntityKind::Unknown)
  {
    setError(error, AudioRequestError::UnknownEntity);
    return false;
  }
  if (!resolveSelector(state, request.entityId, request.selector, selector))
  {
    setError(error, AudioRequestError::UnsupportedSelector);
    return false;
  }
  return true;
}

} // namespace

bool writeUac2EntityResponse(AudioControlState &state,
                             uint8_t controlInterface,
                             const Uac2EntityRequest &request,
                             DescriptorBuffer &response,
                             AudioRequestError *error)
{
  setError(error, AudioRequestError::None);
  AudioControlSelector selector;
  if (!validateTarget(state, controlInterface, request, selector, error))
  {
    return false;
  }

  int32_t current = 0;
  if (request.request == REQUEST_CUR)
  {
    const uint16_t expected =
        selector == AudioControlSelector::SampleRate
            ? 4
            : (selector == AudioControlSelector::Volume ? 2 : 1);
    if (request.length != expected)
    {
      setError(error, AudioRequestError::InvalidLength);
      return false;
    }
    if (!state.current(request.entityId, selector, request.channel, current))
    {
      setError(error, AudioRequestError::InvalidValue);
      return false;
    }
    bool ok = false;
    if (selector == AudioControlSelector::SampleRate)
    {
      ok = writeU32(response, static_cast<uint32_t>(current));
    }
    else if (selector == AudioControlSelector::Volume)
    {
      ok = response.writeU16(static_cast<uint16_t>(
          static_cast<int16_t>(current)));
    }
    else
    {
      ok = response.writeU8(static_cast<uint8_t>(current));
    }
    if (!ok)
    {
      setError(error, AudioRequestError::BufferOverflow);
    }
    return ok;
  }

  if (request.request != REQUEST_RANGE ||
      selector == AudioControlSelector::Mute ||
      selector == AudioControlSelector::ClockValid)
  {
    setError(error, AudioRequestError::UnsupportedRequest);
    return false;
  }

  if (selector == AudioControlSelector::SampleRate)
  {
    const size_t expected = 2U + state.sampleRateCount() * 12U;
    if (request.length != expected)
    {
      setError(error, AudioRequestError::InvalidLength);
      return false;
    }
    if (!response.writeU16(static_cast<uint16_t>(state.sampleRateCount())))
    {
      setError(error, AudioRequestError::BufferOverflow);
      return false;
    }
    for (size_t i = 0; i < state.sampleRateCount(); ++i)
    {
      const uint32_t rate = state.sampleRate(i);
      if (!(writeU32(response, rate) && writeU32(response, rate) &&
            writeU32(response, 0)))
      {
        setError(error, AudioRequestError::BufferOverflow);
        return false;
      }
    }
    return true;
  }

  AudioControlRange range;
  if (request.length != 8)
  {
    setError(error, AudioRequestError::InvalidLength);
    return false;
  }
  if (!state.range(request.entityId, selector, request.channel, range) ||
      !(response.writeU16(1) &&
        response.writeU16(static_cast<uint16_t>(
            static_cast<int16_t>(range.minimum))) &&
        response.writeU16(static_cast<uint16_t>(
            static_cast<int16_t>(range.maximum))) &&
        response.writeU16(static_cast<uint16_t>(
            static_cast<int16_t>(range.resolution)))))
  {
    setError(error, response.ok() ? AudioRequestError::InvalidValue
                                  : AudioRequestError::BufferOverflow);
    return false;
  }
  return true;
}

bool applyUac2EntityRequest(AudioControlState &state,
                            uint8_t controlInterface,
                            const Uac2EntityRequest &request,
                            const uint8_t *data,
                            size_t length,
                            AudioRequestError *error,
                            AudioControlChange *change)
{
  setError(error, AudioRequestError::None);
  if (change)
  {
    *change = AudioControlChange{};
  }
  AudioControlSelector selector;
  if (!validateTarget(state, controlInterface, request, selector, error))
  {
    return false;
  }
  if (request.request != REQUEST_CUR || !data)
  {
    setError(error, AudioRequestError::UnsupportedRequest);
    return false;
  }

  int32_t value = 0;
  size_t expected = 0;
  if (selector == AudioControlSelector::SampleRate)
  {
    expected = 4;
    if (length == expected)
    {
      value = static_cast<int32_t>(readU32(data));
    }
  }
  else if (selector == AudioControlSelector::Volume)
  {
    expected = 2;
    if (length == expected)
    {
      value = readI16(data);
    }
  }
  else
  {
    expected = 1;
    if (length == expected)
    {
      value = data[0];
    }
  }
  if (length != expected || request.length != expected)
  {
    setError(error, AudioRequestError::InvalidLength);
    return false;
  }
  int32_t previous = 0;
  if (!state.current(request.entityId, selector, request.channel, previous))
  {
    setError(error, AudioRequestError::InvalidValue);
    return false;
  }
  if (!state.setCurrent(request.entityId, selector, request.channel, value))
  {
    setError(error, AudioRequestError::InvalidValue);
    return false;
  }
  if (change)
  {
    change->selector = selector;
    change->entityId = request.entityId;
    change->channel = request.channel;
    change->value = value;
    change->changed = previous != value;
  }
  return true;
}

} // namespace internal
} // namespace espusb
