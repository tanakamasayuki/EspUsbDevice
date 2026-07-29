#pragma once

#include "EspUsbAudioModel.h"

namespace espusb {
namespace internal {

enum class AudioDescriptorError : uint8_t {
  None,
  InvalidGraph,
  UnsupportedProtocol,
  UnsupportedFormatCount,
  InvalidFormat,
  BufferOverflow,
};

struct AudioDescriptorConfig {
  uint8_t functionStringIndex = 0;
  uint8_t controlStringIndex = 0;
  uint8_t playbackStringIndex = 0;
  uint8_t captureStringIndex = 0;
};

bool writeUac1Function(DescriptorBuildContext &context,
                       const AudioFunctionModel &function,
                       const AudioFunctionGraph &graph,
                       const AudioDescriptorConfig &config,
                       AudioDescriptorError *error = nullptr);

bool writeUac2Function(DescriptorBuildContext &context,
                       const AudioFunctionModel &function,
                       const AudioFunctionGraph &graph,
                       const AudioDescriptorConfig &config,
                       AudioDescriptorError *error = nullptr);

} // namespace internal
} // namespace espusb
