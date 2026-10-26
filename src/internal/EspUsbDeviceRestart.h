#pragma once
#include <stdint.h>
class EspUsbDevice;
namespace espusb { namespace internal {
bool scheduleDeviceRestart(EspUsbDevice *device, uint32_t delayMs, bool toBootloader);
} }
