#pragma once
// Bulk max packet size of the link these probes run on. EspUsbController::Auto
// and the Host's default port select the high-speed controller on the P4 and
// the S31 (HS direct links) and full speed elsewhere.
#if CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31
constexpr unsigned PROBE_BULK_MPS = 512;
#else
constexpr unsigned PROBE_BULK_MPS = 64;
#endif
