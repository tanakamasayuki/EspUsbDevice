# EspUsbDevice MediaKeys

> 日本語版: [README.ja.md](README.ja.md)

Creates a USB HID consumer control / system control device and sends media keys
such as volume, mute, and play/pause to the host.

## Hardware

- ESP32-S3 or another Arduino-ESP32 board with USB device support
- A USB host such as a PC, another ESP32 running EspUsbHost, or a test fixture
- A separate Serial monitor connection for logs when available

## What It Does

- Starts `EspUsbDevice` as a full-speed HID media keys device
- Sends Consumer Control usages every 3 seconds
- Rotates through volume up / down, mute, play/pause, next track, and previous
  track
- Leaves System Control usages as commented examples because they can affect the
  host power state

## Key APIs

- `EspUsbDeviceHidConsumerControl media(device)` registers the media-key HID
  function.
- `media.click(usage)` sends a press/release pair.
- `media.press(usage)` / `media.release()` explicitly control pressed state.
- `media.sendUsage(usage)` sends raw usage state.
- `EspUsbDeviceHidSystemControl systemControl(device)` handles power / standby /
  wake usages.

## Consumer Control Constants

| Constant | Use |
|----------|-----|
| `ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_UP` | Volume up |
| `ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_DOWN` | Volume down |
| `ESP_USB_DEVICE_CONSUMER_CONTROL_MUTE` | Mute |
| `ESP_USB_DEVICE_CONSUMER_CONTROL_PLAY_PAUSE` | Play / pause |
| `ESP_USB_DEVICE_CONSUMER_CONTROL_NEXT_TRACK` | Next track |
| `ESP_USB_DEVICE_CONSUMER_CONTROL_PREVIOUS_TRACK` | Previous track |

## System Control Constants

| Constant | Use |
|----------|-----|
| `ESP_USB_DEVICE_SYSTEM_CONTROL_POWER_OFF` | Power off |
| `ESP_USB_DEVICE_SYSTEM_CONTROL_STANDBY` | Standby / sleep |
| `ESP_USB_DEVICE_SYSTEM_CONTROL_WAKE_HOST` | Wake |

## Notes

- Media key behavior depends on the host OS and active application.
- Volume and play/pause keys act on the host.
- Do not send system control keys automatically; they may trigger sleep or power
  actions.
- Serial monitor prints a summary of each sent usage.

## See Also

- [Keyboard](../Keyboard/) - HID keyboard device
- [Gamepad](../Gamepad/) - HID gamepad device
