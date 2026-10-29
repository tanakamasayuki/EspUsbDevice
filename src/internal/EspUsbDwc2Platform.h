#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#include "esp_intr_alloc.h"
#include "esp_err.h"

#if defined(CONFIG_IDF_TARGET_ESP32P4)
enum { ESPUSB_DWC2_PORT_COUNT = 2 };
#elif defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
enum { ESPUSB_DWC2_PORT_COUNT = 1 };
#else
#error "EspUsbDevice DWC2 platform currently supports ESP32-S2/S3/P4"
#endif

#ifdef __cplusplus
extern "C" {
#endif
// Physical controller limits, independent of Function counts and buffers.
// New SoCs add port records here; HS need not be port 1 (e.g. future S31).
typedef struct {
  uintptr_t reg_base;
  uint32_t irqnum;
  uint8_t ep_count, ep_in_count;
  uint16_t otg_dfifo_depth;
  bool highSpeed, usesUsbWrap;
} EspUsbDwc2Port;
extern const EspUsbDwc2Port espusb_dwc2_ports[ESPUSB_DWC2_PORT_COUNT];
static inline const EspUsbDwc2Port *espusb_dwc2_port(uint8_t port) {
  return port < ESPUSB_DWC2_PORT_COUNT ? espusb_dwc2_ports + port : NULL;
}
static inline const EspUsbDwc2Port *espusb_dwc2_port_for_speed(bool highSpeed) {
  for (uint8_t port = 0; port < ESPUSB_DWC2_PORT_COUNT; ++port)
    if (espusb_dwc2_ports[port].highSpeed == highSpeed) return espusb_dwc2_ports + port;
  return NULL;
}
// One active controller owns one IRQ registration. Mask/unmask reuses it;
// release only during controller shutdown, before freeing core storage.
bool espusb_dwc2_enable_irq(uint8_t port);
void espusb_dwc2_mask_irq(uint8_t port);
void espusb_dwc2_release_irq(uint8_t port);
void espusb_dwc2_set_pulls(uint8_t port, bool connected);
void espusb_dwc2_wakeup_delay(void);
// Private SDK boundary, also used to inject registration failure in probes.
esp_err_t espusb_dwc2_allocate_interrupt(int source, int flags, intr_handler_t handler,
                                        void *context, intr_handle_t *result);
#ifdef __cplusplus
}
#endif
