#pragma once

// Registry for the TinyUSB "application" class drivers this library adds on top
// of the vendored built-ins (usbd_app_driver_get_cb). The vendored TinyUSB under
// src/ is upstream-verbatim, so a class TinyUSB does not implement - CCID today -
// cannot be added to its driver table and registers here instead.
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

// Register (or, with drivers == nullptr, unregister) the application driver
// table. Must be called before the TinyUSB device stack is started, i.e. from a
// class's begin(). `drivers` must stay valid while the stack runs.
void espUsbDeviceRegisterAppDrivers(const usbd_class_driver_t *drivers, uint8_t count);

#endif
