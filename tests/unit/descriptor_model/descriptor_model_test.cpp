#include "internal/EspUsbDescriptorModel.h"
#include "internal/EspUsbHidDescriptor.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

using espusb::internal::DescriptorBuffer;
using espusb::internal::DescriptorBuildContext;
using espusb::internal::DescriptorError;
using espusb::internal::DescriptorLayout;
using espusb::internal::EndpointDirection;
using espusb::internal::EndpointLimits;
using espusb::internal::UsbSpeed;
using espusb::internal::HidFunctionConfig;
using espusb::internal::HidFunctionLayout;
using espusb::internal::allocateHidFunction;
using espusb::internal::writeDeviceQualifier;
using espusb::internal::writeHidFunction;

static uint16_t read16(const uint8_t *data)
{
  return static_cast<uint16_t>(data[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
}

static void testLayout()
{
  DescriptorLayout layout(EndpointLimits{5, 5, 5});
  assert(layout.ok());
  assert(layout.allocateInterfaces(2) == 0);
  assert(layout.allocateInterfaces() == 2);
  assert(layout.interfaceCount() == 3);
  assert(layout.allocateString() == 1);
  assert(layout.allocateString() == 2);

  const auto duplex = layout.allocateDuplexEndpoint();
  assert(duplex.out == 0x01);
  assert(duplex.in == 0x81);
  assert(layout.allocateEndpoint(EndpointDirection::In) == 0x82);
  assert(layout.allocateEndpoint(EndpointDirection::Out) == 0x02);
  assert(layout.reserveEndpoint(0x85));
  assert(layout.reserveEndpoint(0x05));
  assert(layout.inEndpointMask() == ((1U << 1) | (1U << 2) | (1U << 5)));
  assert(layout.outEndpointMask() == ((1U << 1) | (1U << 2) | (1U << 5)));
}

static void testEndpointConflict()
{
  DescriptorLayout layout(EndpointLimits{5, 5, 5});
  assert(layout.reserveEndpoint(0x81));
  assert(!layout.reserveEndpoint(0x81));
  assert(layout.error() == DescriptorError::EndpointConflict);
}

static void testEndpointCapacity()
{
  DescriptorLayout layout(EndpointLimits{5, 2, 1});
  assert(layout.allocateEndpoint(EndpointDirection::In) == 0x81);
  assert(layout.allocateEndpoint(EndpointDirection::In) == 0x82);
  assert(layout.allocateEndpoint(EndpointDirection::In) == 0);
  assert(layout.error() == DescriptorError::EndpointOverflow);
}

static void testInterfaceCapacity()
{
  DescriptorLayout layout(EndpointLimits{5, 5, 5});
  assert(layout.allocateInterfaces(254) == 0);
  assert(layout.allocateInterfaces() == 254);
  assert(layout.interfaceCount() == 255);
  assert(layout.allocateInterfaces() == 0xff);
  assert(layout.error() == DescriptorError::InterfaceOverflow);
}

static void testInvalidLimits()
{
  DescriptorLayout zeroEndpoints(EndpointLimits{});
  assert(!zeroEndpoints.ok());
  assert(zeroEndpoints.error() == DescriptorError::InvalidArgument);

  DescriptorLayout tooManyIn(EndpointLimits{5, 6, 5});
  assert(!tooManyIn.ok());
  assert(tooManyIn.error() == DescriptorError::InvalidArgument);
}

static void buildBulkConfiguration(UsbSpeed speed,
                                   uint8_t descriptorType,
                                   uint8_t *storage,
                                   size_t size)
{
  DescriptorBuffer buffer(storage, size);
  DescriptorBuildContext context(speed, buffer);
  assert(context.beginConfiguration(1, 1, 0x80, 50, descriptorType));
  assert(context.writeInterface(0, 0, 2, 0xff, 0, 0));
  assert(context.writeEndpoint(0x01, 0x02, 64, 512, 0, 0));
  assert(context.writeEndpoint(0x81, 0x02, 64, 512, 0, 0));
  assert(context.endConfiguration());
  assert(buffer.size() == 32);
}

static void testPerSpeedDescriptors()
{
  uint8_t full[32] = {};
  uint8_t high[32] = {};
  buildBulkConfiguration(UsbSpeed::Full, 0x02, full, sizeof(full));
  buildBulkConfiguration(UsbSpeed::High, 0x02, high, sizeof(high));

  assert(read16(full + 2) == sizeof(full));
  assert(read16(high + 2) == sizeof(high));
  assert(full[4] == 1);
  assert(memcmp(full, high, 20) == 0);
  assert(read16(full + 22) == 64);
  assert(read16(high + 22) == 512);
  assert(read16(full + 29) == 64);
  assert(read16(high + 29) == 512);
}

static void testOtherSpeedAndQualifier()
{
  uint8_t other[32] = {};
  buildBulkConfiguration(UsbSpeed::Full, 0x07, other, sizeof(other));
  assert(other[1] == 0x07);

  uint8_t qualifier[10] = {};
  DescriptorBuffer buffer(qualifier, sizeof(qualifier));
  assert(writeDeviceQualifier(buffer, 0x0200, 0xef, 0x02, 0x01, 64, 1));
  const uint8_t expected[] = {
      10, 0x06, 0x00, 0x02, 0xef, 0x02, 0x01, 64, 1, 0,
  };
  assert(buffer.size() == sizeof(expected));
  assert(memcmp(buffer.data(), expected, sizeof(expected)) == 0);
}

static void testHidFunctionUsesOneLayoutAtBothSpeeds()
{
  DescriptorLayout layout(EndpointLimits{5, 5, 5});
  HidFunctionLayout hid;
  assert(allocateHidFunction(layout, hid));
  assert(hid.interfaceNumber == 0);
  assert(hid.endpoint.out == 0x01);
  assert(hid.endpoint.in == 0x81);

  HidFunctionConfig config;
  config.subclass = 1;
  config.protocol = 1;
  config.reportDescriptorLength = 63;
  config.fullSpeedPacketSize = 8;
  config.highSpeedPacketSize = 16;

  uint8_t full[64] = {};
  DescriptorBuffer fullBuffer(full, sizeof(full));
  DescriptorBuildContext fullContext(UsbSpeed::Full, fullBuffer);
  assert(fullContext.beginConfiguration(1, 1, 0x80, 50));
  assert(writeHidFunction(fullContext, hid, config));
  assert(fullContext.endConfiguration());

  uint8_t high[64] = {};
  DescriptorBuffer highBuffer(high, sizeof(high));
  DescriptorBuildContext highContext(UsbSpeed::High, highBuffer);
  assert(highContext.beginConfiguration(1, 1, 0x80, 50));
  assert(writeHidFunction(highContext, hid, config));
  assert(highContext.endConfiguration());

  assert(fullBuffer.size() == 41);
  assert(highBuffer.size() == 41);
  assert(full[9 + 2] == high[9 + 2]);
  assert(full[24] == 0x22);
  assert(read16(full + 25) == 63);
  assert(full[29] == 0x01);
  assert(high[29] == 0x01);
  assert(read16(full + 31) == 8);
  assert(read16(high + 31) == 16);
  assert(full[36] == 0x81);
  assert(high[36] == 0x81);
  assert(read16(full + 38) == 8);
  assert(read16(high + 38) == 16);
}

static void testBufferOverflow()
{
  uint8_t storage[8] = {};
  DescriptorBuffer buffer(storage, sizeof(storage));
  DescriptorBuildContext context(UsbSpeed::Full, buffer);
  assert(!context.beginConfiguration(1, 1, 0x80, 50));
  assert(buffer.error() == DescriptorError::BufferOverflow);
}

int main()
{
  testLayout();
  testEndpointConflict();
  testEndpointCapacity();
  testInterfaceCapacity();
  testInvalidLimits();
  testPerSpeedDescriptors();
  testOtherSpeedAndQualifier();
  testHidFunctionUsesOneLayoutAtBothSpeeds();
  testBufferOverflow();
  return 0;
}
