# EspUsbDeviceBringUpCheck

> 日本語版: [README.ja.md](README.ja.md)

The **first sketch to run** on a new board. It checks, in order, whether the USB
device starts, whether the host enumerates it, and at which speed - and prints a
checklist when enumeration never happens.

It registers a HID keyboard but never sends a key, so it is safe to leave
plugged into a PC.

## Hardware

- An Arduino-ESP32 board with USB device support (ESP32-S2 / S3 / P4)
- A USB host such as a PC, and a data-capable USB cable
- A separate Serial monitor connection for logs (the UART / USB Serial-JTAG side
  used for flashing)

## What It Does

- Prints the target name and the arduino-esp32 version
- Prints the requested controller (`Auto` / `FullSpeed` / `HighSpeed`) and the
  one that will actually be used
- Prints whether `device.begin()` succeeded, and on failure the
  `lastErrorName()` plus the common causes
- Prints `MOUNTED` / `UNMOUNTED` as `device.ready()` changes
- Prints the negotiated speed (Full / High) on mount
- Prints a troubleshooting checklist once if nothing enumerates within 10 seconds
- Prints `HOST_OUTPUT_REPORT` when you press CapsLock / NumLock on the host,
  which proves the **host -> device** direction works

## How To Read It

| Output | Meaning |
|--------|---------|
| `BEGIN failed` | The host is not involved. Something is wrong on the board: descriptor too large, endpoint budget exceeded, unsupported target |
| `BEGIN ok` but no `MOUNTED` | Electrical (cable, connector, VBUS), or the host never got the descriptors |
| `MOUNTED` appears | Enumeration succeeded; anything after this is class-specific |
| `MOUNTED` / `UNMOUNTED` repeating | Insufficient power, bad cable, or the host re-enumerating |
| `HOST_OUTPUT_REPORT` appears | Traffic works in both directions |

Common `BEGIN failed` errors:

- `ESP_ERR_INVALID_SIZE` - the descriptor or endpoint budget does not fit this
  controller. Remove a class, or use the high-speed controller on ESP32-P4.
- `ESP_ERR_NOT_SUPPORTED` - this target has no usable USB device controller.
- `ESP_ERR_NO_MEM` - out of heap for the descriptor buffers.

## On ESP32-P4

`config.controller` defaults to `Auto`, which is HighSpeed (rhport 1, external
UTMI PHY). If the cable goes to the full-speed connector, uncomment
`config.controller = EspUsbController::FullSpeed;` in the sketch. Which
connector is wired to which PHY is board-specific - check the schematic.

## Expected Serial Output

```text
=== EspUsbDevice bring-up check ===
TARGET ESP32-S3
CORE arduino-esp32 3.3.11
CONTROLLER requested=Auto resolved=FullSpeed (Auto)
BEGIN ok
DESCRIPTOR config_bytes=34 hid_endpoint_size=8
VID_PID 303a:4050
Now connect the device connector to a host and watch for MOUNTED.
MOUNTED t=4120ms speed=Full (12 Mbps)
HOST_OUTPUT_REPORT leds=0x02 num=0 caps=1 scroll=0
STATUS mounted=1 t=9000ms leds=0x02
```

## Related

- [EspUsbDeviceDescriptorDump](../EspUsbDeviceDescriptorDump/) - run next when the descriptors are the suspect
- [EspUsbDeviceConsole](../EspUsbDeviceConsole/) - hand-drive transfers once enumeration works
- [docs/usb-device-guide.md](../../../docs/usb-device-guide.md) - the full bring-up procedure
