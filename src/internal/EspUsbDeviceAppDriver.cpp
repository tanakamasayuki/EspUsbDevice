#include "EspUsbDeviceAppDriver.h"
#include "EspUsbMemory.h"

#if defined(SOC_USB_OTG_SUPPORTED) && SOC_USB_OTG_SUPPORTED

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static espusb::internal::UsbClassDriver *g_appDrivers = nullptr;
static const espusb::internal::UsbClassDriver **g_sources = nullptr;
static uint8_t g_appDriverCount = 0;

bool espUsbDeviceRegisterAppDrivers(const espusb::internal::UsbClassDriver *drivers, uint8_t count)
{
  if (!drivers || !count || unsigned(g_appDriverCount) + count > UINT8_MAX) return false;
  auto *table = static_cast<espusb::internal::UsbClassDriver *>(espusb::internal::allocateUsbMemory((g_appDriverCount + count) * sizeof(espusb::internal::UsbClassDriver)));
  auto **sources = static_cast<const espusb::internal::UsbClassDriver **>(espusb::internal::allocateUsbMemory((g_appDriverCount + count) * sizeof(const espusb::internal::UsbClassDriver *)));
  if (!table || !sources) { espusb::internal::freeUsbMemory(table); espusb::internal::freeUsbMemory(sources); return false; }
  if (g_appDriverCount) {
    memcpy(table, g_appDrivers, g_appDriverCount * sizeof(*table));
    memcpy(sources, g_sources, g_appDriverCount * sizeof(*sources));
  }
  for (uint8_t i = 0; i < count; ++i) {
    table[g_appDriverCount + i] = drivers[i];
    sources[g_appDriverCount + i] = &drivers[i];
  }
  espusb::internal::freeUsbMemory(g_appDrivers); espusb::internal::freeUsbMemory(g_sources);
  g_appDrivers = table; g_sources = sources; g_appDriverCount += count;
  return true;
}

void espUsbDeviceUnregisterAppDrivers(const espusb::internal::UsbClassDriver *drivers, uint8_t count)
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
    espusb::internal::freeUsbMemory(g_appDrivers); espusb::internal::freeUsbMemory(g_sources);
    g_appDrivers = nullptr; g_sources = nullptr;
  }
}

// The independent core borrows this table for its active lifetime.
const espusb::internal::UsbClassDriver *espusb::internal::selectedUsbClassDrivers(uint8_t &count)
{
  count = g_appDriverCount;
  return g_appDrivers;
}

#endif
