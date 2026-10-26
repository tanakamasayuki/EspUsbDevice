#pragma once
#include <stddef.h>
#include <new>
#include <utility>

namespace espusb { namespace internal {
// Private allocation boundary shared by independent functions. Definitions are
// weak so fault probes can replace them without test switches in the public API.
// alignment=0 is ordinary storage; DMA callers reserve whole 64-byte lines.
void *allocateUsbMemory(size_t bytes, size_t alignment = 0, bool dma = false);
void freeUsbMemory(void *memory);

template <typename T, typename... Args>
T *newUsbObject(Args &&...args) {
  void *memory = allocateUsbMemory(sizeof(T));
  return memory ? new (memory) T(std::forward<Args>(args)...) : nullptr;
}
template <typename T>
void deleteUsbObject(T *object) {
  if (!object) return;
  object->~T();
  freeUsbMemory(object);
}
} }
