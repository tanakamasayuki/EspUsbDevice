#pragma once

// TinyUSB's pinned upstream sources include this conventional filename.
// EspUsbDevice owns the actual configuration and does not include the
// Arduino-ESP32 arduino_tinyusb component's tusb_config.h.
#include "internal/EspUsbTinyUsbConfig.h"
