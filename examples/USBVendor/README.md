# EspUsbDevice USBVendor

> 日本語版: [README.ja.md](README.ja.md)

This example creates a non-HID vendor-specific USB interface. A vendor HID
uses HID reports for custom traffic; `USBVendor` uses bulk IN / OUT endpoints
and vendor control requests.

The goal is not Arduino-ESP32 `USBVendor` API compatibility. `EspUsbDeviceVendor`
uses a simple `available()` / `read()` / `write()` / `flush()` API with callbacks,
and is an Arduino `Print`.

## Hardware

- ESP32-S2 / S3 / P4 board with a native USB OTG port
- PC, another ESP32 running EspUsbHost, or another USB host
- Serial monitor connection for logs

## Behavior

- Builds a vendor-specific interface with `bInterfaceClass = 0xff`, with a
  512-byte receive ring and a 1024-byte send ring set by `configure()`.
- Prints bulk OUT data as hex to the Serial monitor, and logs each `onRx()`
  notification.
- Echoes received data back through bulk IN as `echo: ...`, waiting until the
  whole reply fits so it is never cut short.
- Responds to vendor control IN request `0x10` with `EspUsbDeviceVendor`.
- Accepts vendor control OUT request `0x11` with up to 64 data bytes (or none),
  checks the received length in the DATA stage and prints the bytes once the
  transfer has completed.
- Sets a WebUSB landing URL with `EspUsbDeviceConfig::webusbEnabled` and
  `webusbUrl`, and a Windows `DeviceInterfaceGUIDs` value with
  `deviceInterfaceGuid`.
- Logs the WebUSB / Microsoft OS 2.0 requests the library answers itself, via
  `device.onAnyControlRequest()`.
- Sends a status line through bulk IN every 3 seconds.

## Usage

1. Flash the sketch and open the normal Serial monitor.
2. Connect the USB device port to a PC or host.
3. Open the vendor-specific interface on the host.
4. Send bytes to bulk OUT and read the echo from bulk IN.
5. Send an IN control transfer (`bmRequestType = 0xc0`, `bRequest = 0x10`) to
   read device info, or an OUT transfer (`0x40`, `0x11`) with a few bytes.

On a PC, this usually requires a host-side implementation using libusb, WinUSB,
WebUSB, or a similar API. On Windows the library's Microsoft OS 2.0 descriptor
binds WinUSB without an `.inf` file.

```python
# pyusb sketch of the host side
import usb.core
dev = usb.core.find(idVendor=0x303a, idProduct=0x4019)
print(bytes(dev.ctrl_transfer(0xc0, 0x10, 0, 0, 64)))
dev.ctrl_transfer(0x40, 0x11, 0, 0, b"\x01\x02\x03")
```

## Main API

- `EspUsbDeviceVendor vendor(device)` registers the vendor-specific function.
  An optional second argument sets the full-speed bulk packet size. USB 2.0
  5.8.3 allows only 8/16/32/64 at full speed; any other value makes
  `device.begin()` fail with `ESP_ERR_INVALID_SIZE`. High speed always uses 512.
- `vendor.configure(EspUsbVendorConfig)` sets the rings and receive mode.
  Only while stopped.
- `vendor.available()` / `read()` read bytes received on bulk OUT; they never
  wait.
- `vendor.write()` / `print()` / `printf()` copy bytes into the send ring and
  return the number accepted. They never wait.
- `vendor.waitWritable(n, timeoutMs)` waits until `n` bytes fit;
  `writeAvailable()` reports the free space now.
- `vendor.flush()` starts sending what is queued (needed only with
  `autoFlush = false`); it does not wait for completion.
- `vendor.mounted()` returns whether the host has configured the device and the
  endpoints are open.
- `vendor.onRx(callback)` runs on bulk OUT reception.
- `vendor.onControlRequest(callback)` receives EP0 vendor requests at the
  SETUP, DATA and ACK stages.
- `vendor.sendControlResponse(request, data, length)` starts the data stage: the
  bytes to send for IN, the receive buffer for OUT. With `wLength = 0` it sends
  only the status stage.
- `request.actualBytes` is the number of data-stage bytes actually transferred.
- `device.onAnyControlRequest(observer)` sees every control request, including
  those the library answers before `onControlRequest()`. It returns `esp_err_t`:
  set it while stopped; while started it returns `ESP_ERR_INVALID_STATE`.
- `config.webusbEnabled` adds the WebUSB BOS capability; `config.webusbUrl`
  sets the landing page. `https://` and `http://` are encoded with the WebUSB
  scheme byte.
- `config.deviceInterfaceGuid` sets the GUID Windows applications use to find
  this interface. Generate your own for a product.

## Vendor Request Codes

The library answers two exact vendor requests before `onControlRequest()`
runs:

| Request | bmRequestType | bRequest | wValue | wIndex | When |
|---|---|---|---|---|---|
| WebUSB GET_URL | `0xC0` | `0x01` | URL index (1 answered, others stall) | `2` | `webusbEnabled = true` |
| Microsoft OS 2.0 descriptor set | `0xC0` | `0x02` | `0` | `7` | a Vendor or DFU Function exists |

Any other request with the same `bRequest` (another direction, `wIndex` or
`wValue`) reaches `onControlRequest()`. This example still uses `0x10` and
`0x11` for its own protocol, which keeps it clear of both codes.

## Difference From a Vendor HID

- A vendor HID is easy to use through HID drivers and suits small fixed
  reports.
- `USBVendor` is better for bulk transfer, PC applications, browser integration,
  and custom protocols.
- `USBVendor` may require host-side driver, permission, and interface-claiming
  code.

## Notes

- Callbacks run on the USB worker and must be registered before `begin()`.
  Keep them short; do not wait or call `device.end()` there.
- The control OUT buffer must stay valid until the transfer ends, so it is a
  global array.
- Returning `false` at SETUP or DATA stalls the request. Returning `false` at
  ACK cannot undo a completed transfer.
- Bulk data is a byte stream: several `write()` calls may arrive in one host
  read, and the default for Vendor is no ZLP after a packet-sized write. Frame
  your messages with a length.

## Related

- [VendorEcho](../VendorEcho/) - minimal bulk echo that keeps partial writes
- [ReconfigureVendor](../ReconfigureVendor/) - change ring sizes at run time
- [Serial](../Serial/) - CDC ACM serial
- [Vendor API](../../docs/classes/vendor.md)

## Walkthrough and checks

### Before running

Echo vendor bulk data and answer explicit control requests. Follow the hardware requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

A host client must open this vendor interface; it is not an OS serial port. `onRx()` only records the notification; `loop()` reads and echoes, so a slow host cannot block the USB worker. `handleControl()` answers 0x10 and 0x11, which have different directions and stage handling. Requests it does not define are stalled.

### API use and adaptation

USB IN/OUT is always named from the host's viewpoint: IN goes to the host, OUT goes to the device. Keep endpoint direction separate from the application roles of request and response. A bulk transfer is a byte stream, not necessarily a whole command; use the actual length and protocol framing. Control transfers also carry direction, request, value, index and length, not just a payload.

A USB transfer accepted or completed is not an application acknowledgment. Check error/status replies and recover protocol state on disconnect. Callback pointers may be reused after return. For borrowed buffers (`writeDirect` / `readDirect`), obey the submit/completion ownership rules and do not overwrite in-flight data. Start with a short known request before tuning rings or transfer sizes.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one request, capacity or URL. The demonstration's error logging is not a general retry or recovery policy.

## Changes from v2

- The request codes moved from `0x01` / `0x02` to `0x10` / `0x11`. In v2 too,
  `0x01` was taken by WebUSB (enabled in that example) and `0x02` with
  `wIndex = 7` by Microsoft OS 2.0, so the info request never reached the
  sketch.
- The OUT request now receives its data into a buffer and is handled per stage.
  In v3 an OUT request with data needs a receive buffer; v2 acknowledged only
  the status stage.
- Echo moved from the `onRx()` callback to `loop()`, and waits with
  `waitWritable()` instead of possibly cutting the reply short. `flush()` is no
  longer needed with the default `autoFlush`.
- The WebUSB URL includes `https://`. Without a scheme the URL descriptor says
  "full URL included", and a host may reject `example.com/...`.
- New: `deviceInterfaceGuid` (v2 had no GUID setting), `onAnyControlRequest()`,
  `actualBytes`, and ring sizes set with `configure()`.

- [Sketch](USBVendor.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
