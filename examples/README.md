# Examples

Arduino sketches that show the basic `EspUsbDevice` usage. Sketches using this
library should not call Arduino-ESP32's `USB.begin()`, `USBHIDKeyboard`, or
`USBHIDMouse`.

## Keyboard

HID boot keyboard device example.
See [Keyboard/README.md](Keyboard/README.md) for details.

- Configure port, speed, VID/PID, and string descriptors with
  `EspUsbDeviceConfig`.
- Send US ASCII strings with `EspUsbDeviceHidKeyboard::write()`.
- Use `tapKey()` for one character and `tapUsage()` / `pressUsage()` for raw HID
  usage IDs.
- Select the same layout IDs as EspUsbHost with `setLayout()` to change
  character-to-usage mapping.
- Receive NumLock, CapsLock, ScrollLock, and other LED state with
  `onOutputReport()`.

The string wrapper uses the same keymap tables as EspUsbHost in reverse and uses
the same layout IDs.

## Mouse

HID boot mouse device example.
See [Mouse/README.md](Mouse/README.md) for details.

- Send movement, wheel, and button state with `EspUsbDeviceHidMouse::move()`.
- Use `click()` for a press/release pair.
- Left, right, middle, back, and forward are exposed as raw button bits.

## KeyboardMouse

Composite keyboard + mouse HID device example. The current composite HID
implementation uses one HID interface with report IDs.
See [KeyboardMouse/README.md](KeyboardMouse/README.md) for details.

- Keyboard report ID: `1`
- Mouse report ID: `2`
- Composite HID endpoint MPS: `16 bytes`

## Notes

- Connect the USB-device-capable ESP32-S3 or similar board to a USB host.
- Serial monitor output is only for logs. Check HID behavior through the USB
  host connection.
- This library is not designed to run together with Arduino's built-in USB
  device classes.
