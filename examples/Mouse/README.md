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

## Walkthrough and checks

### Before running

Send relative pointer motion, wheel input and button transitions. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Use a test area because movement starts automatically. `move()` supplies deltas, not screen coordinates; `press()` leaves a button held until `release()`. The sketch logs failure but does not implement a general pending-release queue, so an application must explicitly recover its current button state.

### API use and adaptation

A HID report is a structured state or change, not an arbitrary text line. Decode it with its report descriptor: distinguish report ID, usage page/usage, signed value and logical range. Keyboard usages identify key positions, mouse motion is relative, and button values are bitmaps. Match press with release, or compare complete states, before adding an application action.

Callback report data is borrowed. Copy the bytes or decoded values into owned storage before handing work to another task. Log only short diagnostics in the callback; for repeated raw dumps, terminal speed can become the bottleneck. If an input is missing, first confirm enumeration, then descriptor/report length, then application decoding. Do not change all three at once.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](Mouse.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
