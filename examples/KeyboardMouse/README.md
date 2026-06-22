# EspUsbDevice KeyboardMouse

> 日本語版: [README.ja.md](README.ja.md)

Creates a composite USB HID device that exposes keyboard and mouse reports from
one ESP32.

## Hardware

- ESP32-S3 or another Arduino-ESP32 board with USB device support
- A USB host such as a PC, another ESP32 running EspUsbHost, or a test fixture
- A separate Serial monitor connection for logs when available

## What It Does

- Starts `EspUsbDevice` with both `EspUsbDeviceHidKeyboard` and
  `EspUsbDeviceHidMouse`
- Sends a keyboard character with the layout-aware keyboard wrapper
- Sends mouse movement and a left-click
- Demonstrates the composite HID descriptor used by this library

## Composite HID Shape

The current composite HID implementation uses one HID interface with report IDs.
This keeps the runtime compatible with the Arduino-ESP32 TinyUSB integration
used by this library.

| Report | Report ID |
|--------|-----------|
| Keyboard | `1` |
| Mouse | `2` |

The composite HID interrupt endpoint MPS is `16 bytes` so the report-ID-prefixed
keyboard report fits in one packet.

## Key APIs

- Construct multiple class objects with the same `EspUsbDevice` instance:
  `EspUsbDeviceHidKeyboard keyboard(device);`
  `EspUsbDeviceHidMouse mouse(device);`
- Call `device.begin(config)` once after all class objects are constructed.
- Use the keyboard APIs from [Keyboard](../Keyboard/).
- Use the mouse APIs from [Mouse](../Mouse/).

## Expected Serial Output

```text
USB keyboard + mouse ready
KEY_FAILED error=ESP_ERR_TIMEOUT
MOVE_FAILED error=ESP_ERR_TIMEOUT
CLICK_FAILED error=ESP_ERR_TIMEOUT
```

The example only prints failures after startup. Keyboard and mouse behavior
should be observed on the USB host.

## See Also

- [Keyboard](../Keyboard/) - HID keyboard device
- [Mouse](../Mouse/) - HID mouse device
