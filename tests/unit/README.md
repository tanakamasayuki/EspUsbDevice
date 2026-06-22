# Unit Tests

> 日本語: [README.ja.md](README.ja.md)

Unit tests cover host-independent logic:

- Device descriptor bytes.
- Configuration descriptor layout.
- FS/HS endpoint MPS selection.
- HID report descriptor bytes.
- HID keyboard/mouse report builders.

## `compile_smoke`

This is the first environment check. In `--run-mode=build`, it verifies Arduino
CLI, sketch.yaml, the ESP32 board package, library resolution, and minimal
public header compilation. It does not validate the USB device stack at runtime.
