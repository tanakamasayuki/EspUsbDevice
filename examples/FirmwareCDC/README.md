# EspUsbDevice FirmwareCDC

> 日本語版: [README.ja.md](README.ja.md)

The smallest firmware update there is: a length, then that many bytes, over a
plain CDC serial port. Useful when the host side has to be a script you control
rather than a standard tool, and as the shape to copy when adding an update
command to a device that already has a serial port for something else.

## Hardware

- An ESP32-S2, ESP32-S3 or ESP32-P4 board with USB device support
- A PC with Python and `pyserial`
- A separate Serial connection for logs

## Requirements

A partition scheme with **two application partitions**.

## Protocol

```
host -> device   "FW <size>\n"
device -> host   "READY\n"          (or "ERR <reason>\n")
host -> device   <size> bytes
device -> host   "OK\n"             (or "ERR <reason>\n"), then it restarts
```

Length-prefixed because CDC is a byte stream with no framing: without a length
the device cannot tell the end of the image from a pause.

## Usage

```sh
uv run --with pyserial python3 firmware_cdc.py /dev/ttyACM0 build/FirmwareCDC.ino.bin
```

## Key APIs

- `EspUsbDeviceCdcSerial port(device, "Firmware")` is the transport.
- `EspUsbDeviceFirmwareUpdate update` does the flash side:
  `begin(size)` / `write()` / `end()` / `abort()`.
- `update.begin(size)` with the real size refuses an image larger than the
  partition **before the host sends a byte of it**.
- `update.end()` verifies before moving the boot partition, so a truncated
  upload fails there rather than at the next boot.

## Notes

- **The flash writing happens in `loop()`, not in a USB callback.** Every class
  callback in this library runs on the usbd task, and a flash erase takes
  milliseconds that would be taken from the whole device. This sketch polls
  `port.available()` instead - which is the pattern to copy, not an accident of
  how it was written.
- Full-speed bulk caps at 1.216 MB/s on paper and CDC framing plus a flash write
  per chunk keeps you well under it. Fine for a 300 KB image; for a 1 MB one on
  a P4, [`FirmwareVendor`](../FirmwareVendor/) is the faster shape and
  [`FirmwareDFU`](../FirmwareDFU/) is the one with a standard host tool.
- The protocol has no authentication and no integrity check of its own - the
  image's own checksum is what `end()` verifies. Anything that can open the port
  can reflash the board.
- This library is not designed to run together with Arduino's built-in USB
  device classes.

## Walkthrough and checks

### Before running

Receive a length-delimited firmware image through CDC. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

The host first sends the FW size command and waits for readiness before streaming exactly that many binary bytes. `readCommand()` and `readImage()` handle different states; a serial terminal sending text is not a binary uploader. Distinguish receive progress from `update.end()` verification and restart.

### API use and adaptation

Use the intended application image for the selected target and partition layout, not a merged Flash dump or bootloader binary. Follow availability/capacity checks before beginning the upload. Transport acknowledgment, received byte count, image validation, boot selection and a successful next boot are different stages. The ROM-loader example changes boot mode rather than using the same in-application update path.

Keep the documented recovery/upload route available before testing. Ordinary firmware replacement can leave NVS data intact, so do not assume a fresh application has fresh settings. `markValid()` concerns application validation, not authenticity of arbitrary received images; deployed products need an explicit authorization and image-verification policy. Reboot/Flash work belongs in the execution path used by the example, not in newly added long-running USB callbacks.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](FirmwareCDC.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
