#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
class EspUsbDeviceVideo;
struct EspUsbTransferSubmission;
namespace espusb { namespace internal {
struct VideoGuard {
  VideoGuard(); ~VideoGuard();
  VideoGuard(const VideoGuard &) = delete;
  VideoGuard &operator=(const VideoGuard &) = delete;
};
struct VideoTransport {
  static esp_err_t begin(EspUsbDeviceVideo &);
  static void end(EspUsbDeviceVideo &);
  static bool streaming(const EspUsbDeviceVideo &);
  static size_t capacity(const EspUsbDeviceVideo &);
  static EspUsbTransferSubmission submit(EspUsbDeviceVideo &, const void *, size_t);
  static esp_err_t cancel(EspUsbDeviceVideo &, uint64_t);
};
} }
