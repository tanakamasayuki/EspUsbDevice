# EspUsbDevice FirmwareVendor

> 日本語版: [README.ja.md](README.ja.md)

Firmware update over a vendor-specific interface: control requests for the
commands, a bulk OUT endpoint for the image. **The fastest route in the
library** — bulk OUT on an ESP32-P4 high-speed link moves an image in a fraction
of the time DFU's EP0 transfers take.

The cost is that the host side is yours to write and to ship.
[`FirmwareDFU`](../FirmwareDFU/) is the same job with a standard tool instead,
for no endpoints.

## Hardware

- An ESP32-S2, ESP32-S3 or ESP32-P4 board with USB device support
- A PC with Python and `pyusb`
- A separate Serial connection for logs

## Requirements

A partition scheme with **two application partitions**.

## Protocol

| Direction | Request | Meaning |
|---|---|---|
| control OUT `0x40` | `0x01`, `wValue`=size low, `wIndex`=size high | start |
| bulk OUT | — | the image |
| control OUT `0x40` | `0x02` | commit and restart |
| control OUT `0x40` | `0x03` | abort |
| control IN `0xc0` | `0x04` → 8 bytes | active flag, error, bytes written |

Commands on EP0 and data on bulk is the shape worth copying: the status request
can be answered at any time, including while the bulk endpoint is busy, so the
host can watch progress without interrupting the transfer.

## Usage

```sh
uv run --with pyusb python3 firmware_vendor.py build/FirmwareVendor.ino.bin
```

On Linux this needs write access to the device node — a udev rule for the
VID:PID, or `sudo`. On Windows the vendor interface gets the WinUSB driver
automatically from the Microsoft OS 2.0 descriptor this library emits.

## Key APIs

- `EspUsbDeviceVendor vendor(device)` is the transport; `onControlRequest()`
  handles the commands and `sendControlResponse()` answers them.
- `EspUsbDeviceFirmwareUpdate update` does the flash side.
- `vendor.available()` / `read()` drain the bulk FIFO from `loop()`.

## Notes

- **The control callback sets flags; `loop()` does the work.** Control requests
  and `onRx()` both run on the usbd task, and a flash erase there stalls the
  whole device. The bulk endpoint NAKs while `loop()` writes, which is the host
  slowing down rather than the device stalling.
- On ESP32-P4 raise `CFG_TUD_VENDOR_RX_BUFSIZE` from `build_opt.h` to keep the
  FIFO ahead of the flash writes; the library's defaults are sized for the
  common case, not for a sustained one-way stream
  ([advanced guide, 5.4](../../docs/usb-device-advanced.md#54-buffer-sizes)).
- The protocol has no authentication. Anything that can claim the interface can
  reflash the board.
- This library is not designed to run together with Arduino's built-in USB
  device classes.

## Walkthrough and checks

### Before running

Receive firmware through Vendor bulk with control-plane commands. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Control requests reserve start/commit/abort work, while `loop()` performs update operations and drains bulk bytes. Follow the documented request numbers and status reply rather than treating control acknowledgment as completed Flash programming. A host uploader must also handle partial input and reported errors.

### API use and adaptation

Use the intended application image for the selected target and partition layout, not a merged Flash dump or bootloader binary. Follow availability/capacity checks before beginning the upload. Transport acknowledgment, received byte count, image validation, boot selection and a successful next boot are different stages. The ROM-loader example changes boot mode rather than using the same in-application update path.

Keep the documented recovery/upload route available before testing. Ordinary firmware replacement can leave NVS data intact, so do not assume a fresh application has fresh settings. `markValid()` concerns application validation, not authenticity of arbitrary received images; deployed products need an explicit authorization and image-verification policy. Reboot/Flash work belongs in the execution path used by the example, not in newly added long-running USB callbacks.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](FirmwareVendor.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
