# Working through the examples

[日本語](example-workflow.ja.md)

A guide to trying the EspUsbDevice v3 examples in order, from wiring to checking results to modifying them.

The ESP32 is the USB device side. You connect it to a PC or another host and have it recognized as a keyboard, a serial port, an audio device and so on. The other end must be a host; connecting two devices together never starts enumeration.

USB principles and how to isolate problems are in the [USB Device Development Guide](usb-device-guide.md). This document is limited to the steps and cautions for running the examples on your bench.

## Terms to know first

| Term | Meaning here |
| --- | --- |
| Device | Owns USB as a whole. `EspUsbDevice` in a sketch |
| Function | One capability shown to the host. `EspUsbDeviceHidKeyboard`, `EspUsbDeviceCdcSerial`, ... |
| Enumeration | The host reading the device's information and settling its address and configuration |
| Descriptor | What the device declares about its functions and data formats |
| Interface | A functional unit inside one USB device. A composite device has several |
| Endpoint | A data channel with a number, a direction and a transfer type |
| IN / OUT | Directions are named from the host's side. IN is device → host, OUT is host → device |
| Callback | A function you register beforehand, called by the library on an event |
| USB worker | The FreeRTOS task that does USB processing. Callbacks run on it |

## Examples to try first

1. [`EspUsbDeviceBringUpCheck`](../examples/Info/EspUsbDeviceBringUpCheck/README.md): tell a successful `begin()` apart from the host finishing configuration (`ready()`).
2. [`Serial`](../examples/Serial/README.md): tell the log `Serial` apart from the new CDC port that appears.
3. [`Keyboard`](../examples/Keyboard/README.md) or [`Gamepad`](../examples/Gamepad/README.md): check with a matching PC application.
4. [`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/README.md): read how the declared Functions map to interfaces and endpoints.
5. Move on to composite devices, audio, storage and firmware update only after single Functions work.

The full list is in [examples](../examples/README.md). Each example's README covers the parts it needs, how to operate it, the API it uses and cautions for modifying it. Examples with helper files are used as a whole folder.

## Installing and building

1. Set up Arduino-ESP32 and the library as described in the [README](../README.md). The reference is Arduino-ESP32 **3.3.12**.
2. In the Arduino IDE, open an example from "File → Examples → EspUsbDevice" and select the exact board. With the CLI, build from the repository root:

```sh
arduino-cli compile --profile esp32s3 examples/Serial
arduino-cli upload  --profile esp32s3 --port <flashing port> examples/Serial
```

3. Each example's `sketch.yaml` holds the build conditions per profile.

| Profile | Target | Main settings |
| --- | --- | --- |
| `esp32s3` | ESP32-S3 | `USBMode=default` (USB-OTG (TinyUSB)) |
| `esp32s2` | ESP32-S2 | Defaults |
| `esp32p4` | ESP32-P4 (older revision v1.x) | `ChipVariant=prev3` |
| `esp32p4x` | ESP32-P4X (revision v3.x) | `ChipVariant=postv3` |

4. When setting the Arduino IDE by hand, match the above. On S3, set `USB Mode` to "USB-OTG (TinyUSB)" and leave the On Boot options such as `USB CDC On Boot` disabled.
5. Upload over a path you know works, and open the log at the same speed as the sketch's `Serial.begin()`.

A successful build does not mean the wiring or the interaction with the host has been checked. ESP32-S2 has been compile-checked only and is not verified on hardware.

## Check connectors and power first

A "USB" label on the board does not tell you whether it leads to the OTG data pins, USB Serial/JTAG or a USB-UART bridge. Check the controller, D−/D+, PHY, VBUS and USB-C CC circuit in the schematic. Use a cable that carries data.

- **The host** supplies VBUS to the other side; **the device** receives it. Do not tie an external supply to USB power carelessly; follow the board's power design.
- **ESP32-P4 has an HS and an FS controller.** The default (`EspUsbController::Auto`) is HS. Match `config.controller` to the connector the cable is in. If they do not match, `begin()` succeeds but the host sees nothing. Choosing a controller does not change board wiring or the power circuit.
- **`Serial` is the sketch's log path.** Depending on board and settings it goes over a UART or over USB Serial/JTAG. Find out which connector you can read it on. It is separate from the CDC or HID the example creates.
- In examples that swap OTG and Serial/JTAG pins (FS on P4), arrange a separate log and recovery path, such as a UART, first.

## Order for reading the code

1. Read the header comment and the constants for pins, formats and commands. Check the units.
2. Look at how the global objects relate. Tell `EspUsbDevice` (the owner of everything) apart from the Functions and helpers registered with it.
3. In `setup()`, follow the order: register callbacks → `configure()` the Functions → `device.begin()`. All of these come before `begin()`.
4. In `loop()`, look at received bytes, state changes, periodic work and pending work.
5. Tell return types apart: a success `bool`, an `esp_err_t`, the number of bytes actually handled, a state query. What is kept and what is discarded on failure differs between examples.

## Do not confuse the states

| Observation | How to read it |
| --- | --- |
| `device.begin(config)` succeeds | Resources are allocated and USB has started. The host may not have finished enumeration |
| `device.started()` | From a successful `begin()` until `end()`. Unrelated to the host connection |
| `device.ready()` | The host has completed SET_CONFIGURATION |
| `device.suspended()` | The bus is suspended. `ready()` stays true |
| `device.speed()` | The speed the host negotiated at its last bus reset (`Full` or `High`). `None` before that |
| CDC `connected()` | The host's DTR state. Not the physical connection, and not use of other Functions |
| Audio / Video stream start | The host has started using that stream. Enumeration alone does not start it |
| A send / write succeeds | The API accepted it. Not the same as the host application having processed it |

When `begin()` fails, print `device.lastErrorName()` straight away; it returns the `esp_err_to_name()` name of any code. The main meanings are below; see [Reading errors in the development guide](usb-device-guide.md#27-reading-errors) for detail.

| Printed | Suspect first |
| --- | --- |
| `ESP_ERR_INVALID_SIZE` | Endpoint budget exceeded. The IN endpoints do not fit the controller's TX FIFO. Size requirements at HS (MSC `transferBytes` not a multiple of 512, and so on) |
| `ESP_ERR_NO_MEM` | Out of heap |
| `ESP_ERR_INVALID_ARG` | A wrong setting |
| `ESP_ERR_INVALID_STATE` | `begin()` called twice. A Function added while running |
| `ESP_ERR_NOT_SUPPORTED` | A chip without USB OTG. A controller it does not have |

## Changing settings

Capacities and modes are set per Function with `configure()`. No build flags or `build_opt.h` are used (old TinyUSB flags cause a compile error).

- To change them: `device.end()` → `configure()` → `device.begin()`. The host sees an unplug and a replug.
- You cannot `configure()` or add Functions while running.
- A setting that works at FS can fail in `begin()` on the P4's HS controller, because the bulk max packet becomes 512 bytes at HS ([What changes at HS](usb-device-guide.md#43-what-changes-at-hs)).
- Adding Functions can hit the endpoint and FIFO budgets. On S3 there are at most 4 device-to-host (IN) endpoints ([Endpoint budget](usb-device-guide.md#44-endpoint-budget)).

## Keep callbacks and loop() apart

USB callbacks run on the USB worker, separate from `loop()`. You do not receive by calling them from `loop()`. Adding long logging, waits, decoding or file I/O to a callback delays the next USB event.

- Keep callbacks short. Hand heavy parsing, file work and restart decisions to `loop()` along the path the example shows.
- Do not wait inside a callback. Send HID reports with `timeoutMs = 0`.
- Do not call `begin()` / `end()` or `configure()` inside a callback.
- Data, reports and frames given to a callback may be reused once it returns. Copy what you need later into your own buffer, and decide who owns it and for how long.
- For borrowed transfers (`writeDirect`, `readDirect`, Video's `submitFrame` and the like), do not change or free the buffer from acceptance until the completion notification. Do not confuse them with ordinary writes, which copy. Buffers for `writeDirect` / `readDirect` go in DMA-capable internal RAM on a 64-byte boundary; PSRAM cannot be used.

`volatile` alone does not give you mutual exclusion, ordering or consistency across several values when handing data to another task. Use a synchronized handoff such as a FreeRTOS queue. Enqueue from a callback without waiting, and decide in advance whether a full queue means drop, count or retry.

## Finite buffers and failures

- Interpret only the length a read returned. Do not assume the full requested length arrives, or that one callback is one command.
- When a write is short or returns false, decide whether to keep the rest or count it as dropped. A log line in the example is not recovery.
- Check press and release for HID, Note On and Off for MIDI, how long a frame is borrowed for Video, and the difference between samples, frames and bytes for Audio.
- For storage, keep block operations and the filesystem apart. Do not let the host and the sketch modify the same medium at the same time.
- Do not measure speed with heavy printing in callbacks. Separate slow printing from slow USB.

## When it does not work

| Symptom | Check next |
| --- | --- |
| Cannot read the log at all | Board settings, the `Serial` path, terminal speed, effects of port switching |
| `begin()` fails | What `lastErrorName()` prints, controller setting, conflict with another USB stack, required resources |
| Starts but does not enumerate | Data pins, cable, VBUS, selected controller and connector (P4) |
| Enumerates but the class does not work | Interfaces, endpoints, report descriptor, formats the other side supports |
| Works only at first, or drops under load | Actual received lengths, returned byte counts, full queues, callback time, buffer lifetime |
| Does not work after reconnecting | Stale session, DTR, held keys, pending transfers |

Try one stage at a time: no partner → connected → normal operation → disconnect → reconnect, and keep the log of each stage. Before device-specific actions such as changing settings, printing or rewriting flash, check in the example's README what will actually happen.

## About what has been verified

The examples use only the v3 public API. Pass/fail testing on hardware is done separately in [validation](../validation/README.ja.md). Verified setups: two S3 boards (FS), one P4 with HS ↔ FS, older P4 and P4X connected at HS, and P4X with a Linux PC (HS). v3 behavior on S2 hardware, Windows and macOS has not been verified.

For principles see the [USB Device Development Guide](usb-device-guide.md); for internals and callback context, the [Advanced guide](usb-device-advanced.md); for each class's API, the guides under `docs/classes/`.
