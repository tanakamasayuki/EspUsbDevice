#pragma once
// Bulk max packet size of the link these probes run on. EspUsbController::Auto
// and the Host's default port select the high-speed controller on the P4 (HS
// direct link of the p4_p4x_hs/p4x_p4_hs combos) and full speed elsewhere.
#if CONFIG_IDF_TARGET_ESP32P4
constexpr unsigned PROBE_BULK_MPS = 512;
#else
constexpr unsigned PROBE_BULK_MPS = 64;
#endif
