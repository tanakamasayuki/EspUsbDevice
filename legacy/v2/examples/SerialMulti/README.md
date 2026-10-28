# EspUsbDevice SerialMulti

> 日本語版: [README.ja.md](README.ja.md)

Creates two independent USB CDC ACM serial ports on a single device. The host
sees two COM ports (Windows) or two `/dev/ttyACM*` nodes (Linux), each with its
own buffers, line coding and DTR state.

A typical use is keeping a machine protocol and a human console apart, so log
output never corrupts a command stream.

## Hardware

- ESP32-S2 / S3 / P4 board with USB device support
- A USB host: a PC, or another ESP32 running EspUsbHost
- A separate Serial monitor connection for logs

## What It Does

- Registers two `EspUsbDeviceCdcSerial` functions, named `Console` and `Data Link`,
  plus a third named `Telemetry` on the ESP32-P4, where the endpoint budget has
  room for it
- Echoes characters received on either port back to that same port, prefixed
  with the port name
- Sends a `tick=...` message every 3 seconds to whichever ports are open
- Prints DTR / RTS changes for both ports to the regular Serial monitor

## Usage

1. Flash the sketch and open the regular Serial monitor.
2. Connect the USB device port to the host.
3. Two serial ports appear. Open both.
4. Type into either one; the reply comes back on that port only, and the
   Serial monitor shows which port received it.

## How Many Ports Fit

Each CDC port costs **two IN endpoints** (notification + data), and IN endpoints
are the scarce resource on these controllers:

| Controller | CDC alone | Alongside HID + Vendor |
|---|---|---|
| ESP32-S2 / S3 | 2 ports (uses all 4 IN endpoints) | 1 port |
| ESP32-P4 full-speed controller | 2 ports | 1 port |
| ESP32-P4 high-speed controller | 3 ports | 2 ports |

Registering more than fits makes `begin()` fail with `ESP_ERR_INVALID_SIZE`
before the USB PHY is started, so the host never sees a broken device.
`EspUsbDevice::maxCdcPorts()` reports the capacity this build was compiled with.

## Key APIs

- `EspUsbDeviceCdcSerial Console(device, "Console")` registers a named CDC port.
  The name reaches the host as the function's `iFunction` / `iInterface` string
- `Console.port()` is the port index, which is also the TinyUSB instance the
  object drives. It follows registration order
- `EspUsbDevice::maxCdcPorts()` is the compile-time port capacity for this SoC
- Everything else (`available()`, `read()`, `write()`, `connected()`,
  `onRx()`, `onLineCoding()`, `onLineState()`) works per port, exactly as in
  [Serial](../Serial/)

## Notes

- Name the ports. Two ACM functions are otherwise identical, and without names
  a host has no way to tell them apart
- Set a unique `config.serialNumber`. Windows remembers COM port numbers per
  VID/PID/serial, so without one the numbers move when the device is replugged
  into a different port
- A configuration with several functions automatically declares
  `bDeviceClass = 0xEF/0x02/0x01`, which is what makes Windows bind a driver per
  function rather than one driver across all interfaces
- Port order decides endpoint addresses. Changing registration order changes
  them, which matters if a host-side script hardcodes addresses

## Related

- [Serial](../Serial/) - single CDC ACM port
- [CompositeHidCdcMsc](../CompositeHidCdcMsc/) - HID + CDC + MSC in one device
- [USBVendor](../USBVendor/) - bulk vendor interface, 1 IN endpoint instead of 2

## Walkthrough and checks

### Before running

Expose independent CDC serial ports on one USB device. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Console and DataLink attach to the same device but have separate receive state and DTR. Open each OS port and compare its labeled response. Match each logical port by testing it rather than assuming the OS numbers are stable; the conditional additional port depends on the selected target.

### API use and adaptation

`device.ready()` concerns USB configuration; the CDC `connected()` state concerns host DTR. They answer different questions. The callback's receive amount is not a line or packet boundary: read the actual bytes and accumulate your own complete messages. Use `write()` for bytes and `print()`/`printf()` for text formatting. `flush()` is not a guarantee that the PC application consumed the data.

Line-coding callbacks report the host's requested serial parameters; they do not change USB speed. Keep callbacks short and use a synchronized handoff to `loop()` for larger parsing. For reliable commands, check the number of bytes accepted, keep any unsent suffix, and decide what partial input means after disconnect.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](SerialMulti.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
