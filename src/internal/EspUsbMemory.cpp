#include "EspUsbMemory.h"
#include <stdint.h>
#include <stdlib.h>
#include "esp_heap_caps.h"

namespace espusb { namespace internal {
__attribute__((weak)) void *allocateUsbMemory(size_t bytes, size_t alignment, bool dma) {
  if (!bytes) return nullptr;
  if (!alignment && !dma) return malloc(bytes);
  if (!alignment) alignment = alignof(max_align_t);
  if ((alignment & (alignment - 1)) || bytes > SIZE_MAX - (alignment - 1)) return nullptr;
  bytes = (bytes + alignment - 1) & ~(alignment - 1);
  return heap_caps_aligned_alloc(alignment, bytes, MALLOC_CAP_INTERNAL | (dma ? MALLOC_CAP_DMA : 0));
}
__attribute__((weak)) void freeUsbMemory(void *memory) { free(memory); }
} }
