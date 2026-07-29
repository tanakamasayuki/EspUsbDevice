#pragma once

#include "EspUsbDescriptorModel.h"

namespace espusb {
namespace internal {

struct HidFunctionLayout {
  uint8_t interfaceNumber = 0xff;
  DuplexEndpoint endpoint{};
};

struct HidFunctionConfig {
  uint8_t subclass = 0;
  uint8_t protocol = 0;
  uint8_t stringIndex = 0;
  uint16_t reportDescriptorLength = 0;
  uint16_t fullSpeedPacketSize = 0;
  uint16_t highSpeedPacketSize = 0;
  uint8_t fullSpeedInterval = 1;
  uint8_t highSpeedInterval = 4;
  bool hasOutEndpoint = true;
};

bool allocateHidFunction(DescriptorLayout &layout,
                         HidFunctionLayout &function);

bool writeHidFunction(DescriptorBuildContext &context,
                      const HidFunctionLayout &layout,
                      const HidFunctionConfig &config);

}  // namespace internal
}  // namespace espusb
