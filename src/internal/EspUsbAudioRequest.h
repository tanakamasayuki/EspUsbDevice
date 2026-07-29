#pragma once

#include "EspUsbAudioControl.h"

namespace espusb {
namespace internal {

struct Uac2EntityRequest {
  uint8_t request = 0;
  uint8_t channel = 0;
  uint8_t selector = 0;
  uint8_t interfaceNumber = 0;
  uint8_t entityId = 0;
  uint16_t length = 0;
};

enum class AudioRequestError : uint8_t {
  None,
  InvalidInterface,
  UnknownEntity,
  UnsupportedRequest,
  UnsupportedSelector,
  InvalidLength,
  InvalidValue,
  BufferOverflow,
};

bool writeUac2EntityResponse(AudioControlState &state,
                             uint8_t controlInterface,
                             const Uac2EntityRequest &request,
                             DescriptorBuffer &response,
                             AudioRequestError *error = nullptr);

bool applyUac2EntityRequest(AudioControlState &state,
                            uint8_t controlInterface,
                            const Uac2EntityRequest &request,
                            const uint8_t *data,
                            size_t length,
                            AudioRequestError *error = nullptr);

} // namespace internal
} // namespace espusb
