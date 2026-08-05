// Host-side unit test for the CCID configuration descriptor bytes.
//
// The descriptor builder is extracted verbatim from
// src/EspUsbDeviceCcid.cpp by test_ccid_descriptor.py into
// "espusbdevice_ccid_real.h", so these checks run against the shipped code.
//
// What matters here is the CCID class descriptor (bDescriptorType 0x21): a host
// reads dwFeatures to decide whether it may send whole APDUs, and
// dwMaxCCIDMessageLength to size its buffers. The endpoint layout matters too -
// the interrupt IN endpoint is optional in CCID, but a reader that declares one
// has to have it at the address the host is told.

#include <cstdint>
#include <cstddef>
#include <cstdio>

#include "espusbdevice_ccid_real.h"

namespace
{
int failures = 0;

void check(bool condition, const char *name)
{
  if (!condition)
  {
    printf("FAIL %s\n", name);
    failures++;
  }
}

uint16_t le16(const uint8_t *data)
{
  return static_cast<uint16_t>(data[0]) | static_cast<uint16_t>(data[1] << 8);
}

uint32_t le32(const uint8_t *data)
{
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

} // namespace

int main()
{
  uint8_t buffer[256] = {};

  // Standalone reader: interface 0, endpoints starting at 1.
  const uint16_t length = ccidConfigurationDescriptor(buffer, 0, 1, 64);
  check(length == 9 + 54 + 21, "total_length");

  const uint8_t *itf = &buffer[0];
  check(itf[0] == 9 && itf[1] == 0x04, "interface_header");
  check(itf[2] == 0 && itf[3] == 0, "interface_number_and_alt");
  check(itf[4] == 3, "interface_endpoint_count");
  // Class 0x0b subclass 0 protocol 0 is bulk CCID; a non-zero protocol would be
  // ICCD, which speaks over control transfers and is not what this implements.
  check(itf[5] == 0x0b && itf[6] == 0x00 && itf[7] == 0x00, "interface_class");

  const uint8_t *ccid = &buffer[9];
  check(ccid[0] == 54 && ccid[1] == 0x21, "ccid_descriptor_header");
  check(le16(&ccid[2]) == 0x0110, "ccid_bcd");
  check(ccid[4] == 0, "ccid_max_slot_index");
  check(ccid[5] == 0x07, "ccid_voltage_support");
  check(le32(&ccid[6]) == 0x00000002, "ccid_protocols_t1");
  check(le32(&ccid[10]) == 3580 && le32(&ccid[14]) == 3580, "ccid_clock");
  check(ccid[18] == 0, "ccid_num_clocks");
  check(le32(&ccid[19]) == 9600 && le32(&ccid[23]) == 9600, "ccid_data_rate");
  check(ccid[27] == 0, "ccid_num_data_rates");
  check(le32(&ccid[28]) == 254, "ccid_max_ifsd");
  check(le32(&ccid[32]) == 0, "ccid_synch_protocols");
  check(le32(&ccid[36]) == 0, "ccid_mechanical");
  // Bits 16..18 == 0b010: short APDU level exchange. This is the field a host
  // reads to decide between sending TPDUs and sending APDUs.
  const uint32_t features = le32(&ccid[40]);
  check(((features >> 16) & 0x07) == 0x02, "ccid_exchange_level_short_apdu");
  check((features & 0x0000000e) == 0x0000000e, "ccid_auto_activation_and_voltage");
  check(le32(&ccid[44]) == 10 + 261, "ccid_max_message_length");
  check(ccid[48] == 0xff && ccid[49] == 0xff, "ccid_class_get_response_envelope");
  check(le16(&ccid[50]) == 0, "ccid_lcd_layout");
  check(ccid[52] == 0, "ccid_pin_support");
  check(ccid[53] == 1, "ccid_max_busy_slots");

  const uint8_t *epOut = &buffer[9 + 54];
  const uint8_t *epIn = &buffer[9 + 54 + 7];
  const uint8_t *epNotify = &buffer[9 + 54 + 14];
  check(epOut[0] == 7 && epOut[1] == 0x05 && epOut[2] == 0x01, "ep_out_address");
  check(epOut[3] == 0x02 && le16(&epOut[4]) == 64, "ep_out_bulk_64");
  check(epIn[0] == 7 && epIn[1] == 0x05 && epIn[2] == 0x81, "ep_in_address");
  check(epIn[3] == 0x02 && le16(&epIn[4]) == 64, "ep_in_bulk_64");
  check(epNotify[0] == 7 && epNotify[1] == 0x05 && epNotify[2] == 0x82, "ep_notify_address");
  check(epNotify[3] == 0x03 && le16(&epNotify[4]) == 8, "ep_notify_interrupt_8");
  check(epNotify[6] == 16, "ep_notify_interval");

  // Composite placement: the interface number and both endpoint numbers move,
  // and the interrupt endpoint stays one above the bulk pair.
  uint8_t composite[256] = {};
  const uint16_t compositeLength = ccidConfigurationDescriptor(composite, 2, 3, 64);
  check(compositeLength == length, "composite_same_length");
  check(composite[2] == 2, "composite_interface_number");
  check(composite[9 + 54 + 2] == 0x03, "composite_ep_out");
  check(composite[9 + 54 + 7 + 2] == 0x83, "composite_ep_in");
  check(composite[9 + 54 + 14 + 2] == 0x84, "composite_ep_notify");

  // An endpoint number with no room for the interrupt endpoint above it is
  // refused rather than silently wrapping to endpoint 15 + 1.
  check(ccidConfigurationDescriptor(buffer, 0, 15, 64) == 0, "reject_endpoint_15");
  check(ccidConfigurationDescriptor(buffer, 0, 0, 64) == 0, "reject_endpoint_0");
  check(ccidConfigurationDescriptor(nullptr, 0, 1, 64) == 0, "reject_null");

  if (failures != 0)
  {
    printf("NG %d failures\n", failures);
    return 1;
  }
  printf("OK\n");
  return 0;
}
