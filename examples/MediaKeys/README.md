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

## Walkthrough and checks

### Before running

Send consumer media actions and declare a system-control function. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

`media.click()` pairs press and release for each test step. Observe host volume/playback behavior separately from the USB send result. The commented system-control calls can affect host power state; leave them disabled until an explicit user action is wired into your application.

### API use and adaptation

A HID report is a structured state or change, not an arbitrary text line. Decode it with its report descriptor: distinguish report ID, usage page/usage, signed value and logical range. Keyboard usages identify key positions, mouse motion is relative, and button values are bitmaps. Match press with release, or compare complete states, before adding an application action.

Callback report data is borrowed. Copy the bytes or decoded values into owned storage before handing work to another task. Log only short diagnostics in the callback; for repeated raw dumps, terminal speed can become the bottleneck. If an input is missing, first confirm enumeration, then descriptor/report length, then application decoding. Do not change all three at once.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](MediaKeys.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
