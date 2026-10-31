# EspUsbDevice Info Tools

> 日本語版: [README.ja.md](README.ja.md)

Three diagnostic sketches for bringing up a board and narrowing down a problem.
They do not demonstrate a class. They show what the library built, whether the
host accepted it, and how the host reacts to single transfers.

| Sketch | Purpose | Needs a host | Run it |
| --- | --- | --- | --- |
| [EspUsbDeviceBringUpCheck](EspUsbDeviceBringUpCheck/README.md) | Checks `begin()`, enumeration, suspend and the host-to-device path in order. Prints a checklist when enumeration never happens | Yes | First, on every new board or cable |
| [EspUsbDeviceDescriptorDump](EspUsbDeviceDescriptorDump/README.md) | Prints every descriptor the library built and the endpoint budget against this target's controller | No | Next, when you choose or change a set of Functions |
| [EspUsbDeviceConsole](EspUsbDeviceConsole/README.md) | Sends HID reports and vendor transfers one at a time from a serial terminal, and prints every request coming from the host | Yes | When a host application does not react as expected |

## Which One to Use

- **The device does not appear on the host.** Run EspUsbDeviceBringUpCheck. A
  `BEGIN failed` line means the problem is on the board (descriptor, endpoint
  budget, setting). `BEGIN ok` without `MOUNTED` points at the cable,
  connector, power or the host.
- **`begin()` fails, or the host shows something other than you meant.** Run
  EspUsbDeviceDescriptorDump with the same Functions enabled. It shows the
  exact bytes you declare, at full and high speed, and whether the endpoints
  fit this controller. It also works with no cable
  (`EspUsbDeviceStartMode::DescriptorsOnly`).
- **The device enumerates, but an application ignores it.** Run
  EspUsbDeviceConsole. Try one usage or report at a time, and read the
  `HOST_` lines to see what the application sends.

All three print to the Serial monitor of the flashing port (UART or USB
Serial/JTAG), separate from the OTG port the library owns.
EspUsbDeviceConsole also takes its commands there.

## Related

- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
- [USB device guide](../../docs/usb-device-guide.md) - bring-up procedure and
  host-side tools
- [Troubleshooting](../../docs/troubleshooting.md)
