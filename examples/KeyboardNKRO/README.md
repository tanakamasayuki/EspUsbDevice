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
- Holds a 10-key chord at once (impossible with the 6-key boot report), then
  releases all keys together
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
- `keyboard.pressUsage(usage, modifiers)` / `keyboard.releaseUsage(usage)` hold
  and release individual keys; with NKRO any number can be held at once.
- `keyboard.releaseAll()` releases every held key.
- `keyboard.write()`, `tapKey()`, `pressKey()`, and `setLayout()` work the same as
  the 6KRO keyboard for sequential text entry.

## Expected Serial Output

```text
USB NKRO keyboard ready (nkro=1)
sent 10-key chord (protocol=report)
sent 10-key chord (protocol=report)
```

## See Also

- [Keyboard](../Keyboard/) - standard 6-key boot keyboard
- [KeyboardMouse](../KeyboardMouse/) - composite keyboard and mouse device
