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

struct UsbRuntimeOptions {
  UsbController controller = UsbController::Auto;
  uint32_t taskStackBytes = 4096;
  int8_t taskPriority = -1;
  uint16_t eventQueueDepth = 16;
  // Bitmap of IN endpoint numbers whose DWC2 transmit FIFO should hold two
  // packets instead of one. Only bulk endpoints are affected; the controller
  // ignores the rest. Applied to the native controller before initialization;
  // change it after end(), before the next begin().
  uint16_t bulkInDoubleBuffered = 0;
  bool vbusSensing = false;
  // Core to pin the device worker to, or -1 to leave it unpinned as before.
  int8_t taskCoreId = -1;
};

// Caller-owned, one-shot worker jobs. Re-posting during a run schedules the
// next worker iteration. Remove only after stopping the worker.
struct UsbRuntimeJob {
  void (*run)(void *);
  void *context;
  UsbRuntimeJob *next = nullptr;
  bool queued = false;
};
bool postUsbRuntimeJob(UsbRuntimeJob &job);
void removeUsbRuntimeJob(UsbRuntimeJob &job);

esp_err_t startUsbRuntime(const UsbRuntimeOptions &options);
// timeoutMs 0 waits for the worker indefinitely.
esp_err_t stopUsbRuntime(uint32_t timeoutMs = 0);
bool usbRuntimeInTask();
bool usbRuntimeStarted();
bool usbRuntimeStopping();
uint8_t usbRuntimeRhport();

} // namespace internal
} // namespace espusb
