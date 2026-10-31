# EspUsbDevice Gamepad

> 日本語版: [README.ja.md](README.ja.md)

Creates a USB HID gamepad and sends axes, hat switch and button state to the Host.

## Hardware

- ESP32-S3, ESP32-S2 or ESP32-P4 board; the native USB OTG port goes to the Host
- A USB Host such as a PC, another ESP32 running EspUsbHost, or a test fixture
- A separate debug connection for `Serial` (115200) logs

## What It Does

- Starts `EspUsbDevice` as an HID gamepad with its own VID/PID/strings
- While the Host has the device configured, every 500 ms:
  - steps the axes through a sine-like table (`rx`/`ry` mirror `x`/`y`)
  - sweeps the hat switch through center and 8 directions
  - sends buttons 1 through 8 as a rotating button bit
- Prints a summary of each sent report

## Key APIs

- `EspUsbDeviceHidGamepad gamepad(device)` registers the HID gamepad Function.
- `gamepad.send(x, y, z, rz, rx, ry, hat, buttons)` sends axes, hat and button
  bitmask together.
- `gamepad.sendReport(report)` sends a raw `EspUsbDeviceGamepadReport`.
- `gamepad.releaseAll()` returns axes, hat and buttons to the neutral state.

## Report

- Axes: `x`, `y`, `z`, `rz`, `rx`, `ry`
- Axis range: `-127` to `127`
- Hat: `ESP_USB_DEVICE_GAMEPAD_HAT_CENTER` (no direction) or 8 directions
- Buttons: 32-bit bitmask of all held buttons, not a button number
- Report ID: `ESP_USB_DEVICE_HID_REPORT_ID_GAMEPAD` when combined with other HID Functions

## Hat Constants

| Constant | Value | Direction |
|----------|-------|-----------|
| `ESP_USB_DEVICE_GAMEPAD_HAT_CENTER` | `0` | none (null state) |
| `ESP_USB_DEVICE_GAMEPAD_HAT_UP` | `1` | 0° |
| `ESP_USB_DEVICE_GAMEPAD_HAT_UP_RIGHT` | `2` | 45° |
| `ESP_USB_DEVICE_GAMEPAD_HAT_RIGHT` | `3` | 90° |
| `ESP_USB_DEVICE_GAMEPAD_HAT_DOWN_RIGHT` | `4` | 135° |
| `ESP_USB_DEVICE_GAMEPAD_HAT_DOWN` | `5` | 180° |
| `ESP_USB_DEVICE_GAMEPAD_HAT_DOWN_LEFT` | `6` | 225° |
| `ESP_USB_DEVICE_GAMEPAD_HAT_LEFT` | `7` | 270° |
| `ESP_USB_DEVICE_GAMEPAD_HAT_UP_LEFT` | `8` | 315° |

## Expected Serial Output

```text
USB gamepad ready
GAMEPAD_TX x=0 y=127 z=0 rz=-127 hat=0 buttons=0x00000001
GAMEPAD_TX x=64 y=64 z=-64 rz=-64 hat=1 buttons=0x00000002
```

## How To Check

- Use the PC game controller settings screen to inspect axes / hat / buttons.
- With EspUsbHost, observe the values in the `onGamepad()` callback.
- Compare one printed step with the `send()` arguments.

This demo has no physical joystick. Adding one needs scaling, center calibration
and an explicit neutral state (`releaseAll()`).

## Changes from v2

- Reports are sent only while `device.ready()` is true.
- The API (`send()`, `sendReport()`, `releaseAll()`) and the hat constants are
  unchanged, and the report stays the same size on the wire.
- The report descriptor is corrected: the hat's logical range is now 1-8 with a
  null state, so `HAT_CENTER` (0) means "no direction". In v2 the range was 0-8,
  so center was reported as a direction and the 8 directions did not map to
  0-315° (UP was about 39°). Host software that compensated for that should be
  rechecked.

## See Also

- [KeyboardMouse](../KeyboardMouse/) - composite keyboard and mouse HID
- [MediaKeys](../MediaKeys/) - consumer and system control
- [HID API guide](../../docs/classes/hid.md)
- [All examples](../README.md)
