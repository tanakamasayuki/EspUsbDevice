#include "EspUsbAudioEvent.h"

namespace espusb {
namespace internal {

bool AudioEventQueue::push(const AudioRuntimeEvent &event)
{
  const uint8_t head = head_.load(std::memory_order_relaxed);
  const uint8_t tail = tail_.load(std::memory_order_acquire);
  if (static_cast<uint8_t>(head - tail) >= CAPACITY)
  {
    dropped_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  events_[head % CAPACITY] = event;
  head_.store(static_cast<uint8_t>(head + 1), std::memory_order_release);
  return true;
}

bool AudioEventQueue::pop(AudioRuntimeEvent &event)
{
  const uint8_t tail = tail_.load(std::memory_order_relaxed);
  const uint8_t head = head_.load(std::memory_order_acquire);
  if (tail == head)
  {
    return false;
  }
  event = events_[tail % CAPACITY];
  tail_.store(static_cast<uint8_t>(tail + 1), std::memory_order_release);
  return true;
}

size_t AudioEventQueue::pending() const
{
  const uint8_t head = head_.load(std::memory_order_acquire);
  const uint8_t tail = tail_.load(std::memory_order_acquire);
  return static_cast<uint8_t>(head - tail);
}

uint32_t AudioEventQueue::dropped() const
{
  return dropped_.load(std::memory_order_relaxed);
}

void AudioEventQueue::clear()
{
  tail_.store(head_.load(std::memory_order_acquire),
              std::memory_order_release);
  dropped_.store(0, std::memory_order_relaxed);
}

} // namespace internal
} // namespace espusb
