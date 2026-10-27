#pragma once

// Selected first-party class drivers. The independent core snapshots this
// table at start; there is no built-in TinyUSB class table or fallback.
// Registration does not reference unused classes, keeping their code out of
// sketches that do not select them. Wire types remain shared with the DWC2
// bridge; the dispatch layout and registration lifecycle belong to this core.

#include <stdint.h>

#if __has_include("soc/soc_caps.h")
#include "soc/soc_caps.h"
#endif

#if defined(SOC_USB_OTG_SUPPORTED) && SOC_USB_OTG_SUPPORTED

#include "EspUsbWire.h"

namespace espusb { namespace internal {
// Control/transfer callbacks run on the device worker; sof runs in interrupt context.
// There is deliberately no class transfer callback in the hardware ISR.
struct UsbClassDriver {
  void (*init)();
  bool (*deinit)();
  void (*reset)(uint8_t port);
  uint16_t (*open)(uint8_t port, const EspUsbInterfaceDescriptor *, uint16_t available);
  bool (*control_xfer_cb)(uint8_t port, uint8_t stage, const EspUsbControlRequest *);
  bool (*xfer_cb)(uint8_t port, uint8_t address, EspUsbControllerResult, uint32_t bytes);
  void (*sof)(uint8_t port, uint32_t frame);
};
// Borrowed until stop; registration/removal must not race an active core.
const UsbClassDriver *selectedUsbClassDrivers(uint8_t &count);
} }

// Append only selected drivers before stack start. Storage is sized to the
// registered entries; no global references to class implementations. The source
// table identifies entries during removal and must outlive registration.
// Unregister only after the stack has stopped retaining the combined table.
bool espUsbDeviceRegisterAppDrivers(const espusb::internal::UsbClassDriver *drivers, uint8_t count);
void espUsbDeviceUnregisterAppDrivers(const espusb::internal::UsbClassDriver *drivers, uint8_t count);

#endif
