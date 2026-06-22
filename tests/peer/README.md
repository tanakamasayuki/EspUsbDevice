# Peer Tests

> 日本語: [README.ja.md](README.ja.md)

`tests/peer` contains two-board automated tests.

- Host board: ESP32-S3 running EspUsbHost.
- Device board: ESP32-S3 running EspUsbDevice.

The device sketch is controlled by serial commands. This keeps the USB behavior
deterministic and makes host-side assertions stable.

## Hardware

Connect the USB data pins between the host and device boards:

| Host board | Device board |
|------------|--------------|
| GPIO19 (D-) | GPIO19 (D-) |
| GPIO20 (D+) | GPIO20 (D+) |
| GND | GND |

Avoid tying VBUS together when both boards are powered separately from the host
PC.

## Initial Tests

- `hid_keyboard`: raw boot keyboard report and LED output report. Passing on the two-board S3 setup.
- `hid_mouse`: raw boot mouse report.
- `hid_keyboard_mouse`: composite keyboard + mouse device.
- `custom_hid`: fixed custom report descriptor and raw input.
- `hid_vendor`: interrupt IN/OUT and feature report.

Later phases add consumer control, system control, gamepad, CDC ACM, MIDI, MSC,
and Audio.
