#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "EspUsbWire.h"

// Only the integer/layout operations needed by the remaining DWC2 source.
#define ESPUSB_BIT(n) (UINT32_C(1) << (n))
#define ESPUSB_MASK(high, low) ((UINT32_MAX << (low)) & (UINT32_MAX >> (31 - (high))))
#define ESPUSB_PACKED __attribute__((packed))
#ifdef __cplusplus
#define ESPUSB_STATIC_ASSERT(condition, message) static_assert(condition, message)
#else
#define ESPUSB_STATIC_ASSERT(condition, message) _Static_assert(condition, message)
#endif
#define ESPUSB_REQUIRE(condition) do { if (!(condition)) return false; } while (0)
#define ESPUSB_CLEAR_MEMORY(address, bytes) ((void)memset(address, 0, bytes))
static inline uint8_t espusb_min_u8(uint8_t a, uint8_t b) { return a < b ? a : b; }
static inline uint16_t espusb_min_u16(uint16_t a, uint16_t b) { return a < b ? a : b; }
static inline uint32_t espusb_min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }
static inline uint32_t espusb_div_ceil(uint32_t n, uint32_t divisor) { return (n + divisor - 1) / divisor; }
static inline bool espusb_bit_test(uint32_t value, uint8_t bit) { return (value & ESPUSB_BIT(bit)) != 0; }
