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
  // Bitmap of IN endpoint numbers whose DWC2 transmit FIFO should hold two
  // packets instead of one. Only bulk endpoints are affected; the controller
  // ignores the rest. Applied through tud_configure() before tusb_init(),
  // which is the only point at which it can be set.
  uint16_t bulkInDoubleBuffered = 0;
  // Core to pin the usbd task to, or -1 to leave it unpinned as before.
  int8_t taskCoreId = -1;
};

esp_err_t startTinyUsbRuntime(const TinyUsbRuntimeOptions &options);
void stopTinyUsbRuntime();
bool tinyUsbRuntimeStarted();
uint8_t tinyUsbRuntimeRhport();

} // namespace internal
} // namespace espusb
