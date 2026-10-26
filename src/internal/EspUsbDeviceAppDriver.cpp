#include "EspUsbDeviceAppDriver.h"

#if defined(SOC_USB_OTG_SUPPORTED) && SOC_USB_OTG_SUPPORTED

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static usbd_class_driver_t *g_appDrivers = nullptr;
static const usbd_class_driver_t **g_sources = nullptr;
static uint8_t g_appDriverCount = 0;

bool espUsbDeviceRegisterAppDrivers(const usbd_class_driver_t *drivers, uint8_t count)
{
  if (!drivers || !count || unsigned(g_appDriverCount) + count > UINT8_MAX) return false;
  auto *table = static_cast<usbd_class_driver_t *>(malloc((g_appDriverCount + count) * sizeof(usbd_class_driver_t)));
  auto **sources = static_cast<const usbd_class_driver_t **>(malloc((g_appDriverCount + count) * sizeof(const usbd_class_driver_t *)));
  if (!table || !sources) { free(table); free(sources); return false; }
  if (g_appDriverCount) {
    memcpy(table, g_appDrivers, g_appDriverCount * sizeof(*table));
    memcpy(sources, g_sources, g_appDriverCount * sizeof(*sources));
  }
  for (uint8_t i = 0; i < count; ++i) {
    table[g_appDriverCount + i] = drivers[i];
    sources[g_appDriverCount + i] = &drivers[i];
  }
  free(g_appDrivers); free(g_sources);
  g_appDrivers = table; g_sources = sources; g_appDriverCount += count;
  return true;
}

void espUsbDeviceUnregisterAppDrivers(const usbd_class_driver_t *drivers, uint8_t count)
{
  for (uint8_t i = 0; i < count; ++i) {
    for (uint8_t j = 0; j < g_appDriverCount; ++j) {
      if (g_sources[j] != &drivers[i]) continue;
      memmove(g_appDrivers + j, g_appDrivers + j + 1, (g_appDriverCount - j - 1) * sizeof(*g_appDrivers));
      memmove(g_sources + j, g_sources + j + 1, (g_appDriverCount - j - 1) * sizeof(*g_sources));
      --g_appDriverCount;
      break;
    }
  }
  if (!g_appDriverCount) {
    free(g_appDrivers); free(g_sources);
    g_appDrivers = nullptr; g_sources = nullptr;
  }
}

// Strong override of the weak hook in usbd.c, read once by tusb_init().
extern "C" usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count)
{
  *driver_count = g_appDriverCount;
  return g_appDrivers;
}

#endif
