# EspUsbDevice CustomHID

> 日本語版: [README.ja.md](README.ja.md)

Creates a custom HID device from a sketch-provided HID report descriptor.

This example sends an 8-byte Input report on vendor-defined usage page `0xff00`
every second. It is a starting point for small HID devices that do not fit the
built-in Keyboard, Mouse, Gamepad or VendorHID classes.

## Hardware

- ESP32-S3, ESP32-S2 or ESP32-P4 board; the native USB OTG port goes to the Host
- A USB Host such as EspUsbHost, a HID parser, or a custom Host application
- A separate debug connection for `Serial` (115200) logs

## What It Does

- Defines `reportDescriptor` in the sketch
- Passes the descriptor and the interrupt IN packet size (8 bytes) to
  `EspUsbDeviceHidCustom`
- Prints the Input / Output / Feature report sizes that `begin()` derived from
  the descriptor
- While the Host has the device configured, sends an 8-byte report every second
- Prints the sent counter

## Key APIs

- `EspUsbDeviceHidCustom customHid(device, descriptor, descriptorLength,
  inputReportSize)` registers the custom HID Function. `inputReportSize` (default
  64) is the interrupt IN `wMaxPacketSize` to declare, report ID byte included: up
  to 64 at full speed (larger values are capped at 64) and up to 1024 at high
  speed. A longer report is still sent, over several packets of one transfer.
  The report storage is derived from the descriptor, not from this value.
- When the Custom Function shares one HID interface with other HID Functions,
  that interface's packet is the largest one any member asks for (at least 16).
- `customHid.inputPacketSize()` returns the interrupt IN `wMaxPacketSize` at the
  speed the Host negotiated (0 before the bus reset and after `end()`).
- `customHid.sendReport(data, length)` sends an Input report without a report ID.
- `customHid.sendReport(data, length, reportId)` is used for descriptors with a
  report ID. When combined with other HID Functions, the assigned ID is used.
- `customHid.inputReportCapacity()` / `outputReportCapacity()` /
  `featureReportCapacity()` return the sizes `begin()` found, including a report
  ID byte when one is declared (0 before `begin()`).
- `customHid.setFeatureReport(data, length, reportId)` sets the value a Host
  GET_REPORT(Feature) reads, for descriptors that declare a Feature report.

## Descriptor

This example descriptor defines:

- Usage page: vendor-defined `0xff00`
- Report ID: none
- Input report: 8 bytes (Report Count 8, Report Size 8 bits)
- No Output or Feature report, so no OUT endpoint storage is allocated

`begin()` parses the descriptor (Report Size / Count / ID, Push/Pop) and rejects
truncated items, unbalanced Push/Pop, an explicit Report ID 0, a mix of numbered
and unnumbered reports, and reports that are too long. It is not a full check of
the usages and their meaning. Keep the descriptor array valid and unchanged while
the device runs.

## Expected Serial Output

```text
USB custom HID ready (input=8 output=0 feature=0 bytes)
CUSTOM_HID_TX counter=0
CUSTOM_HID_TX counter=1
```

## Difference From VendorHID

- `VendorHID` provides a predefined vendor-defined descriptor and Output /
  Feature callbacks.
- `CustomHID` lets the sketch fully define the descriptor.
- Use `CustomHID` to validate custom descriptors. Use `VendorHID` for simple
  custom binary exchange.

## Checks

- Read `reportDescriptor` together with the bytes passed to `sendReport()`.
  Match report ID, field count and report length in the Host reader.
- This example has no Output or Feature protocol. To receive Output reports,
  use `VendorHID` or derive your own class from `EspUsbHidFunction`.

## Changes from v2

- Reports are sent only while `device.ready()` is true.
- The descriptor is parsed at `begin()`; a malformed one now fails `begin()`
  (`customHid.startError()` gives the cause) instead of reaching the Host.
- `inputReportSize` sets the declared interrupt IN packet, also in a composite
  HID interface and at high speed. v2 sized buffers from `CFG_TUD_HID_EP_BUFSIZE`,
  which is removed; reports longer than one packet are sent as several packets of
  one transfer.
- New: `inputReportCapacity()` and the other capacity queries, `inputPacketSize()`,
  and `setFeatureReport()`.
- `sendReport(data, length[, reportId])` is unchanged.

## See Also

- [VendorHID](../VendorHID/) - predefined vendor-defined HID
- [Gamepad](../Gamepad/) - HID gamepad
- [HID API guide](../../docs/classes/hid.md)
- [All examples](../README.md)
