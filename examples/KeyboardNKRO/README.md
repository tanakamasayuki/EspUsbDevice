# EspUsbDevice Keyboard (NKRO)

> 日本語版: [README.ja.md](README.ja.md)

Creates a USB HID keyboard with N-key rollover (NKRO): any number of keys can be
held down at the same time, instead of the 6-key limit of the boot report.

## Hardware

- ESP32-S3 or another Arduino-ESP32 board with USB device support
- A USB host such as a PC, another ESP32 running EspUsbHost, or a test fixture
- A separate Serial monitor connection for logs when available

## What It Does

- Enables NKRO with `keyboard.enableNkro()` before `device.begin()`
- Builds a 10-key chord plus Left Shift in an `EspUsbDeviceNkroKeyboardReport` and
  sends the whole state as ONE report (impossible with the 6-key boot report),
  then releases all keys together
- Reads `keyboard.heldState()` back to show what the host was last told
- Still answers boot protocol (BIOS) with the 6-key format automatically

## How NKRO Works Here

- In report protocol the device sends a bitmap report: one modifier byte plus a
  224-bit key bitmap covering usages `0x00`-`0xDF`. Every key has its own bit, so
  there is no rollover limit.
- The bitmap range includes International1-9 (`0x87`-`0x8F`) and LANG1-9
  (`0x90`-`0x98`), so JIS and other non-US layout keys work.
- If the host selects boot protocol (typically a BIOS/UEFI), the device folds the
  held keys down to the first six and sends the standard 6-key boot report, so it
  still works before the OS HID driver loads.
- The IN endpoint packet size is raised to 32 bytes so the ~29-byte bitmap report
  fits in a single transfer (within `CFG_TUD_HID_EP_BUFSIZE` = 64).

## Key APIs

- `keyboard.enableNkro()` turns on NKRO. Call it before `device.begin()`.
- `keyboard.nkroEnabled()` reports whether NKRO is active.
- `keyboard.sendReport(nkroReport)` sends the whole held-key state as one report.
  Prefer it when several keys change together, or when your sketch computes the
  complete key set each cycle: the incremental API below emits one report per
  changed key, so the host sees a chord arrive key by key.
- `EspUsbDeviceNkroKeyboardReport` holds `modifiers` plus a 28-byte `bitmap` and is
  operated with `press(usage)` / `release(usage)` / `isDown(usage)` / `clear()`.
  Modifier usages `0xE0`-`0xE7` (Left Shift is `0xE1`) go into `modifiers`
  automatically. `press()` returns false only for usages above `0xDF` that are not
  modifiers, which this report cannot represent.
- `keyboard.heldState()` returns the state the host was last told about — useful to
  skip sending an unchanged state (the library does not do that for you) or to
  resynchronise without `releaseAll()`.
- `keyboard.pressUsage(usage, modifiers)` / `keyboard.releaseUsage(usage)` hold
  and release individual keys; with NKRO any number can be held at once.
- `keyboard.releaseAll()` releases every held key.
- `keyboard.write()`, `tapKey()`, `pressKey()`, and `setLayout()` work the same as
  the 6KRO keyboard for sequential text entry.

## Expected Serial Output

```text
USB NKRO keyboard ready (nkro=1)
after releaseAll: A still down? no
sent 10-key chord (protocol=report)
after releaseAll: A still down? no
sent 10-key chord (protocol=report)
```

## See Also

- [Keyboard](../Keyboard/) - standard 6-key boot keyboard
- [KeyboardMouse](../KeyboardMouse/) - composite keyboard and mouse device
