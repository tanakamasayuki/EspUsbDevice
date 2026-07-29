#pragma once

#include <atomic>
#include <stddef.h>
#include <stdint.h>

namespace espusb {
namespace internal {

enum class AudioRuntimeEventType : uint8_t {
  SampleRate,
  Mute,
  Volume,
  StreamState,
};

enum class AudioRuntimeEventTarget : uint8_t {
  Function,
  Playback,
  Capture,
};

struct AudioRuntimeEvent {
  AudioRuntimeEventType type = AudioRuntimeEventType::SampleRate;
  AudioRuntimeEventTarget target = AudioRuntimeEventTarget::Function;
  uint8_t channel = 0;
  uint8_t alternateSetting = 0;
  int32_t value = 0;
};

class AudioEventQueue {
public:
  static constexpr uint8_t CAPACITY = 8;

  bool push(const AudioRuntimeEvent &event);
  bool pop(AudioRuntimeEvent &event);
  size_t pending() const;
  uint32_t dropped() const;
  void clear();

private:
  AudioRuntimeEvent events_[CAPACITY] = {};
  std::atomic<uint8_t> head_{0};
  std::atomic<uint8_t> tail_{0};
  std::atomic<uint32_t> dropped_{0};
};

} // namespace internal
} // namespace espusb
