#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
class EspUsbAudioFunction;
namespace espusb { namespace internal {
struct AudioGuard {
  AudioGuard();
  ~AudioGuard();
  AudioGuard(const AudioGuard &) = delete;
  AudioGuard &operator=(const AudioGuard &) = delete;
};
struct AudioTransport {
  static esp_err_t begin(EspUsbAudioFunction &);
  static void prepareToStop(EspUsbAudioFunction &);
  static void end(EspUsbAudioFunction &);
  static size_t available(const EspUsbAudioFunction &, bool playback);
  static size_t capacity(const EspUsbAudioFunction &, bool playback, bool packet = false);
  static size_t read(EspUsbAudioFunction &, void *, size_t);
  static size_t write(EspUsbAudioFunction &, const void *, size_t);
  static bool clear(EspUsbAudioFunction &, bool playback);
  static bool replyEntity(EspUsbAudioFunction &, uint8_t port, const void *request);
  static bool reply(EspUsbAudioFunction &, uint8_t port, const void *request, const void *, size_t);
};
} }
