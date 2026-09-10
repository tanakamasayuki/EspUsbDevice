# Loopback Tests

> 日本語版: [README.ja.md](README.ja.md)

`tests/loopback` contains one-board ESP32-P4 tests where EspUsbHost and
EspUsbDevice run on the same chip.

The first target is HID keyboard loopback with descriptor logging so P4
port/speed behavior can be verified before broader class coverage is added.

## Why these wait for a banner when `peer/` asks

Every test here opens by waiting for `HOST_DEVICE`, the line the sketch prints
once when its own host half enumerates its own device half. `tests/peer` moved
away from exactly that shape: a line printed once at enumeration is only visible
to whichever test reads it first, so the peer modules now ask (`?` ->
`DEVICE_READY <0|1>`) instead of awaiting an announcement.

That change is not needed here, and the reason is structural rather than a
judgement call. Each module in this directory is a single test with its own
sketch and its own upload, so the test that reads the banner is always the first
thing to run after the board comes up. There is no second test to be starved of
it, and no order for it to depend on.

What did bite once was timing rather than order: the sketch printed its port
report at boot, and in a full run that output could scroll past before pytest
attached to the serial port, which failed inside the suite and passed when run
alone. The fix was to move the report after enumeration so it is emitted with
`HOST_DEVICE` rather than ahead of it. If a module here ever grows a second
test, or starts reporting something before enumeration again, convert it to the
`peer/` shape rather than re-tuning the timing.

## Tests

- `hid_keyboard`: starts `EspUsbHost` and `EspUsbDeviceHidKeyboard` on one P4,
  sends `hello, keyboard` from the device side, and verifies it through Host
  `onKeyboard()`. It also sends NumLock / CapsLock / ScrollLock / clear LED
  output reports from the host side and verifies Device `onOutputReport()`.
  The test runs both controller assignments: Device HS + Host FS, then Device
  FS + Host HS.
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
- `usb_serial_multi`: starts a three-port CDC ACM device on one P4's high-speed
  controller. The descriptor unit tests stop at "the bytes are well formed", but
  three ports means six non-control IN endpoints, and every IN endpoint needs its
  own TxFIFO carved out of the controller's 1024-word data FIFO - an allocation
  dcd_dwc2 only performs at SET_CONFIGURATION, so it has to be enumerated to be
  believed. Checks 6 interfaces / 9 endpoints (3 interrupt IN + 6 bulk) with no
  duplicate address and `class=ef` on the host side, traffic both ways on port 0,
  and that bytes written to ports 1 and 2 never surface on port 0's stream.
  All three ports are driven individually from the
  host, with traffic both ways, separation, and per-port line coding. Three
  ports fit the host controller only because a port costs two channels rather
  than three - the host no longer claims the CDC control interface and sends its
  class requests over EP0, so EP0 plus 3 x (bulk IN + OUT) is 7 of 8 channels.
  Uses EspUsbHost 2.8.0's per-port binding.
- `usb_midi`: starts USB MIDI on one P4 and verifies channel voice messages and
  short Host -> Device SysEx packet splitting.
- `usb_msc`: starts USB Mass Storage on one P4 and verifies single-LUN RAM disk
  capacity, inquiry, read, write, and error paths.
- `usb_vendor`: starts a vendor-specific interface on one P4 and verifies bulk
  echo, application control IN/OUT, and WebUSB landing URL reads.
- `usb_audio`: deferred while the Host-side Audio peer remains UAC1-focused.
  P4 Device Audio defaults to UAC1 like other targets; UAC2 is explicit.

## P4 port / PHY reality (verified 2026-07)

P4 has two USB OTG controllers but only one UTMI (high-speed) PHY
(`SOC_USB_OTG_PERIPH_NUM=2`, `SOC_USB_UTMI_PHY_NUM=1`). EspUsbDevice owns its
TinyUSB runtime and maps `FullSpeed` to rhport 0/internal PHY and `HighSpeed` to
rhport 1/UTMI. EspUsbHost can independently select the other controller.

Consequences for one-board loopback:

- Device HS + Host FS is valid and negotiates FS because the host is FS.
- Device FS + Host HS is also valid and negotiates FS because the device is FS.
- Device HS + Host HS cannot share rhport 1/the single UTMI PHY.
- Device FS + Host FS cannot share rhport 0/the internal FS controller.
- Therefore an HS link is impossible on one board; HS-link validation still
  needs two boards.

## Matrix

| Device | Host | Expected |
|--------|------|----------|
| HS/UTMI device | FS host | Supported; link negotiates FS. |
| FS device | HS/UTMI host | Supported; link negotiates FS. |
| HS device | HS host | Not possible on one P4; rhport 1/UTMI conflict. |
| FS device | FS host | Not possible on one P4; rhport 0 conflict. |
