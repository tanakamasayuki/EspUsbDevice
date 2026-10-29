#pragma once
#include <stdint.h>
#include "esp_err.h"
namespace espusb { namespace internal {
// The DWC2 adapter shares native USB wire types. This is the
// independent core's lifecycle boundary, not a selectable second backend.
esp_err_t startUsbDeviceCore(uint8_t port, bool highSpeed, uint16_t eventQueueDepth);
void stopUsbDeviceCore();
bool usbRemoteWakeupEnabled();
esp_err_t requestUsbRemoteWakeup();
void runUsbDeviceCore(uint32_t timeoutMilliseconds);
bool usbDeviceCoreReadyForJobs();
uint8_t usbEndpointInterface(uint8_t port, uint8_t address);
bool rejectUsbControl();
uint16_t usbControlTransferredBytes();
} }
