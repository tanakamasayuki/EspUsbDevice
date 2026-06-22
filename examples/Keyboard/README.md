# EspUsbDevice Keyboard

> 日本語版: [README.ja.md](README.ja.md)

Creates a USB HID boot keyboard device and sends text, individual characters,
raw HID usage IDs, and keyboard LED output reports.

## Hardware

- ESP32-S3 or another Arduino-ESP32 board with USB device support
- A USB host such as a PC, another ESP32 running EspUsbHost, or a test fixture
- A separate Serial monitor connection for logs when available

## What It Does

- Starts `EspUsbDevice` as a full-speed HID keyboard
- Sends layout-aware ASCII text with `keyboard.write()`
- Switches between `EN_US` and `JA_JP` layouts with `keyboard.setLayout()`
- Sends raw HID usage IDs for keys that are not part of the ASCII wrapper
- Receives NumLock / CapsLock / ScrollLock LED output reports from the host

## Key APIs

- `keyboard.setLayout(layout)` selects the character-to-usage mapping.
- `keyboard.write(text)` sends a null-terminated ASCII string.
- `keyboard.tapKey(key)` presses and releases one layout-aware ASCII character.
- `keyboard.pressKey(key)` presses one layout-aware ASCII character and keeps it
  pressed until `releaseAll()` or `releaseUsage()`.
- `keyboard.tapUsage(usage, modifiers)` sends one raw HID usage.
- `keyboard.pressUsage(usage, modifiers)` / `keyboard.releaseUsage(usage)` give
  direct control over boot keyboard reports.
- `keyboard.onOutputReport(callback)` receives keyboard LED reports from the
  host.

## Layouts

`EspUsbDeviceKeyboardLayout` uses the same numeric IDs as EspUsbHost. The device
side uses the Host keymap tables in reverse: Host parses `usage/modifier` to
ASCII, Device maps ASCII back to `usage/modifier`.

Supported layout constants:

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
USB keyboard ready
LEDS num=0 caps=1 scroll=0 raw=0x02
last_leds=0x02
```

## See Also

- [KeyboardMouse](../KeyboardMouse/) - composite keyboard and mouse device
- [Mouse](../Mouse/) - HID mouse device
