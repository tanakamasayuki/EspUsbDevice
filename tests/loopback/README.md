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
- `custom_hid`: starts a custom HID report descriptor device on one P4 and
  verifies descriptor length and raw reports through Host `onHIDReportDescriptor()`
  and `onHIDInput()`.
- `hid_vendor`: starts a HID vendor device on one P4 and verifies Device -> Host
  input, Host -> Device feature reports, and Host -> Device output reports.
- `hid_consumer_control`: starts a consumer control device on one P4 and verifies
  volume / media-key press and release events through Host `onConsumerControl()`.
- `hid_system_control`: starts a system control device on one P4 and verifies
  power / standby / wake press and release events through Host `onSystemControl()`.
- `hid_keyboard_layout`: starts a keyboard device on one P4, switches the Host and
  Device keyboard layout together, and verifies that `EN_US` and `JA_JP` symbol keys
  arrive as the same ASCII through Host `onKeyboard()`.
- `usb_serial`: starts CDC ACM serial on one P4 and verifies Device -> Host,
  Host -> Device, and line coding callbacks.
- `usb_midi`: starts USB MIDI on one P4 and verifies channel voice messages and
  short Host -> Device SysEx packet splitting.
- `usb_msc`: starts USB Mass Storage on one P4 and verifies single-LUN RAM disk
  capacity, inquiry, read, write, and error paths.
- `usb_vendor`: starts a vendor-specific interface on one P4 and verifies bulk
  echo, application control IN/OUT, and WebUSB landing URL reads.
- `usb_audio`: intentionally omitted. On P4 this library's audio is UAC2 / high
  speed only, but one-board loopback can only run at full speed (see the P4 note
  below), so P4 audio cannot be exercised in loopback. Audio is covered by
  `peer/usb_audio` (S3, UAC1) plus manual high-speed checks.

## P4 port / PHY reality (verified 2026-07)

P4 has two USB OTG controllers but only one UTMI (high-speed) PHY
(`SOC_USB_OTG_PERIPH_NUM=2`, `SOC_USB_UTMI_PHY_NUM=1`). The Arduino core pins the
device stack to the HS/UTMI PHY (`EspUsbDeviceConfig.port`/`speed` are not wired to
`tinyusb_init`). Speed and controller are separate: the HS-capable device still
negotiates **Full Speed** when the link partner is a full-speed host.

Consequences for one-board loopback:

- The device sits on the HS/UTMI PHY but runs at FS against an FS host, and it
  enumerates fine.
- The single UTMI PHY is held by the device, so the host must use
  `ESP_USB_HOST_PORT_FULL_SPEED`. An HS host collides on the PHY
  (`usb_phy: selected PHY is in use`).
- Therefore an HS link is impossible on one board; HS-link validation needs the
  two-board peer setup.

## Matrix

| Device | Host | Expected |
|--------|------|----------|
| HS(UTMI) device, FS operation | FS host | The realizable one-board loopback. Device is HS-PHY-fixed but negotiates FS. |
| HS device | HS host | Not possible on one P4 — the single UTMI PHY cannot be shared (PHY conflict). Use two boards for an HS link. |

> No audio loopback on P4: this library's audio is UAC2 / high speed on P4
> (`TUD_OPT_HIGH_SPEED`), but the one-board loopback link is full speed, so the
> two are fundamentally incompatible. P4 audio is therefore HS-only and validated
> outside loopback. See `docs/DESIGN_NOTES.ja.md` "P4 USB ポート/PHY の実測整理".
