#include "EspUsbHidDescriptor.h"

namespace espusb {
namespace internal {

bool allocateHidFunction(DescriptorLayout &layout,
                         HidFunctionLayout &function)
{
  if (!layout.ok() || function.interfaceNumber != 0xff ||
      static_cast<bool>(function.endpoint))
  {
    return false;
  }
  const uint8_t interfaceNumber = layout.allocateInterfaces();
  if (!layout.ok())
  {
    return false;
  }
  const DuplexEndpoint endpoint = layout.allocateDuplexEndpoint();
  if (!endpoint)
  {
    return false;
  }
  function.interfaceNumber = interfaceNumber;
  function.endpoint = endpoint;
  return true;
}

bool writeHidFunction(DescriptorBuildContext &context,
                      const HidFunctionLayout &layout,
                      const HidFunctionConfig &config)
{
  if (layout.interfaceNumber == 0xff || !layout.endpoint ||
      config.reportDescriptorLength == 0 ||
      config.fullSpeedPacketSize == 0 ||
      config.highSpeedPacketSize == 0)
  {
    return false;
  }

  DescriptorBuffer &buffer = context.buffer();
  const uint8_t endpointCount = config.hasOutEndpoint ? 2 : 1;
  if (!context.writeInterface(layout.interfaceNumber, 0, endpointCount,
                              0x03, config.subclass, config.protocol,
                              config.stringIndex))
  {
    return false;
  }

  if (!(buffer.writeU8(9) &&
        buffer.writeU8(0x21) &&
        buffer.writeU16(0x0111) &&
        buffer.writeU8(0) &&
        buffer.writeU8(1) &&
        buffer.writeU8(0x22) &&
        buffer.writeU16(config.reportDescriptorLength)))
  {
    return false;
  }

  if (config.hasOutEndpoint &&
      !context.writeEndpoint(layout.endpoint.out, 0x03,
                             config.fullSpeedPacketSize,
                             config.highSpeedPacketSize,
                             config.fullSpeedInterval,
                             config.highSpeedInterval))
  {
    return false;
  }

  return context.writeEndpoint(layout.endpoint.in, 0x03,
                               config.fullSpeedPacketSize,
                               config.highSpeedPacketSize,
                               config.fullSpeedInterval,
                               config.highSpeedInterval);
}

}  // namespace internal
}  // namespace espusb
