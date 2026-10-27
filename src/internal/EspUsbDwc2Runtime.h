#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
// Storage belongs to the independent core. Masks include EP0 and every
// descriptor alternate; allocate no transfer record for unselected directions.
// Caller supplies an internal DMA block aligned to 64 bytes. Release only
// after controller/ISR shutdown, before freeing that block. Prepare synchronizes
// the SETUP DMA cache line and leaves no active state if synchronization fails.
size_t espusb_dwc2_runtime_size(uint8_t port, uint16_t out_mask, uint16_t in_mask);
bool espusb_dwc2_runtime_prepare(uint8_t port, void *memory, size_t bytes, uint16_t out_mask, uint16_t in_mask);
void espusb_dwc2_runtime_release(void);
// Private metadata for resource probes; zero before begin/after end.
size_t espusb_dwc2_runtime_bytes(void);
uint8_t espusb_dwc2_runtime_transfers(void);
#ifdef __cplusplus
}
#endif
