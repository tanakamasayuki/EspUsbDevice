# EspUsbDeviceBringUpCheck

> 日本語版: [README.ja.md](README.ja.md)

The **first sketch to run** on a new board. It checks, in order, whether the USB
device starts and whether the host enumerates it, and prints a checklist when
enumeration never happens.

It registers a HID keyboard but never sends a key, so it is safe to leave
plugged into a PC.

## Hardware

- An Arduino-ESP32 board with USB device support (ESP32-S2 / S3 / P4)
- A USB host such as a PC, and a data-capable USB cable
- A separate Serial monitor connection for logs (the UART / USB Serial-JTAG side
  used for flashing)

## What It Does

- Prints the target name, the arduino-esp32 version and the library version
- Prints the requested controller (`Auto` / `FullSpeed` / `HighSpeed`) and the
  one that will actually be used
- Prints whether `device.begin()` succeeded, and on failure the
  `lastErrorName()` plus the common causes
- Prints the configuration descriptor size and HID endpoint size after `begin()`
- Prints `MOUNTED` / `UNMOUNTED` as `device.ready()` changes, and `SUSPENDED` /
  `RESUMED` as `device.suspended()` changes
- Prints a troubleshooting checklist once if nothing enumerates within 10 seconds
- Prints `HOST_OUTPUT_REPORT` when you press CapsLock / NumLock on the host,
  which proves the **host -> device** direction works

## How To Read It

| Output | Meaning |
|--------|---------|
| `BEGIN failed` | The host is not involved. Something is wrong on the board: descriptor too large, endpoint budget exceeded, unsupported target, invalid setting |
| `BEGIN ok` but no `MOUNTED` | Electrical (cable, connector, VBUS), or the host never got the descriptors |
| `MOUNTED` appears | Enumeration succeeded; anything after this is class-specific |
| `MOUNTED` / `UNMOUNTED` repeating | Insufficient power, bad cable, or the host re-enumerating |
| `SUSPENDED` | The host suspended the bus (sleep, selective suspend). The configuration is kept: `ready()` stays true |
| `HOST_OUTPUT_REPORT` appears | Traffic works in both directions |

Common `BEGIN failed` errors:

- `ESP_ERR_INVALID_SIZE` - the descriptor or endpoint budget does not fit this
  controller. Remove a Function, or use the high-speed controller on ESP32-P4.
- `ESP_ERR_NOT_SUPPORTED` - no USB device controller for this target or for the
  requested `controller`.
- `ESP_ERR_NO_MEM` - out of heap for the descriptors or the Functions' buffers.
- `ESP_ERR_INVALID_ARG` - a `EspUsbDeviceConfig` value or a Function setting is
  invalid.
- `ESP_ERR_INVALID_STATE` - already started, or a Function was registered while
  the device was running.

## On ESP32-P4

`config.controller` defaults to `Auto`, which is HighSpeed (the HS port with
its UTMI PHY). If the cable goes to the full-speed connector, uncomment
`config.controller = EspUsbController::FullSpeed;` in the sketch. Which
connector is wired to which PHY is board-specific - check the schematic.

## Expected Serial Output

```text
=== EspUsbDevice bring-up check ===
TARGET ESP32-S3
CORE arduino-esp32 3.3.12
LIBRARY EspUsbDevice 3.0.0
CONTROLLER requested=Auto resolved=FullSpeed (Auto)
BEGIN ok
DESCRIPTOR config_bytes=34
VID_PID 303a:4050
Now connect the device connector to a host and watch for MOUNTED.
MOUNTED t=4120ms speed=full hid_packet=8
HOST_OUTPUT_REPORT leds=0x02 num=0 caps=1 scroll=0
STATUS mounted=1 suspended=0 t=9000ms leds=0x02
```

## Related

- [EspUsbDeviceDescriptorDump](../EspUsbDeviceDescriptorDump/) - run next when the descriptors are the suspect
- [EspUsbDeviceConsole](../EspUsbDeviceConsole/) - hand-drive transfers once enumeration works
- [docs/usb-device-guide.md](../../../docs/usb-device-guide.md) - the full bring-up procedure
- [Core API (v3)](../../../docs/usb-device-advanced.md) - controllers and endpoint limits

## Walkthrough and checks

### Before running

Separate internal device startup from host configuration. Follow the hardware requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Inspect `beginOk`, ready and suspend transitions and the printed configuration before changing a USB Function. If startup succeeds but ready stays false, check the data connector, cable and host enumeration. A physical cable alone is not a configured HID device.

### API use and adaptation

Treat startup (`started()`), enumeration (`ready()`), interface discovery and actual class communication as separate checkpoints. A descriptor says what a device advertises; it does not prove a transfer completed or that a protocol is implemented. An interface number, endpoint address, USB address and physical port identify different things.

First compare the same device directly connected and reconnected. Add a hub or more Functions only after the simple case is understood. Preserve the error name and the operation that produced it.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs. Test the host absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one setting. The demonstration's error logging is not a general retry or recovery policy.

## Changes from v2

- **The negotiated bus speed is no longer printed.** v2 read it with
  TinyUSB's `tud_speed_get()`; v3 does not use TinyUSB and has no public query
  for the current speed. The `resolved=` controller line still shows which port
  `Auto` selects.
- The sketch no longer includes `tusb.h`.
- New: `SUSPENDED` / `RESUMED` from `device.suspended()`, the library version,
  and the `INVALID_ARG` / `INVALID_STATE` causes in the failure help.
- `onOutputReport()` returns `esp_err_t` and is checked.
- The checklist's build item no longer refers to TinyUSB mode; it warns against
  the core's USB-on-boot features instead.

- [Sketch](EspUsbDeviceBringUpCheck.ino)
- [All examples](../../README.md)
- [Example workflow](../../../docs/example-workflow.md)
