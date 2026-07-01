# Loopback Tests

> 日本語: [README.ja.md](README.ja.md)

`tests/loopback` contains one-board ESP32-P4 tests where EspUsbHost and
EspUsbDevice run on the same chip.

The first target is HID keyboard loopback with descriptor logging so P4
port/speed behavior can be verified before broader class coverage is added.

## Tests

- `hid_keyboard`: starts `EspUsbHost` and `EspUsbDeviceHidKeyboard` on one P4,
  sends `hello, keyboard` from the device side, and verifies it through Host
  `onKeyboard()`. It also sends NumLock / CapsLock / ScrollLock / clear LED
  output reports from the host side and verifies Device `onOutputReport()`.
- `hid_mouse`: starts `EspUsbHost` and `EspUsbDeviceHidMouse` on one P4 and
  verifies move / wheel / left / right / middle / back / forward through Host
  `onMouse()`.
- `hid_keyboard_mouse`: starts a keyboard + mouse composite device on one P4
  and verifies that both reports reach Host callbacks.
- `usb_serial`: starts CDC ACM serial on one P4 and verifies Device -> Host,
  Host -> Device, and line coding callbacks.
- `usb_midi`: starts USB MIDI on one P4 and verifies channel voice messages and
  short Host -> Device SysEx packet splitting.
- `usb_msc`: starts USB Mass Storage on one P4 and verifies single-LUN RAM disk
  capacity, inquiry, read, write, and error paths.
- `usb_vendor`: starts a vendor-specific interface on one P4 and verifies bulk
  echo, application control IN/OUT, and WebUSB landing URL reads.

## Matrix

| Device | Host | Expected |
|--------|------|----------|
| FS device | FS host | Primary target if the SDK allows this pairing. |
| HS device | HS host | Expected baseline for P4 high-speed paths. |
| FS device | HS host | Probe/diagnostic case. |
| HS device | FS host | Expected to fail or be unsupported; document explicitly. |
