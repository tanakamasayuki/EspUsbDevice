# EspUsbDevice CustomHID

> 日本語版: [README.ja.md](README.ja.md)

Creates a custom HID device from a sketch-provided HID report descriptor.

This example sends an 8-byte Input report on vendor-defined usage page `0xff00`
every second. It is a starting point for small HID devices that do not fit the
built-in Keyboard, Mouse, Gamepad, or VendorHID classes.

## Hardware

- ESP32-S3 or another Arduino-ESP32 board with USB device support
- A USB host such as EspUsbHost, a HID parser, or a custom host application
- A separate Serial monitor connection for logs when available

## What It Does

- Defines `REPORT_DESCRIPTOR` in the sketch
- Passes the descriptor and input report size to `EspUsbDeviceHidCustom`
- Sends an 8-byte report to the host every second
- Prints the sent counter to Serial monitor

## Key APIs

- `EspUsbDeviceHidCustom customHid(device, descriptor, descriptorLength,
  inputReportSize)` registers the custom HID function.
- `customHid.sendReport(data, length)` sends an Input report without a report ID.
- `customHid.sendReport(data, length, reportId)` is used for descriptors with a
  report ID.

## Descriptor

This example descriptor defines:

- Usage page: vendor-defined `0xff00`
- Report ID: none
- Input report size: 8 bytes
- Report count: 8
- Report size: 8 bits

If the descriptor and `inputReportSize` do not match, the host parser and
endpoint MPS expectations can diverge from the actual report length. Start with
small fixed-size reports first.

## Difference From VendorHID

- `VendorHID` provides a predefined vendor-defined descriptor and Output /
  Feature callbacks.
- `CustomHID` lets the sketch fully define the descriptor.
- Use `CustomHID` to validate custom descriptors. Use `VendorHID` for simple
  custom binary exchange.

## See Also

- [VendorHID](../VendorHID/) - predefined vendor-defined HID
- [Gamepad](../Gamepad/) - HID gamepad device

## Walkthrough and checks

### Before running

Describe and send a custom input-only HID report. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Read REPORT_DESCRIPTOR together with the bytes passed to `customHid.sendReport()`. Match report ID, field count and report length in the host reader; this example does not automatically provide an Output or Feature protocol. Keep the descriptor array alive for the device lifetime.

### API use and adaptation

A HID report is a structured state or change, not an arbitrary text line. Decode it with its report descriptor: distinguish report ID, usage page/usage, signed value and logical range. Keyboard usages identify key positions, mouse motion is relative, and button values are bitmaps. Match press with release, or compare complete states, before adding an application action.

Callback report data is borrowed. Copy the bytes or decoded values into owned storage before handing work to another task. Log only short diagnostics in the callback; for repeated raw dumps, terminal speed can become the bottleneck. If an input is missing, first confirm enumeration, then descriptor/report length, then application decoding. Do not change all three at once.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](CustomHID.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
