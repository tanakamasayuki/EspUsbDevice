# EspUsbDevice

EspUsbDevice is a new ESP32 Arduino USB Device library.

The goal is not compatibility with Arduino-ESP32's `USB`, `USBHIDKeyboard`, or
`USBHIDMouse` APIs. The goal is a better, small, explicit USB device library
where port, speed, descriptors, endpoint packet sizes, and raw class reports are
controlled by the sketch.

The first implementation targets `EspUsbHost` peer and loopback tests because
those tests give concrete hardware coverage and expose the low-level behavior
the library must control. Test-oriented features are the starting point, not the
final boundary of the project.

## Design Goals

- Use EspUsbHost-style explicit configuration and callback APIs.
- Own USB descriptors in this library instead of relying on Arduino USB class
  descriptors.
- Treat HID usage IDs and raw reports as the primary API.
- Support ESP32-S3 two-board peer tests and ESP32-P4 one-board loopback tests as
  early validation targets.
- Keep Arduino-ESP32's standard USB device stack mutually exclusive. Sketches
  using this library must not call `USB.begin()`.

## Initial Scope

The first milestone is the HID-focused MVP needed to replace existing EspUsbHost
peer devices and validate the core API on real hardware:

- Device port/speed/VID/PID/string/power configuration.
- Speed-aware descriptor generation and endpoint MPS selection.
- HID boot keyboard raw report sending.
- HID keyboard output report callback for LED state.
- HID boot mouse raw report sending.
- Serial command sketches for pytest-embedded peer tests.

## Examples

User-facing sketches are documented in [examples/README.md](examples/README.md).

- `Keyboard`: boot keyboard that sends layout-aware ASCII strings and HID usage IDs.
- `Mouse`: boot mouse that sends movement, wheel, and buttons.
- `KeyboardMouse`: composite keyboard + mouse HID.

See [tests/TEST_PLAN.md](tests/TEST_PLAN.md) for the test structure and staged
coverage plan.
Design background and migration notes from existing EspUsbHost tests are in
[docs/DESIGN_NOTES.ja.md](docs/DESIGN_NOTES.ja.md).
The development order based on the currently verified hardware and tool
environment is in [docs/DEVELOPMENT_PLAN.ja.md](docs/DEVELOPMENT_PLAN.ja.md).
