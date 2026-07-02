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

## Run Notes

After an `EspUsbHost` release upgrade or after switching between `s3_peer_host`
and `s3_peer_local`, stale build cache can look like boot-time crashes or
timeouts. Use `--clean` for release validation.

```sh
uv run --env-file .env pytest peer/ --profile=s3_peer_host --clean
```

## Initial Tests

- `hid_keyboard`: raw boot keyboard report and LED output report. Passing on the two-board S3 setup.
- `hid_mouse`: raw boot mouse report. Move, wheel, left, right, middle, back,
  and forward pass through the mouse callback on the two-board S3 setup.
- `hid_keyboard_mouse`: composite keyboard + mouse device. Passing on the
  two-board S3 setup.
- `custom_hid`: fixed custom report descriptor and raw input.
- `hid_vendor`: interrupt IN/OUT and feature report.
- `usb_serial`: CDC ACM serial. Device -> Host, Host -> Device, and line
  coding callbacks pass on the two-board S3 setup.
- `usb_midi`: USB MIDI. Channel voice messages and short Host -> Device SysEx
  packet splitting pass on the two-board S3 setup.
- `usb_msc`: USB Mass Storage. Single-LUN RAM disk capacity, inquiry, read,
  write, and error paths pass on the two-board S3 setup.
- `usb_vendor`: vendor-specific interface. Interface / bulk endpoint
  enumeration, bulk echo, application vendor control IN/OUT, and WebUSB landing
  URL reads pass on the two-board S3 setup.
- `usb_audio`: USB Audio speaker sink. Host -> Device speaker PCM reception
  passes on the two-board S3 setup.

Audio follow-up work remains for loopback, microphone path, long playback, and
real speaker-output checks.
