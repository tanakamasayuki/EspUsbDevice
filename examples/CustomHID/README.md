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
