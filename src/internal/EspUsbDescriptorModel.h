#pragma once

#include <stddef.h>
#include <stdint.h>

namespace espusb {
namespace internal {

enum class UsbSpeed : uint8_t {
  Full,
  High,
};

enum class EndpointDirection : uint8_t {
  Out,
  In,
};

enum class DescriptorError : uint8_t {
  None,
  BufferOverflow,
  InvalidArgument,
  InterfaceOverflow,
  EndpointOverflow,
  EndpointConflict,
  StringOverflow,
};

struct EndpointLimits {
  uint8_t maxEndpointNumber = 0;
  uint8_t maxInEndpoints = 0;
  uint8_t maxOutEndpoints = 0;
};

struct DuplexEndpoint {
  uint8_t out = 0;
  uint8_t in = 0;

  explicit operator bool() const { return out != 0 && in != 0; }
};

class DescriptorBuffer {
public:
  DescriptorBuffer(uint8_t *storage, size_t capacity);

  void reset();
  bool append(const void *data, size_t length);
  bool writeU8(uint8_t value);
  bool writeU16(uint16_t value);
  bool patchU8(size_t offset, uint8_t value);
  bool patchU16(size_t offset, uint16_t value);

  const uint8_t *data() const { return storage_; }
  uint8_t *data() { return storage_; }
  size_t size() const { return size_; }
  size_t capacity() const { return capacity_; }
  DescriptorError error() const { return error_; }
  bool ok() const { return error_ == DescriptorError::None; }

private:
  bool canWrite(size_t length);

  uint8_t *storage_ = nullptr;
  size_t capacity_ = 0;
  size_t size_ = 0;
  DescriptorError error_ = DescriptorError::None;
};

class DescriptorLayout {
public:
  explicit DescriptorLayout(EndpointLimits limits);

  uint8_t allocateInterfaces(uint8_t count = 1);
  uint8_t allocateString();
  uint8_t allocateEndpoint(EndpointDirection direction);
  DuplexEndpoint allocateDuplexEndpoint();
  bool reserveEndpoint(uint8_t address);

  uint8_t interfaceCount() const { return nextInterface_; }
  uint8_t stringCount() const { return static_cast<uint8_t>(nextString_ - 1); }
  uint16_t inEndpointMask() const { return inMask_; }
  uint16_t outEndpointMask() const { return outMask_; }
  DescriptorError error() const { return error_; }
  bool ok() const { return error_ == DescriptorError::None; }

private:
  bool endpointNumberValid(uint8_t number) const;
  bool endpointCapacityAvailable(EndpointDirection direction) const;
  bool markEndpoint(uint8_t number, EndpointDirection direction);
  uint8_t usedEndpointCount(EndpointDirection direction) const;

  EndpointLimits limits_{};
  uint16_t inMask_ = 0;
  uint16_t outMask_ = 0;
  uint8_t nextInterface_ = 0;
  uint16_t nextString_ = 1;
  DescriptorError error_ = DescriptorError::None;
};

class DescriptorBuildContext {
public:
  DescriptorBuildContext(UsbSpeed speed, DescriptorBuffer &buffer);

  bool beginConfiguration(uint8_t interfaceCount,
                          uint8_t configurationValue,
                          uint8_t attributes,
                          uint8_t maxPower,
                          uint8_t descriptorType = 0x02);
  bool endConfiguration();
  bool writeInterface(uint8_t number,
                      uint8_t alternateSetting,
                      uint8_t endpointCount,
                      uint8_t classCode,
                      uint8_t subclass,
                      uint8_t protocol,
                      uint8_t stringIndex = 0);
  bool writeEndpoint(uint8_t address,
                     uint8_t attributes,
                     uint16_t fullSpeedMaxPacket,
                     uint16_t highSpeedMaxPacket,
                     uint8_t fullSpeedInterval,
                     uint8_t highSpeedInterval);

  UsbSpeed speed() const { return speed_; }
  DescriptorBuffer &buffer() { return buffer_; }

private:
  UsbSpeed speed_;
  DescriptorBuffer &buffer_;
  size_t configurationOffset_ = 0;
  bool configurationOpen_ = false;
};

bool writeDeviceQualifier(DescriptorBuffer &buffer,
                          uint16_t usbVersion,
                          uint8_t deviceClass,
                          uint8_t deviceSubclass,
                          uint8_t deviceProtocol,
                          uint8_t endpoint0Size,
                          uint8_t configurationCount);

}  // namespace internal
}  // namespace espusb
