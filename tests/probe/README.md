# Probe Tests

> 日本語: [README.ja.md](README.ja.md)

`tests/probe` is for ESP32-P4 USB bring-up and port/speed identification.
Probe sketches are not stable regression tests; they document observed hardware,
SDK, and host OS behavior.

Initial probes:

- `p4_device_fs_probe`: enumerate as a Full Speed USB device.
- `p4_device_hs_probe`: enumerate as a High Speed USB device.
- `p4_loopback_matrix_probe`: print host/device port and speed combinations.
