# EspUsbDevice Gamepad

> 日本語版: [README.ja.md](README.ja.md)

Creates a USB HID gamepad device and sends axes, hat switch, and button state to
the host.

## Hardware

- ESP32-S3 or another Arduino-ESP32 board with USB device support
- A USB host such as a PC, another ESP32 running EspUsbHost, or a test fixture
- A separate Serial monitor connection for logs when available

## What It Does

- Starts `EspUsbDevice` as a full-speed HID gamepad device
- Changes axis values every 500 ms
- Sweeps the hat switch through center and 8 directions
- Sends buttons 1 through 8 as a rotating button bit

## Key APIs

- `EspUsbDeviceHidGamepad gamepad(device)` registers the HID gamepad function.
- `gamepad.send(x, y, z, rz, rx, ry, hat, buttons)` sends axes, hat, and button
  bitmask together.
- `gamepad.sendReport(report)` sends a raw `EspUsbDeviceGamepadReport`.
- `gamepad.releaseAll()` returns axes, hat, and buttons to neutral state.

## Report

- Axes: `x`, `y`, `z`, `rz`, `rx`, `ry`
- Axis range: `-127` to `127`
- Hat: `ESP_USB_DEVICE_GAMEPAD_HAT_CENTER` or 8 directions
- Buttons: 32-bit bitmask
- Report ID: `ESP_USB_DEVICE_HID_REPORT_ID_GAMEPAD`

## Hat Constants

| Constant | Value |
|----------|-------|
| `ESP_USB_DEVICE_GAMEPAD_HAT_CENTER` | `0` |
| `ESP_USB_DEVICE_GAMEPAD_HAT_UP` | `1` |
| `ESP_USB_DEVICE_GAMEPAD_HAT_UP_RIGHT` | `2` |
| `ESP_USB_DEVICE_GAMEPAD_HAT_RIGHT` | `3` |
| `ESP_USB_DEVICE_GAMEPAD_HAT_DOWN_RIGHT` | `4` |
| `ESP_USB_DEVICE_GAMEPAD_HAT_DOWN` | `5` |
| `ESP_USB_DEVICE_GAMEPAD_HAT_DOWN_LEFT` | `6` |
| `ESP_USB_DEVICE_GAMEPAD_HAT_LEFT` | `7` |
| `ESP_USB_DEVICE_GAMEPAD_HAT_UP_LEFT` | `8` |

## How To Check

- Use the PC game controller settings screen to inspect axes / hat / buttons.
- With EspUsbHost, observe values in the `onGamepad()` callback.
- Serial monitor prints a summary of each sent report.

## See Also

- [KeyboardMouse](../KeyboardMouse/) - composite keyboard and mouse HID
- [Mouse](../Mouse/) - HID mouse device
