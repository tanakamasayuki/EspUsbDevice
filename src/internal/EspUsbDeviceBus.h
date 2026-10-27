#pragma once
#include "EspUsbWire.h"

// Internal native device engine API using the owned USB wire definitions.
// This contract does not expose vendor stack initialization or descriptor macros.
extern "C" {
EspUsbSpeed espusb_device_speed();
bool espusb_device_mounted();
bool espusb_device_suspended();
bool espusb_device_disconnect();
// Private metadata/hook for adversarial core probes; not a user callback API.
bool espusb_device_events_pending();
void espusb_device_event_hook(uint8_t port, uint32_t kind, bool inIsr);
// Worker-only replies, valid for the currently dispatched SETUP/DATA request.
bool espusb_device_control_transfer(uint8_t port, const EspUsbControlRequest *, void *, uint16_t bytes);
bool espusb_device_control_status(uint8_t port, const EspUsbControlRequest *);
bool espusb_device_vendor_control(uint8_t port, uint8_t stage, const EspUsbControlRequest *);
void espusb_device_control_acknowledged(uint8_t port, const EspUsbControlRequest *);
const uint8_t *espusb_device_descriptor_device();
const uint8_t *espusb_device_descriptor_configuration(uint8_t index);
const uint16_t *espusb_device_descriptor_string(uint8_t index, uint16_t language);
const uint8_t *espusb_device_descriptor_bos();
const uint8_t *espusb_device_descriptor_device_qualifier();
const uint8_t *espusb_device_descriptor_other_speed_configuration(uint8_t index);
void espusb_device_on_mount();
void espusb_device_on_unmount();
}
inline bool espusb_device_ready() {
  return espusb_device_mounted() && !espusb_device_suspended();
}
