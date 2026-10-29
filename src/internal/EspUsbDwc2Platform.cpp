// ESP integration for the native device engine. Controller limits, USB_WRAP
// handling and cache policy are adapted from TinyUSB dwc2_esp32.h:
// Copyright (c) 2021 Ha Thach, MIT; see third_party/tinyusb/LICENSE.
#include "EspUsbDwc2Platform.h"
#include "EspUsbController.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "soc/periph_defs.h"
#include "soc/usb_wrap_struct.h"
#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE
#include "esp_cache.h"
static_assert(CONFIG_CACHE_L1_CACHE_LINE_SIZE == 64, "USB DMA storage isolates 64-byte cache lines");
#endif

extern "C" {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
const EspUsbDwc2Port espusb_dwc2_ports[] = {
  {0x50040000, ETS_USB_OTG11_CH0_INTR_SOURCE, 7, 5, 256, false, true},
  {0x50000000, ETS_USB_OTG_INTR_SOURCE, 16, 8, 1024, true, false},
};
#else
const EspUsbDwc2Port espusb_dwc2_ports[] = {
  {0x60080000, ETS_USB_INTR_SOURCE, 7, 5, 256, false, true},
};
#endif
}
namespace {
intr_handle_t activeIrq = nullptr;
uint8_t activePort = 255;
void dispatch(void *argument) { espusb_controller_int_handler(uint8_t(uintptr_t(argument))); }
#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE
bool synchronize(const void *address, uint32_t bytes, int flags) {
  if (!address || !bytes) return false;
  const uint32_t rounded = (bytes + 63u) & ~63u;
  return esp_cache_msync(const_cast<void *>(address), rounded, flags | ESP_CACHE_MSYNC_FLAG_TYPE_DATA) == ESP_OK;
}
#endif
}
extern "C" {
__attribute__((weak)) esp_err_t espusb_dwc2_allocate_interrupt(int source, int flags,
    intr_handler_t handler, void *context, intr_handle_t *result) {
  return esp_intr_alloc(source, flags, handler, context, result);
}
bool espusb_dwc2_enable_irq(uint8_t port) {
  const auto *info = espusb_dwc2_port(port);
  if (!info) return false;
  if (activeIrq) return activePort == port && esp_intr_enable(activeIrq) == ESP_OK;
  intr_handle_t handle = nullptr;
  const auto error = espusb_dwc2_allocate_interrupt(info->irqnum, ESP_INTR_FLAG_LOWMED,
      dispatch, reinterpret_cast<void *>(uintptr_t(port)), &handle);
  if (error != ESP_OK || !handle) {
    if (handle) esp_intr_free(handle);
    return false;
  }
  activeIrq = handle; activePort = port;
  return true;
}
void espusb_dwc2_mask_irq(uint8_t port) {
  if (activeIrq && activePort == port) esp_intr_disable(activeIrq);
}
void espusb_dwc2_release_irq(uint8_t port) {
  if (activeIrq && activePort == port) {
    auto handle = activeIrq;
    activeIrq = nullptr; activePort = 255;
    esp_intr_free(handle);
  }
}
void espusb_dwc2_set_pulls(uint8_t port, bool connected) {
  const auto *info = espusb_dwc2_port(port);
  if (!info || !info->usesUsbWrap) return;
  usb_wrap_otg_conf_reg_t config = {};
  config.val = USB_WRAP.otg_conf.val;
  config.pad_pull_override = !connected;
  config.dp_pullup = config.dm_pullup = 0;
  config.dp_pulldown = config.dm_pulldown = !connected;
  USB_WRAP.otg_conf.val = config.val;
}
void espusb_dwc2_wakeup_delay(void) { esp_rom_delay_us(2000); }
#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE
bool espusb_controller_dcache_clean(const void *p, uint32_t n) {
  return synchronize(p, n, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}
bool espusb_controller_dcache_invalidate(const void *p, uint32_t n) {
  return synchronize(p, n, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
}
bool espusb_controller_dcache_clean_invalidate(const void *p, uint32_t n) {
  return synchronize(p, n, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_DIR_M2C);
}
#else
__attribute__((weak)) bool espusb_controller_dcache_clean(const void *, uint32_t) { return true; }
__attribute__((weak)) bool espusb_controller_dcache_invalidate(const void *, uint32_t) { return true; }
__attribute__((weak)) bool espusb_controller_dcache_clean_invalidate(const void *, uint32_t) { return true; }
#endif
}
