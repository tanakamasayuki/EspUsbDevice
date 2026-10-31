# EspUsbDevice SerialMulti

> 日本語版: [README.ja.md](README.ja.md)

Creates several independent USB CDC ACM serial ports on a single device. The
host sees one COM port (Windows) or `/dev/ttyACM*` node (Linux) per port, each
with its own rings, line coding and DTR state.

A typical use is keeping a machine protocol and a human console apart, so log
output never corrupts a command stream.

## Hardware

- ESP32-S2 / S3 / P4 board with a native USB OTG port
- A USB host: a PC, or another ESP32 running EspUsbHost
- A separate Serial monitor connection for logs

## What It Does

- Registers two `EspUsbDeviceCdcSerial` functions, named `Console` and
  `Data Link`, plus a third named `Telemetry` on the ESP32-P4, where the
  high-speed controller has room for it
- Gives each port its own ring sizes with `configure()` (Console 128/1024,
  Data Link 1024/1024, Telemetry 64/2048 bytes RX/TX)
- Echoes characters received on any port back to that same port, prefixed with
  the port name
- Sends a `tick=...` message every 3 seconds to whichever ports are open
- Prints DTR / RTS and baud changes for every port to the regular Serial
  monitor, and each port's index and allocated capacities at startup

## Usage

1. Flash the sketch and open the regular Serial monitor.
2. Connect the USB device port to the host.
3. Two (P4: three) serial ports appear. Open them.
4. Type into any one; the reply comes back on that port only, and the Serial
   monitor shows which port received it.

## How Many Ports Fit

Each CDC port uses **two IN endpoints** (notification + data), and IN endpoints
are the scarce resource on these controllers:

| Controller | Non-control IN endpoints | CDC alone | Alongside HID + Vendor |
|---|---|---|---|
| ESP32-S2 / S3 | 4 | 2 ports | 1 port |
| ESP32-P4 full-speed controller | 4 | 2 ports | 1 port |
| ESP32-P4 high-speed controller | 7 | 3 ports | 2 ports |

`begin()` checks the whole configuration. Registering more than fits makes it
fail with `ESP_ERR_INVALID_SIZE` (read with `device.lastError()` /
`lastErrorName()`) before the USB PHY is started, so the host never sees a
broken device. There is no separate port-count query; the endpoint budget
depends on every Function in the configuration.

## Key APIs

- `EspUsbDeviceCdcSerial consolePort(device, "Console")` registers a named CDC
  port. The name reaches the host as the function's `iFunction` / `iInterface`
  string
- `port.configure(EspUsbBulkStreamConfig)` sets that port's capacities; each
  port allocates its own rings in `begin()`
- `port.port()` is the port's index in CDC descriptor order, assigned in
  `begin()`. It follows registration order
- `port.readCapacity()` / `writeCapacity()` report what was allocated
- `port.setTxTimeoutMs(50)` bounds how long `write()` waits when that port's
  terminal is not reading, so one stalled port does not stall the others
- Everything else (`available()`, `read()`, `write()`, `connected()`,
  `onRx()`, `onLineCoding()`, `onLineState()`, `onBreak()`) works per port,
  exactly as in [Serial](../Serial/)

## Notes

- Name the ports. Two ACM functions are otherwise identical, and without names
  a host has no way to tell them apart
- Set a unique `config.serialNumber`. Windows remembers COM port numbers per
  VID/PID/serial, so without one the numbers move when the device is replugged
  into a different port
- Each CDC port is wrapped in an Interface Association Descriptor (IAD), and a
  configuration with an IAD declares `bDeviceClass = 0xEF/0x02/0x01`. That is
  what makes Windows bind a driver per function rather than one driver across
  all interfaces
- Port order decides interface and endpoint numbers. Changing registration
  order changes them, which matters if a host-side script hardcodes them
- `config.controller = EspUsbController::FullSpeed` on the P4 drops the budget
  to four IN endpoints, and `begin()` refuses the third port

## Related

- [Serial](../Serial/) - single CDC ACM port
- [CompositeCdcMidi](../CompositeCdcMidi/) - CDC + MIDI with independent resources
- [USBVendor](../USBVendor/) - bulk vendor interface, 1 IN endpoint instead of 2
- [CDC API](../../docs/classes/cdc.md)

## Walkthrough and checks

### Before running

Expose independent CDC serial ports on one USB device. Follow the hardware requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

The `ports[]` table lists each port with its label and capacities; `configurePort()` applies them before `device.begin()`. The ports attach to the same device but have separate receive state and DTR. Open each OS port and compare its labeled response. Match each logical port by testing it rather than assuming the OS numbers are stable; the third port depends on the selected target.

### API use and adaptation

`device.ready()` concerns USB configuration; the CDC `connected()` state concerns host DTR. They answer different questions. The callback's receive amount is not a line or packet boundary: read the actual bytes and accumulate your own complete messages. Use `write()` for bytes and `print()`/`printf()` for text formatting. `flush()` is not a guarantee that the PC application consumed the data.

Line-coding callbacks report the host's requested serial parameters; they do not change USB speed. Keep callbacks short and use a synchronized handoff to `loop()` for larger parsing. For reliable commands, check the number of bytes accepted, keep any unsent suffix, and decide what partial input means after disconnect.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one port, capacity or controller. The demonstration's error logging is not a general retry or recovery policy.

## Changes from v2

- `EspUsbDevice::maxCdcPorts()` was removed: a port count alone cannot describe
  how CDC competes with other Functions for endpoints. `begin()` validates the
  whole configuration; the startup line prints each port's allocated
  capacities instead.
- Capacities are per port, set with `configure()`, instead of the shared
  `CFG_TUD_CDC_*` build flags.
- `port()` is a CDC descriptor-order index, no longer a TinyUSB instance.
- `onLineCoding()` is now also shown per port, and each port's `write()` wait is
  bounded with `setTxTimeoutMs()`.
- The ports are kept in a table, so the code no longer repeats per port.

- [Sketch](SerialMulti.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
