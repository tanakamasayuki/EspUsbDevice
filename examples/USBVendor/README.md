# EspUsbDevice USBVendor

> Japanese: [README.ja.md](README.ja.md)

This example creates a non-HID vendor-specific USB interface. `VendorHID` uses
HID reports for custom traffic. `USBVendor` uses bulk IN / OUT endpoints and
vendor control requests.

The goal is not Arduino-ESP32 `USBVendor` API compatibility. `EspUsbDeviceVendor`
uses a simple `available()` / `read()` / `write()` / `flush()` API with callbacks.

## Hardware

- USB-device-capable ESP32-S3 or similar Arduino-ESP32 board
- PC, another ESP32 running EspUsbHost, or another USB host
- Serial monitor connection for logs

## Behavior

- Builds a vendor-specific interface with `bInterfaceClass = 0xff`.
- Prints bulk OUT data as hex to Serial monitor.
- Echoes received data back through bulk IN as `echo: ...`.
- Responds to vendor control IN request `0x01` with `EspUsbDeviceVendor`.
- Acknowledges vendor control OUT request `0x02` with a status stage.
- Sends a status line through bulk IN every 3 seconds.

## Usage

1. Flash the sketch and open the normal Serial monitor.
2. Connect the USB device port to a PC or host.
3. Open the vendor-specific interface on the host.
4. Send bytes to bulk OUT and read the echo from bulk IN.
5. Send an IN control transfer with `bRequest = 0x01` to read device info.

On a PC, this usually requires a host-side implementation using libusb, WinUSB,
WebUSB, or a similar API.

## Main API

- `EspUsbDeviceVendor UsbVendor(device)` registers the vendor-specific function.
- `UsbVendor.available()` returns bytes received from bulk OUT.
- `UsbVendor.read()` reads bytes from the host.
- `UsbVendor.write()` / `print()` / `printf()` sends bytes through bulk IN.
- `UsbVendor.flush()` flushes pending IN data.
- `UsbVendor.mounted()` returns whether the device is enumerated by the host.
- `UsbVendor.onRx(callback)` runs on bulk OUT reception.
- `UsbVendor.onControlRequest(callback)` receives EP0 vendor requests.
- `UsbVendor.sendControlResponse(request, data, length)` responds to a control
  transfer.

## Difference From VendorHID

- `VendorHID` is easy to use through HID drivers and is suitable for small fixed
  reports.
- `USBVendor` is better for bulk transfer, PC applications, browser integration,
  and custom protocols.
- `USBVendor` may require host-side driver, permission, and interface-claiming
  code.

## Not Implemented Yet

- WebUSB BOS descriptor and landing URL will be added later.
- Microsoft OS 2.0 descriptors will be added when the Windows / WinUSB use case
  is clear.

## Related

- [VendorHID](../VendorHID/) - vendor-defined HID over HID reports
- [Serial](../Serial/) - CDC ACM serial
