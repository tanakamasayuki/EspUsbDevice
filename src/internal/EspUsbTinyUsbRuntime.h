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
};

esp_err_t startTinyUsbRuntime(const TinyUsbRuntimeOptions &options);
void stopTinyUsbRuntime();
bool tinyUsbRuntimeStarted();
uint8_t tinyUsbRuntimeRhport();

} // namespace internal
} // namespace espusb
