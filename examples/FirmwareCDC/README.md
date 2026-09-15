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
