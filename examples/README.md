# Examples

Arduino sketches that show the basic `EspUsbDevice` usage. Sketches using this
library should not call Arduino-ESP32's `USB.begin()`, `USBHIDKeyboard`, or
`USBHIDMouse`.

## Keyboard

HID boot keyboard device example.

- Configure port, speed, VID/PID, and string descriptors with
  `EspUsbDeviceConfig`.
- Send HID usage IDs with `EspUsbDeviceHidKeyboard::pressUsage()`.
- Receive NumLock, CapsLock, ScrollLock, and other LED state with
  `onOutputReport()`.

## Mouse

HID boot mouse device example.

- Send movement, wheel, and button state with `EspUsbDeviceHidMouse::move()`.
- Use `click()` for a press/release pair.
- Left, right, middle, back, and forward are exposed as raw button bits.

## KeyboardMouse

Composite keyboard + mouse HID device example. The current composite HID
implementation uses one HID interface with report IDs.

- Keyboard report ID: `1`
- Mouse report ID: `2`
- Composite HID endpoint MPS: `16 bytes`

## Notes

- Connect the USB-device-capable ESP32-S3 or similar board to a USB host.
- Serial monitor output is only for logs. Check HID behavior through the USB
  host connection.
- This library is not designed to run together with Arduino's built-in USB
  device classes.
