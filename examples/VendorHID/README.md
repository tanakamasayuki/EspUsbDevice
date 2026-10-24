# EspUsbDevice VendorHID

> 日本語版: [README.ja.md](README.ja.md)

Creates a vendor-defined HID device and exchanges custom 63-byte payloads with
the host.

Vendor HID is useful when a host application needs a small custom protocol
without a dedicated kernel driver. This example uses report ID `6` for Input,
Output, and Feature reports.

63 bytes is the default, not the limit. The ceiling is
`CFG_TUD_HID_EP_BUFSIZE - 1` - 63 on ESP32-S2/S3 and **511 on ESP32-P4**, where
a high-speed interrupt endpoint carries the whole report in one packet every
125 us. `EspUsbDeviceHidVendor vendor(device, 511)` is all it takes, and the
report descriptor follows the size automatically. See
[docs/usb-device-advanced.md](../../docs/usb-device-advanced.md#62-maximum-packet-size).

## Hardware

- ESP32-S3 or another Arduino-ESP32 board with USB device support
- A USB host such as EspUsbHost, a PC application using hidapi, or a test fixture
- A separate Serial monitor connection for logs when available

## What It Does

- Starts `EspUsbDevice` as a full-speed vendor HID device
- Sends a 63-byte Input report to the host every second
- Receives host Output reports with `onOutputReport()` and prints a hex dump
- Receives host Feature reports with `onFeatureReport()` and prints a hex dump

## Key APIs

- `EspUsbDeviceHidVendor vendor(device)` registers the vendor-defined HID
  function.
- `vendor.sendInput(data, length)` sends an Input report to the host.
- `vendor.onOutputReport(callback)` receives Output reports from the host.
- `vendor.onFeatureReport(callback)` receives Feature reports from the host.
- In callbacks, inspect `EspUsbDeviceHidReport` fields: `reportId`,
  `reportType`, `data`, and `length`.

## Report Format

- Usage page: vendor-defined `0xff00`
- Report ID: `ESP_USB_DEVICE_HID_REPORT_ID_VENDOR` (`6`)
- Payload size: 63 bytes
- Interrupt IN endpoint: device to host
- Interrupt OUT endpoint: host to device
- Feature report: control transfer

The payload passed to `sendInput()` does not include the report ID. The library
adds it. Output / Feature callback `data` is also the payload portion.

## Notes

- Vendor HID requires a matching host-side application.
- On PC, use the OS HID API, hidapi, or similar libraries.
- Design this as fixed-size report exchange, not a byte stream.
- CDC ACM is a better fit for large continuous streams.

## See Also

- [Serial](../Serial/) - CDC ACM serial device
- [KeyboardMouse](../KeyboardMouse/) - composite HID device

## Walkthrough and checks

### Before running

Exchange vendor-defined HID Input, Output and Feature reports. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

The sketch periodically sends Input and prints host Output/Feature notifications. Use a HID-aware host client with the matching report IDs and lengths. An Input report is not a bulk stream, and a Feature request is not interchangeable with an Output interrupt report.

### API use and adaptation

A HID report is a structured state or change, not an arbitrary text line. Decode it with its report descriptor: distinguish report ID, usage page/usage, signed value and logical range. Keyboard usages identify key positions, mouse motion is relative, and button values are bitmaps. Match press with release, or compare complete states, before adding an application action.

Callback report data is borrowed. Copy the bytes or decoded values into owned storage before handing work to another task. Log only short diagnostics in the callback; for repeated raw dumps, terminal speed can become the bottleneck. If an input is missing, first confirm enumeration, then descriptor/report length, then application decoding. Do not change all three at once.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](VendorHID.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
