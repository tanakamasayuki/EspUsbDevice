#pragma once

// Registry for the TinyUSB "application" class drivers this library adds on top
// of the vendored built-ins (usbd_app_driver_get_cb). The vendored TinyUSB under
// src/ supplies the interim core. Bulk streams (CDC/Vendor) and CCID register
// independent class drivers instead of referencing its built-in table.
//
// The indirection exists for footprint: usbd_app_driver_get_cb is reachable from
// usbd.c and therefore always linked, so if it named the CCID driver directly it
// would pull the whole driver into every sketch. Here it only reads a pointer,
// and the driver is reachable only from the class that registers it - which the
// linker drops when no sketch instantiates that class.

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
