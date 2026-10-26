#pragma once
#include <stddef.h>
#include <stdint.h>
namespace espusb { namespace internal {
enum class AudioRuntimeEventType : uint8_t { SampleRate, Mute, Volume, StreamState };
enum class AudioRuntimeEventTarget : uint8_t { Function, Playback, Capture };
struct AudioRuntimeEvent {
  AudioRuntimeEventType type = AudioRuntimeEventType::SampleRate;
  AudioRuntimeEventTarget target = AudioRuntimeEventTarget::Function;
  uint8_t channel = 0;
  uint8_t alternateSetting = 0;
  int32_t value = 0;
};
class AudioEventQueue {
public:
  AudioEventQueue() = default;
  ~AudioEventQueue();
  AudioEventQueue(const AudioEventQueue &) = delete;
  AudioEventQueue &operator=(const AudioEventQueue &) = delete;
  // Storage configuration is a stopped-lifecycle operation. Zero disables
  // notifications without counting intentionally disabled events as dropped.
  bool configure(size_t capacity);
  void release();
  size_t capacity() const;
  bool push(const AudioRuntimeEvent &event);
  bool pop(AudioRuntimeEvent &event);
  size_t pending() const;
  uint32_t dropped() const;
  void clear();
private:
  AudioRuntimeEvent *events_ = nullptr;
  size_t capacity_ = 0, head_ = 0, tail_ = 0, used_ = 0;
  uint32_t dropped_ = 0;
};
} }
