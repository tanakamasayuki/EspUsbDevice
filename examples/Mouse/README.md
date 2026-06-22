# EspUsbDevice Mouse

> 日本語版: [README.ja.md](README.ja.md)

Creates a USB HID boot mouse device and sends relative movement, wheel deltas,
and button state to a USB host.

## Hardware

- ESP32-S3 or another Arduino-ESP32 board with USB device support
- A USB host such as a PC, another ESP32 running EspUsbHost, or a test fixture
- A separate Serial monitor connection for logs when available

## What It Does

- Starts `EspUsbDevice` as a full-speed HID mouse
- Sends relative `x` / `y` movement deltas
- Sends wheel movement
- Maintains mouse button state locally so press/release calls match Host-side
  `buttons` and `previousButtons` event fields
- Demonstrates left-button press and release

## Key APIs

- `mouse.move(x, y)` sends relative movement.
- `mouse.move(x, y, wheel, buttons)` sends a full boot mouse report and replaces
  the current button state.
- `mouse.wheel(delta)` sends wheel movement while preserving current buttons.
- `mouse.press(buttons)` sets one or more button bits and sends the new state.
- `mouse.release(buttons)` clears one or more button bits and sends the new
  state.
- `mouse.releaseAll()` clears all button bits.
- `mouse.click(button)` sends press, delay, and release.
- `mouse.buttons()` returns the current device-side button bitmask.
- `mouse.sendReport(report)` sends a raw boot mouse report.

## Button Bits

| Constant | Bit |
|----------|-----|
| `ESP_USB_DEVICE_MOUSE_LEFT` | `0x01` |
| `ESP_USB_DEVICE_MOUSE_RIGHT` | `0x02` |
| `ESP_USB_DEVICE_MOUSE_MIDDLE` | `0x04` |
| `ESP_USB_DEVICE_MOUSE_BACK` | `0x08` |
| `ESP_USB_DEVICE_MOUSE_FORWARD` | `0x10` |

## Expected Serial Output

```text
USB mouse ready
MOUSE_FAILED error=ESP_ERR_TIMEOUT
```

The example only prints failures. Movement and button behavior should be
observed on the USB host.

## See Also

- [KeyboardMouse](../KeyboardMouse/) - composite keyboard and mouse device
- [Keyboard](../Keyboard/) - HID keyboard device
