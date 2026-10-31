# EspUsbDevice Keyboard

> 日本語版: [README.ja.md](README.ja.md)

Creates a USB HID boot keyboard and sends keys typed on the debug serial port,
text in two layouts, raw HID usage IDs, and reads the Host's Lock LED reports.

## Hardware

- ESP32-S3, ESP32-S2 or ESP32-P4 board; the native USB OTG port goes to the Host
- A USB Host such as a PC, another ESP32 running EspUsbHost, or a test fixture
- A separate debug connection for `Serial` (115200), used for both input and logs

## What It Does

- Starts `EspUsbDevice` as an HID keyboard with its own VID/PID/strings
- Sends each character received on the debug port as one key tap
- On `~`, runs a demo:
  - layout-aware ASCII text with `keyboard.write()`
  - switches `EN_US` → `JA_JP` → `EN_US` with `keyboard.setLayout()`
  - holds Shift+B with `pressUsage()` and releases it with `releaseUsage()` / `releaseAll()`
  - sends a raw HID usage (`LANG1`) that the ASCII mapping does not cover
  - prints `keyboard.ledState()`
- Prints NumLock / CapsLock / ScrollLock LED output reports from the Host

Nothing is typed until you send something on the debug port. Focus a scratch
editor on the Host first.

## Key APIs

- `keyboard.setLayout(layout)` selects the character-to-usage mapping.
- `keyboard.write(text)` sends a null-terminated ASCII string.
- `keyboard.tapKey(key)` presses and releases one layout-aware ASCII character.
  It returns false (`ESP_ERR_NOT_SUPPORTED`) for a character the layout cannot produce.
- `keyboard.pressKey(key)` presses one layout-aware ASCII character and keeps it
  pressed until `releaseKey(key)`, `releaseUsage()` or `releaseAll()`.
  `releaseKey(key)` lets go of the key and the modifiers the layout used for it.
- `keyboard.tapUsage(usage, modifiers)` presses and releases one raw HID usage.
  It releases only what it added; keys held before the tap stay held.
- `keyboard.pressUsage(usage, modifiers, timeoutMs)` / `keyboard.releaseUsage(usage)`
  add a key to and remove it from the held state, so keys combine: `pressUsage(A)`
  then `pressUsage(B)` holds both. Modifiers add up too, and a modifier usage
  (`0xE0`-`0xE7`) sets its modifier bit. The boot report holds six other keys; a
  seventh fails with `ESP_ERR_INVALID_SIZE` and changes nothing. `releaseUsage()`
  removes only that key and keeps the rest packed at the front of the report.
- `keyboard.ledState()` returns the latest Host LED state and is updated even
  with no callback installed, so an external Caps Lock LED can be driven by polling.
- `keyboard.onOutputReport(callback)` receives keyboard LED reports. It runs on
  the USB worker and must be registered before `device.begin()`.

## Layouts

`EspUsbDeviceKeyboardLayout` uses the same numeric IDs as EspUsbHost. The device
side uses the Host keymap tables in reverse: Host parses `usage/modifier` to
ASCII, Device maps ASCII back to `usage/modifier`. The layout only chooses key
positions; the character that appears depends on the Host's layout setting.

| Constant | Locale | Notes |
|----------|--------|-------|
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_ZH_TW` | zh_TW | Traditional Chinese, US QWERTY symbols |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_DA_DK` | da_DK | Danish |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_DE_DE` | de_DE | German QWERTZ |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US` | en_US | English US, default |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_FI_FI` | fi_FI | Finnish |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_FR_FR` | fr_FR | French AZERTY |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_HU_HU` | hu_HU | Hungarian QWERTZ |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_IT_IT` | it_IT | Italian |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP` | ja_JP | Japanese |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_KO_KR` | ko_KR | Korean, US QWERTY symbols |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_NL_NL` | nl_NL | Dutch |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_NB_NO` | nb_NO | Norwegian Bokmal |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_PT_BR` | pt_BR | Brazilian Portuguese |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_SV_SE` | sv_SE | Swedish |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_ZH_CN` | zh_CN | Simplified Chinese, US QWERTY symbols |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_GB` | en_GB | English UK |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_PT_PT` | pt_PT | Portuguese |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_ES_ES` | es_ES | Spanish |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_FR_CH` | fr_CH | Swiss French |

## Expected Serial Output

```text
USB keyboard ready: type here to send keys, ~ runs the demo
LEDS num=0 caps=1 scroll=0 raw=0x02
last_leds=0x02 caps=1
```

## Checks

- Run the unchanged sketch first. Compare `write()` output with the US and JIS
  symbols separately, and toggle Caps Lock on the Host to see the LED report.
- A HID report is key state, not a text line. Every press needs its release;
  `tapKey()`/`write()` pair them for you, `pressKey()`/`pressUsage()` do not.
- The LED callback runs on the USB worker. Keep it short, and copy anything you
  hand to another task.
- If keys do not arrive, check enumeration first, then the report, then the
  Host's layout. Change one thing at a time.

## Changes from v2

- The sketch no longer types every 3 seconds; it types only what you send on
  the debug port, and `~` runs the former automatic sequence.
- `keyboard.onOutputReport()` now returns `esp_err_t`: `ESP_OK` before `begin()`,
  `ESP_ERR_INVALID_STATE` once started. Code that ignores the result still compiles.
- `pressUsage()`/`pressKey()` now add to the held keys instead of replacing the
  report, `tapUsage()`/`tapKey()` release only what they pressed instead of
  everything, and the third argument of `pressUsage()` is now `timeoutMs`
  (default 100) instead of an unused `holdMs`. `releaseKey(char)` is new.
- `device.begin(config)`, `write()`, `setLayout()` and `ledState()` are
  unchanged. No `CFG_TUD_HID*` build setting is needed; the
  report storage is sized from the report descriptor at `begin()`.

## See Also

- [KeyboardNKRO](../KeyboardNKRO/) - N-key rollover keyboard
- [KeyboardMouse](../KeyboardMouse/) - composite keyboard and mouse
- [HID API guide](../../docs/classes/hid.md)
- [All examples](../README.md)
