# EspUsbDevice VendorHID

> 日本語版: [README.ja.md](README.ja.md)

Creates a vendor-defined HID device and exchanges custom 63-byte payloads with
the Host.

Vendor HID is useful when a Host application needs a small custom protocol
without a dedicated kernel driver. This example uses report ID `6` for Input,
Output and Feature reports.

63 bytes is the default, not the limit. Change `reportBytes` in the sketch to
any size up to `EspUsbDeviceHidVendor::maxReportSize()` (65534); the report
descriptor follows it automatically. At full speed (S2/S3, P4 FS) a report larger
than one 64-byte packet is sent as several packets of one transfer; at high speed
(P4 HS) the interrupt packet grows to `reportBytes + 1`, up to 1024 bytes.
`maxReportSize()` is not a promise that the memory is available.

## Hardware

- ESP32-S3, ESP32-S2 or ESP32-P4 board; the native USB OTG port goes to the Host
- A USB Host such as EspUsbHost, a PC application using hidapi, or a test fixture
- A separate debug connection for `Serial` (115200) logs

## What It Does

- Starts `EspUsbDevice` as a vendor HID device with a 63-byte payload
- Sets the initial Feature report value with `setFeatureReport()`
- Prints the payload size and the Input / Output / Feature capacities
- While the Host has the device configured, sends a 63-byte Input report every second
- Receives Host Output reports with `onOutputReport()` and prints a hex dump
- Receives Host Feature reports (SET_REPORT) with `onFeatureReport()` and prints a hex dump

## Key APIs

- `EspUsbDeviceHidVendor vendor(device, reportSize)` registers the vendor-defined
  HID Function; `reportSize` defaults to 63.
- `vendor.sendInput(data, length)` sends an Input report. It returns false for
  an empty payload or one longer than `reportSize()`; it never truncates.
  A shorter payload is sent as is.
- `vendor.onOutputReport(callback)` receives Output reports from the Host.
- `vendor.onFeatureReport(callback)` receives Feature reports from the Host.
  Both return `esp_err_t` and must be registered before `device.begin()`.
- `vendor.setFeatureReport(data, length)` sets the value the Host reads with
  GET_REPORT(Feature) without sending anything. Valid after `begin()`.
- `vendor.reportSize()`, `vendor.inputReportCapacity()`,
  `vendor.outputReportCapacity()`, `vendor.featureReportCapacity()` report the sizes.
- In callbacks, inspect `EspUsbDeviceHidReport` fields: `reportId`,
  `reportType`, `data` and `length`. `data` is valid only during the callback.

## Report Format

- Usage page: vendor-defined `0xff00`
- Report ID: `ESP_USB_DEVICE_HID_REPORT_ID_VENDOR` (`6`)
- Payload size: 63 bytes (64 with the report ID)
- Interrupt IN endpoint: Device to Host
- Interrupt OUT endpoint: Host to Device
- Feature report: control transfer

The payload passed to `sendInput()` and `setFeatureReport()` does not include
the report ID; the library adds it. Output / Feature callback `data` is also the
payload portion.

## Expected Serial Output

```text
USB vendor HID ready (payload=63 input=64 output=64 feature=64 bytes)
VENDOR_INPUT counter=0
VENDOR_OUTPUT report_id=6 type=2 len=63 data=01 02 03 00 ...
VENDOR_FEATURE report_id=6 type=3 len=63 data=0a 0b 0c 00 ...
```

## Notes

- Vendor HID needs a matching Host-side application. On a PC, use the OS HID
  API, hidapi, or similar libraries.
- Design this as a fixed-size report exchange, not a byte stream. An Input report
  is not a bulk stream, and a Feature request is not interchangeable with an
  Output interrupt report.
- CDC ACM or a vendor bulk interface fits large continuous streams better.
- A true result from `sendInput()` means the report was copied and queued, not
  that the Host read it.

## Changes from v2

- Larger reports no longer need a build setting. v2 capped `reportSize` at
  `CFG_TUD_HID_EP_BUFSIZE - 1` (63 on S2/S3, 511 on P4) set in `build_opt.h`;
  that setting is removed and defining it is a build error. v3 accepts up to
  65534 and sizes the storage at `begin()`.
- `onOutputReport()` / `onFeatureReport()` now return `esp_err_t`
  (`ESP_OK` before `begin()`, `ESP_ERR_INVALID_STATE` after). The sketch checks it.
- `sendInput()` rejects a payload longer than `reportSize()` instead of truncating it.
- New: `setFeatureReport()` to preset the GET_REPORT(Feature) value, and the
  `inputReportCapacity()` / `outputReportCapacity()` / `featureReportCapacity()` queries.
- Messages are sent only while `device.ready()` is true.

## See Also

- [CustomHID](../CustomHID/) - HID with a sketch-defined descriptor
- [VendorEcho](../VendorEcho/) - vendor bulk interface
- [KeyboardMouse](../KeyboardMouse/) - composite HID device
- [HID API guide](../../docs/classes/hid.md)
- [All examples](../README.md)
