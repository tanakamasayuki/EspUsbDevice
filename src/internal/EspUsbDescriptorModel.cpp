#include "EspUsbDescriptorModel.h"

#include <string.h>

namespace espusb {
namespace internal {

namespace {

static uint8_t popcount16(uint16_t value)
{
  uint8_t count = 0;
  while (value != 0)
  {
    count = static_cast<uint8_t>(count + (value & 1U));
    value >>= 1;
  }
  return count;
}

}  // namespace

DescriptorBuffer::DescriptorBuffer(uint8_t *storage, size_t capacity)
    : storage_(storage), capacity_(capacity)
{
  if (storage_ == nullptr || capacity_ == 0)
  {
    error_ = DescriptorError::InvalidArgument;
  }
}

void DescriptorBuffer::reset()
{
  size_ = 0;
  error_ = (storage_ == nullptr || capacity_ == 0)
               ? DescriptorError::InvalidArgument
               : DescriptorError::None;
}

bool DescriptorBuffer::canWrite(size_t length)
{
  if (!ok())
  {
    return false;
  }
  if (length > capacity_ - size_)
  {
    error_ = DescriptorError::BufferOverflow;
    return false;
  }
  return true;
}

bool DescriptorBuffer::append(const void *data, size_t length)
{
  if ((data == nullptr && length != 0) || !canWrite(length))
  {
    if (data == nullptr && length != 0 && ok())
    {
      error_ = DescriptorError::InvalidArgument;
    }
    return false;
  }
  if (length != 0)
  {
    memcpy(storage_ + size_, data, length);
    size_ += length;
  }
  return true;
}

bool DescriptorBuffer::writeU8(uint8_t value)
{
  return append(&value, sizeof(value));
}

bool DescriptorBuffer::writeU16(uint16_t value)
{
  const uint8_t bytes[2] = {
      static_cast<uint8_t>(value & 0xffU),
      static_cast<uint8_t>((value >> 8) & 0xffU),
  };
  return append(bytes, sizeof(bytes));
}

bool DescriptorBuffer::patchU8(size_t offset, uint8_t value)
{
  if (!ok() || offset >= size_)
  {
    if (ok())
    {
      error_ = DescriptorError::InvalidArgument;
    }
    return false;
  }
  storage_[offset] = value;
  return true;
}

bool DescriptorBuffer::patchU16(size_t offset, uint16_t value)
{
  if (!ok() || offset >= size_ || size_ - offset < 2)
  {
    if (ok())
    {
      error_ = DescriptorError::InvalidArgument;
    }
    return false;
  }
  storage_[offset] = static_cast<uint8_t>(value & 0xffU);
  storage_[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffU);
  return true;
}

DescriptorLayout::DescriptorLayout(EndpointLimits limits) : limits_(limits)
{
  if (limits_.maxEndpointNumber == 0 || limits_.maxEndpointNumber > 15 ||
      limits_.maxInEndpoints > limits_.maxEndpointNumber ||
      limits_.maxOutEndpoints > limits_.maxEndpointNumber)
  {
    error_ = DescriptorError::InvalidArgument;
  }
}

uint8_t DescriptorLayout::allocateInterfaces(uint8_t count)
{
  if (!ok() || count == 0)
  {
    if (ok())
    {
      error_ = DescriptorError::InvalidArgument;
    }
    return 0xff;
  }
  if (static_cast<uint16_t>(nextInterface_) + count > 255U)
  {
    error_ = DescriptorError::InterfaceOverflow;
    return 0xff;
  }
  const uint8_t first = nextInterface_;
  nextInterface_ = static_cast<uint8_t>(nextInterface_ + count);
  return first;
}

uint8_t DescriptorLayout::allocateString()
{
  if (!ok())
  {
    return 0;
  }
  if (nextString_ > 255U)
  {
    error_ = DescriptorError::StringOverflow;
    return 0;
  }
  return static_cast<uint8_t>(nextString_++);
}

bool DescriptorLayout::endpointNumberValid(uint8_t number) const
{
  return number != 0 && number <= limits_.maxEndpointNumber;
}

uint8_t DescriptorLayout::usedEndpointCount(EndpointDirection direction) const
{
  return popcount16(direction == EndpointDirection::In ? inMask_ : outMask_);
}

bool DescriptorLayout::endpointCapacityAvailable(EndpointDirection direction) const
{
  const uint8_t maximum = direction == EndpointDirection::In
                              ? limits_.maxInEndpoints
                              : limits_.maxOutEndpoints;
  return usedEndpointCount(direction) < maximum;
}

bool DescriptorLayout::markEndpoint(uint8_t number, EndpointDirection direction)
{
  if (!ok() || !endpointNumberValid(number))
  {
    if (ok())
    {
      error_ = DescriptorError::InvalidArgument;
    }
    return false;
  }
  if (!endpointCapacityAvailable(direction))
  {
    error_ = DescriptorError::EndpointOverflow;
    return false;
  }

  uint16_t &mask = direction == EndpointDirection::In ? inMask_ : outMask_;
  const uint16_t bit = static_cast<uint16_t>(1U << number);
  if ((mask & bit) != 0)
  {
    error_ = DescriptorError::EndpointConflict;
    return false;
  }
  mask = static_cast<uint16_t>(mask | bit);
  return true;
}

uint8_t DescriptorLayout::allocateEndpoint(EndpointDirection direction)
{
  if (!ok() || !endpointCapacityAvailable(direction))
  {
    if (ok())
    {
      error_ = DescriptorError::EndpointOverflow;
    }
    return 0;
  }

  const uint16_t mask = direction == EndpointDirection::In ? inMask_ : outMask_;
  for (uint8_t number = 1; number <= limits_.maxEndpointNumber; ++number)
  {
    if ((mask & static_cast<uint16_t>(1U << number)) == 0)
    {
      if (!markEndpoint(number, direction))
      {
        return 0;
      }
      return direction == EndpointDirection::In
                 ? static_cast<uint8_t>(0x80U | number)
                 : number;
    }
  }
  error_ = DescriptorError::EndpointOverflow;
  return 0;
}

DuplexEndpoint DescriptorLayout::allocateDuplexEndpoint()
{
  if (!ok() || !endpointCapacityAvailable(EndpointDirection::In) ||
      !endpointCapacityAvailable(EndpointDirection::Out))
  {
    if (ok())
    {
      error_ = DescriptorError::EndpointOverflow;
    }
    return {};
  }

  const uint16_t used = static_cast<uint16_t>(inMask_ | outMask_);
  for (uint8_t number = 1; number <= limits_.maxEndpointNumber; ++number)
  {
    if ((used & static_cast<uint16_t>(1U << number)) == 0)
    {
      if (!markEndpoint(number, EndpointDirection::Out) ||
          !markEndpoint(number, EndpointDirection::In))
      {
        return {};
      }
      return {number, static_cast<uint8_t>(0x80U | number)};
    }
  }
  error_ = DescriptorError::EndpointOverflow;
  return {};
}

bool DescriptorLayout::reserveEndpoint(uint8_t address)
{
  const uint8_t number = static_cast<uint8_t>(address & 0x0fU);
  const EndpointDirection direction = (address & 0x80U) != 0
                                          ? EndpointDirection::In
                                          : EndpointDirection::Out;
  if ((address & 0x70U) != 0)
  {
    if (ok())
    {
      error_ = DescriptorError::InvalidArgument;
    }
    return false;
  }
  return markEndpoint(number, direction);
}

DescriptorBuildContext::DescriptorBuildContext(UsbSpeed speed,
                                               DescriptorBuffer &buffer)
    : speed_(speed), buffer_(buffer)
{
}

bool DescriptorBuildContext::beginConfiguration(uint8_t interfaceCount,
                                                uint8_t configurationValue,
                                                uint8_t attributes,
                                                uint8_t maxPower,
                                                uint8_t descriptorType)
{
  if (configurationOpen_ || interfaceCount == 0 || configurationValue == 0 ||
      (descriptorType != 0x02 && descriptorType != 0x07) || !buffer_.ok())
  {
    return false;
  }
  configurationOffset_ = buffer_.size();
  configurationOpen_ = true;
  return buffer_.writeU8(9) &&
         buffer_.writeU8(descriptorType) &&
         buffer_.writeU16(0) &&
         buffer_.writeU8(interfaceCount) &&
         buffer_.writeU8(configurationValue) &&
         buffer_.writeU8(0) &&
         buffer_.writeU8(attributes) &&
         buffer_.writeU8(maxPower);
}

bool DescriptorBuildContext::endConfiguration()
{
  if (!configurationOpen_ || buffer_.size() - configurationOffset_ > 0xffffU)
  {
    return false;
  }
  const uint16_t totalLength =
      static_cast<uint16_t>(buffer_.size() - configurationOffset_);
  if (!buffer_.patchU16(configurationOffset_ + 2, totalLength))
  {
    return false;
  }
  configurationOpen_ = false;
  return true;
}

bool DescriptorBuildContext::writeInterface(uint8_t number,
                                            uint8_t alternateSetting,
                                            uint8_t endpointCount,
                                            uint8_t classCode,
                                            uint8_t subclass,
                                            uint8_t protocol,
                                            uint8_t stringIndex)
{
  return buffer_.writeU8(9) &&
         buffer_.writeU8(0x04) &&
         buffer_.writeU8(number) &&
         buffer_.writeU8(alternateSetting) &&
         buffer_.writeU8(endpointCount) &&
         buffer_.writeU8(classCode) &&
         buffer_.writeU8(subclass) &&
         buffer_.writeU8(protocol) &&
         buffer_.writeU8(stringIndex);
}

bool DescriptorBuildContext::writeEndpoint(uint8_t address,
                                           uint8_t attributes,
                                           uint16_t fullSpeedMaxPacket,
                                           uint16_t highSpeedMaxPacket,
                                           uint8_t fullSpeedInterval,
                                           uint8_t highSpeedInterval)
{
  const uint8_t endpointNumber = static_cast<uint8_t>(address & 0x0fU);
  if (endpointNumber == 0 || (address & 0x70U) != 0 ||
      fullSpeedMaxPacket == 0 || highSpeedMaxPacket == 0)
  {
    return false;
  }
  const uint16_t maxPacket = speed_ == UsbSpeed::High
                                 ? highSpeedMaxPacket
                                 : fullSpeedMaxPacket;
  const uint8_t interval = speed_ == UsbSpeed::High
                               ? highSpeedInterval
                               : fullSpeedInterval;
  return buffer_.writeU8(7) &&
         buffer_.writeU8(0x05) &&
         buffer_.writeU8(address) &&
         buffer_.writeU8(attributes) &&
         buffer_.writeU16(maxPacket) &&
         buffer_.writeU8(interval);
}

bool writeDeviceQualifier(DescriptorBuffer &buffer,
                          uint16_t usbVersion,
                          uint8_t deviceClass,
                          uint8_t deviceSubclass,
                          uint8_t deviceProtocol,
                          uint8_t endpoint0Size,
                          uint8_t configurationCount)
{
  if (endpoint0Size == 0 || configurationCount == 0)
  {
    return false;
  }
  return buffer.writeU8(10) &&
         buffer.writeU8(0x06) &&
         buffer.writeU16(usbVersion) &&
         buffer.writeU8(deviceClass) &&
         buffer.writeU8(deviceSubclass) &&
         buffer.writeU8(deviceProtocol) &&
         buffer.writeU8(endpoint0Size) &&
         buffer.writeU8(configurationCount) &&
         buffer.writeU8(0);
}

}  // namespace internal
}  // namespace espusb
