#pragma once

// Selected first-party class drivers. The independent core snapshots this
// table at start; there is no built-in TinyUSB class table or fallback.
// Registration does not reference unused classes, keeping their code out of
// sketches that do not select them. The C ABI/types are temporary until the
// remaining DWC2 and protocol-header dependency is replaced.

#include <stdint.h>

#if __has_include("soc/soc_caps.h")
#include "soc/soc_caps.h"
#endif

#if defined(SOC_USB_OTG_SUPPORTED) && SOC_USB_OTG_SUPPORTED

#include "device/usbd_pvt.h"

// Append only selected drivers before stack start. Storage is sized to the
// registered entries; no global references to class implementations. The source
// table identifies entries during removal and must outlive registration.
// Unregister only after the stack has stopped retaining the combined table.
bool espUsbDeviceRegisterAppDrivers(const usbd_class_driver_t *drivers, uint8_t count);
void espUsbDeviceUnregisterAppDrivers(const usbd_class_driver_t *drivers, uint8_t count);

#endif
