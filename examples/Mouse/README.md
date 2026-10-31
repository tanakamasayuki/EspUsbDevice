# EspUsbDevice Mouse

> 日本語版: [README.ja.md](README.ja.md)

Creates a USB HID boot mouse and sends relative movement, wheel deltas and
button state to a USB Host.

## Hardware

- ESP32-S3, ESP32-S2 or ESP32-P4 board; the native USB OTG port goes to the Host
- A USB Host such as a PC, another ESP32 running EspUsbHost, or a test fixture
- A separate debug connection for `Serial` (115200) logs

## What It Does

- Starts `EspUsbDevice` as an HID mouse with its own VID/PID/strings
- Once the Host has configured the device, one step per second:
  right, down, left, up (30 counts each), wheel +1, left-button press and release
- Keeps the button state on the device so press/release calls match Host-side
  `buttons` and `previousButtons` event fields
- Prints failures with `device.lastErrorName()` and the current button bitmask

Movement starts as soon as the Host configures the device. Use a test area.

## Key APIs

- `mouse.move(x, y)` sends relative movement and keeps the buttons held by
  `press()`, so `press()` then `move()` drags.
- `mouse.move(x, y, wheel, buttons)` with `buttons` 0-255 sets the held buttons,
  as `press()`/`release()` would. The default `EspUsbDeviceHidMouse::KeepButtons`
  (-1) keeps them. A value outside -1..255 fails with `ESP_ERR_INVALID_ARG`.
- `mouse.wheel(delta)` sends wheel movement while keeping the current buttons.
- `mouse.press(buttons)` sets one or more button bits and sends the new state.
- `mouse.release(buttons)` clears one or more button bits and sends the new state.
- `mouse.releaseAll()` clears all button bits.
- `mouse.click(button)` sends press, a short hold, and release.
- `mouse.buttons()` returns the current device-side button bitmask.
- `mouse.sendReport(report)` sends a raw `EspUsbDeviceBootMouseReport`;
  `mouse.sendState()` resends the current buttons with zero movement.

In report protocol the device sends 4 bytes (buttons, X, Y, wheel); in boot
protocol it converts to the 3-byte boot format. X/Y/wheel are reset to 0 after
a successful send, so a Host idle repeat does not repeat the movement.

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
MOUSE_FAILED ESP_ERR_TIMEOUT buttons=0x00
```

The sketch prints only failures. Watch movement and buttons on the Host.

## Checks

- `move()` takes deltas, not screen coordinates.
- `press()` leaves a button held until `release()`. The sketch always sends the
  release, even if the press failed; it does not implement a general pending-release
  queue, so an application must recover its own button state.
- A true result means the report was copied and queued, not that the Host read it.

## Changes from v2

- Reports are sent only while `device.ready()` is true, so a missing Host no
  longer produces a failure every second.
- The release is sent even when the press failed, and the failure log includes
  `mouse.buttons()`.
- `move(x, y)` without `buttons` now keeps the held buttons. v2 defaulted
  `buttons` to 0, so a move released any held button; pass 0 explicitly for that.
- `wheel()`, `press()`, `release()`, `click()`, `buttons()` and
  `sendReport()`/`sendState()` are unchanged. Relative values are cleared after
  a successful send, so a Host idle repeat sends zero movement.

## See Also

- [KeyboardMouse](../KeyboardMouse/) - composite keyboard and mouse
- [Keyboard](../Keyboard/) - HID keyboard
- [HID API guide](../../docs/classes/hid.md)
- [All examples](../README.md)
