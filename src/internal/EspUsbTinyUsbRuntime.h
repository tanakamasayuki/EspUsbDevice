#pragma once

#include <stdint.h>
#include "esp_err.h"

namespace espusb {
namespace internal {

enum class UsbController : uint8_t {
  Auto,
  FullSpeed,
  HighSpeed,
};

struct TinyUsbRuntimeOptions {
  UsbController controller = UsbController::Auto;
  uint32_t taskStackSize = 4096;
  uint8_t taskPriority = 0;
  uint16_t eventQueueDepth = 16;
  // Bitmap of IN endpoint numbers whose DWC2 transmit FIFO should hold two
  // packets instead of one. Only bulk endpoints are affected; the controller
  // ignores the rest. Applied to the native controller before initialization;
  // change it after end(), before the next begin().
  uint16_t bulkInDoubleBuffered = 0;
  // Core to pin the device worker to, or -1 to leave it unpinned as before.
  int8_t taskCoreId = -1;
};

// Caller-owned, one-shot worker jobs. Re-posting during a run schedules the
// next worker iteration. Remove only after stopping the worker.
struct TinyUsbRuntimeJob {
  void (*run)(void *);
  void *context;
  TinyUsbRuntimeJob *next = nullptr;
  bool queued = false;
};
bool postTinyUsbRuntimeJob(TinyUsbRuntimeJob &job);
void removeTinyUsbRuntimeJob(TinyUsbRuntimeJob &job);

esp_err_t startTinyUsbRuntime(const TinyUsbRuntimeOptions &options);
bool stopTinyUsbRuntime();
bool tinyUsbRuntimeInTask();
bool tinyUsbRuntimeStarted();
uint8_t tinyUsbRuntimeRhport();

} // namespace internal
} // namespace espusb
