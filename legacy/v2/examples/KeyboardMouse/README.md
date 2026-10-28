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

## Walkthrough and checks

### Before running

Combine keyboard and mouse reports on one HID device. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Both function objects register on the same device before `begin()`. Type and motion are generated automatically: use a scratch document and compare the class report IDs with the descriptor. Adding a second function changes the report layout; do not reuse assumptions from a single-function raw HID client.

### API use and adaptation

A HID report is a structured state or change, not an arbitrary text line. Decode it with its report descriptor: distinguish report ID, usage page/usage, signed value and logical range. Keyboard usages identify key positions, mouse motion is relative, and button values are bitmaps. Match press with release, or compare complete states, before adding an application action.

Callback report data is borrowed. Copy the bytes or decoded values into owned storage before handing work to another task. Log only short diagnostics in the callback; for repeated raw dumps, terminal speed can become the bottleneck. If an input is missing, first confirm enumeration, then descriptor/report length, then application decoding. Do not change all three at once.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](KeyboardMouse.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
