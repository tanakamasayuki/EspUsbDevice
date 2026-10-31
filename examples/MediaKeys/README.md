# EspUsbDevice MediaKeys

> 日本語版: [README.ja.md](README.ja.md)

Creates a USB HID Consumer Control / System Control device and sends media keys
such as volume, mute and play/pause to the Host.

## Hardware

- ESP32-S3, ESP32-S2 or ESP32-P4 board; the native USB OTG port goes to the Host
- A USB Host such as a PC, another ESP32 running EspUsbHost, or a test fixture
- A separate debug connection for `Serial` (115200) logs

## What It Does

- Starts `EspUsbDevice` with a Consumer Control and a System Control Function,
  combined into one HID interface
- While the Host has the device configured, sends one Consumer Control usage
  every 3 seconds, rotating through volume up / down, mute, play/pause, next
  track and previous track
- Leaves System Control usages as commented examples because they can affect the
  Host power state

## Key APIs

- `EspUsbDeviceHidConsumerControl media(device)` registers the media-key Function.
- `media.click(usage)` sends a press/release pair.
- `media.press(usage)` / `media.release()` explicitly control the pressed state.
- `media.sendUsage(usage)` sends a raw usage state; `media.usage()` returns it.
- `EspUsbDeviceHidSystemControl systemControl(device)` handles power off /
  standby / wake usages with the same `press` / `release` / `click` /
  `sendUsage` / `usage` methods.

## Consumer Control Constants

| Constant | Usage | Use |
|----------|-------|-----|
| `ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_UP` | `0x00e9` | Volume up |
| `ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_DOWN` | `0x00ea` | Volume down |
| `ESP_USB_DEVICE_CONSUMER_CONTROL_MUTE` | `0x00e2` | Mute |
| `ESP_USB_DEVICE_CONSUMER_CONTROL_PLAY_PAUSE` | `0x00cd` | Play / pause |
| `ESP_USB_DEVICE_CONSUMER_CONTROL_NEXT_TRACK` | `0x00b5` | Next track |
| `ESP_USB_DEVICE_CONSUMER_CONTROL_PREVIOUS_TRACK` | `0x00b6` | Previous track |

Any other Consumer page usage from `0x000` to `0x3FF` can be passed directly. The
report descriptor declares that range (Logical Maximum 1023), so a larger value
fails with `ESP_ERR_INVALID_ARG` and is not sent.

## System Control Constants

| Constant | Usage | Use |
|----------|-------|-----|
| `ESP_USB_DEVICE_SYSTEM_CONTROL_POWER_OFF` | `0x81` | Power off |
| `ESP_USB_DEVICE_SYSTEM_CONTROL_STANDBY` | `0x82` | Standby / sleep |
| `ESP_USB_DEVICE_SYSTEM_CONTROL_WAKE_HOST` | `0x83` | Wake |

Any other value (except 0, which releases) is rejected with false and
`ESP_ERR_INVALID_ARG`. `WAKE_HOST` is an ordinary HID input; it
does not perform a USB remote wakeup while the bus is suspended.

## Expected Serial Output

```text
USB media keys ready
MEDIA_KEY volume up usage=0x00e9
MEDIA_KEY volume down usage=0x00ea
MEDIA_KEY mute usage=0x00e2
```

## Notes

- Media key behavior depends on the Host OS and the active application.
  Observe it separately from the USB send result.
- Volume and play/pause keys act on the Host.
- Do not send System Control usages automatically; they may trigger sleep or
  power off. Wire them to an explicit user action.

## Changes from v2

- Usages are sent only while `device.ready()` is true.
- System Control now takes the standard Generic Desktop usage IDs (`0x81`-`0x83`)
  and `usage()` returns them. The constant names are unchanged, so code that used
  the constants works as before; code that passed the integers 1/2/3, or compared
  `usage()` with them, must switch to the constants. The descriptor now really
  advertises Power Down / Sleep / Wake Up (v2 advertised usages 0-3).
- Consumer Control usages above `0x3FF` now fail with `ESP_ERR_INVALID_ARG`;
  v2 sent them although the descriptor cannot carry them. Usages in range are
  unchanged.

## See Also

- [Keyboard](../Keyboard/) - HID keyboard
- [Gamepad](../Gamepad/) - HID gamepad
- [Remote wakeup](../../docs/usb-device-advanced.md) (suspend and remote wakeup)
- [HID API guide](../../docs/classes/hid.md)
- [All examples](../README.md)
