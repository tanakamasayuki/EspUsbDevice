#include "EspUsbDeviceAppDriver.h"

#if defined(SOC_USB_OTG_SUPPORTED) && SOC_USB_OTG_SUPPORTED

#include <stddef.h>

static const usbd_class_driver_t *g_appDrivers = nullptr;
static uint8_t g_appDriverCount = 0;

void espUsbDeviceRegisterAppDrivers(const usbd_class_driver_t *drivers, uint8_t count)
{
  g_appDrivers = count > 0 ? drivers : nullptr;
  g_appDriverCount = drivers ? count : 0;
}

// Strong override of the weak hook in usbd.c, read once by tusb_init().
extern "C" usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count)
{
  *driver_count = g_appDriverCount;
  return g_appDrivers;
}

#endif
