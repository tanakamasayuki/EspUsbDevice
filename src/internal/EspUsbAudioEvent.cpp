#include "EspUsbAudioEvent.h"
#include "EspUsbMemory.h"
#if __has_include("freertos/FreeRTOS.h")
#include "freertos/FreeRTOS.h"
#else
#include <mutex>
#endif
namespace {
#if __has_include("freertos/FreeRTOS.h")
portMUX_TYPE eventLock = portMUX_INITIALIZER_UNLOCKED;
struct Guard {
  Guard() { portENTER_CRITICAL(&eventLock); }
  ~Guard() { portEXIT_CRITICAL(&eventLock); }
};
#else
std::mutex eventLock;
struct Guard {
  Guard() { eventLock.lock(); }
  ~Guard() { eventLock.unlock(); }
};
#endif
}
namespace espusb { namespace internal {
AudioEventQueue::~AudioEventQueue() { release(); }
bool AudioEventQueue::configure(size_t capacity) {
  if (capacity > SIZE_MAX / sizeof(AudioRuntimeEvent)) return false;
  auto *replacement = capacity ? static_cast<AudioRuntimeEvent *>(allocateUsbMemory(capacity * sizeof(AudioRuntimeEvent))) : nullptr;
  if (capacity && !replacement) return false;
  AudioRuntimeEvent *previous;
  {
    Guard guard;
    previous = events_; events_ = replacement; capacity_ = capacity;
    head_ = tail_ = used_ = 0; dropped_ = 0;
  }
  freeUsbMemory(previous);
  return true;
}
void AudioEventQueue::release() { configure(0); }
size_t AudioEventQueue::capacity() const { Guard guard; return capacity_; }
bool AudioEventQueue::push(const AudioRuntimeEvent &event) {
  Guard guard;
  if (!capacity_) return false;
  if (used_ == capacity_) { ++dropped_; return false; }
  new (events_ + head_) AudioRuntimeEvent(event);
  if (++head_ == capacity_) head_ = 0;
  ++used_; return true;
}
bool AudioEventQueue::pop(AudioRuntimeEvent &event) {
  Guard guard;
  if (!used_) return false;
  event = events_[tail_];
  if (++tail_ == capacity_) tail_ = 0;
  --used_; return true;
}
size_t AudioEventQueue::pending() const { Guard guard; return used_; }
uint32_t AudioEventQueue::dropped() const { Guard guard; return dropped_; }
void AudioEventQueue::clear() { Guard guard; head_ = tail_ = used_ = 0; dropped_ = 0; }
} }
