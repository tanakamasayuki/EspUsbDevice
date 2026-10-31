# EspUsbDevice Keyboard (NKRO)

> 日本語版: [README.ja.md](README.ja.md)

Creates a USB HID keyboard with N-key rollover (NKRO): any number of keys can be
held down at the same time, instead of the 6-key limit of the boot report.

## Hardware

- ESP32-S3, ESP32-S2 or ESP32-P4 board; the native USB OTG port goes to the Host
- A USB Host such as a PC, another ESP32 running EspUsbHost, or a test fixture
- A separate debug connection for `Serial` (115200), used for both input and logs

## What It Does

- Enables NKRO with `keyboard.enableNkro()` before `device.begin()` and checks the result
- On `c` from the debug port, builds a 10-key chord plus Left Shift in an
  `EspUsbDeviceNkroKeyboardReport` and sends the whole state as ONE report
  (impossible with the 6-key boot report), then releases all keys together
- Reads `keyboard.heldState()` back to show what the Host was last told
- Prints each boot/report protocol change through `keyboard.onProtocol()`
- Still answers boot protocol (BIOS) with the 6-key format automatically

Open a key tester on the Host before sending `c`; nothing is sent before that.

## How NKRO Works Here

- In report protocol the device sends a bitmap report: one modifier byte plus a
  224-bit key bitmap covering usages `0x00`-`0xDF` (29 bytes). Every key has its
  own bit, so there is no rollover limit.
- The bitmap range includes International1-9 (`0x87`-`0x8F`) and LANG1-9
  (`0x90`-`0x98`), so JIS and other non-US layout keys work.
- If the Host selects boot protocol (typically a BIOS/UEFI), the device folds the
  held keys down to the first six and sends the standard 8-byte boot report, so it
  still works before the OS HID driver loads.
- The interrupt IN packet is 32 bytes so the 29-byte report fits one packet.
  No build setting is involved; the report storage is sized at `begin()`.

## Key APIs

- `keyboard.enableNkro()` turns on NKRO. Call it before `device.begin()`; it
  returns `ESP_ERR_INVALID_STATE` once the device has started.
- `keyboard.nkroEnabled()` reports whether NKRO is active.
- `keyboard.sendReport(nkroReport)` sends the whole held-key state as one report.
  Prefer it when several keys change together, or when your sketch computes the
  complete key set each cycle: the incremental API below emits one report per
  changed key, so the Host sees a chord arrive key by key.
- `EspUsbDeviceNkroKeyboardReport` holds `modifiers` plus a 28-byte `bitmap` and is
  operated with `press(usage)` / `release(usage)` / `isDown(usage)` / `clear()`.
  Modifier usages `0xE0`-`0xE7` (Left Shift is `0xE1`) go into `modifiers`
  automatically. `press()` returns false only for usages above `0xDF` that are not
  modifiers, which this report cannot represent.
- `keyboard.heldState()` returns the state the Host was last told about - useful to
  skip sending an unchanged state (the library does not do that for you) or to
  resynchronise without `releaseAll()`.
- `keyboard.pressUsage(usage, modifiers)` / `keyboard.releaseUsage(usage)` hold
  and release individual keys; with NKRO any number can be held at once. A usage
  the bitmap cannot carry (above `0xDF` and not a modifier) fails with
  `ESP_ERR_INVALID_ARG`.
- `keyboard.releaseAll()` releases every held key.
- `keyboard.protocol()` returns 0 (boot) or 1 (report); `keyboard.onProtocol()`
  reports changes on the USB worker and must be registered before `begin()`.
- `keyboard.write()`, `tapKey()`, `pressKey()`, `releaseKey()` and `setLayout()`
  work the same as the 6KRO keyboard for sequential text entry. With NKRO enabled
  they also go through the bitmap report.

## Expected Serial Output

```text
USB NKRO keyboard ready (nkro=1)
Focus a key tester on the Host, then send c on this port
after releaseAll: A still down? no
sent 10-key chord (protocol=report)
```

`PROTOCOL boot` / `PROTOCOL report` appear only when the Host sends SET_PROTOCOL.

## Checks

- Watch the chord in a key tester and confirm that `releaseAll()` ends it.
- A report is the complete key state. Compare whole states, or pair every press
  with a release, before adding an application action.
- More than six keys need report protocol and a Host that reads the NKRO
  descriptor; boot protocol still uses its 6-key form.
- `sendReport()` returning true means the report was copied and queued, not
  that the Host has read it.

## Changes from v2

- The chord is sent when you send `c` on the debug port, not every 4 seconds.
- `keyboard.enableNkro()` and `keyboard.onProtocol()` now return `esp_err_t`
  (`ESP_OK` before `begin()`, `ESP_ERR_INVALID_STATE` after). The sketch checks it.
- The 32-byte IN packet no longer depends on `CFG_TUD_HID_EP_BUFSIZE`, which has
  been removed; defining it is now a build error.
- `sendReport(EspUsbDeviceNkroKeyboardReport)`, `heldState()`, `protocol()` and
  `releaseAll()` are unchanged.

## See Also

- [Keyboard](../Keyboard/) - standard 6-key boot keyboard
- [KeyboardMouse](../KeyboardMouse/) - composite keyboard and mouse device
- [HID API guide](../../docs/classes/hid.md)
- [All examples](../README.md)
