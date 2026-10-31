# EspUsbDevice FirmwareCDC

> 日本語版: [README.ja.md](README.ja.md)

The smallest firmware update there is: a length, then that many bytes, over a
plain CDC serial port. Useful when the host side has to be a script you control
rather than a standard tool, and as the shape to copy when adding an update
command to a device that already has a serial port for something else.

## Hardware

- An ESP32-S2, ESP32-S3 or ESP32-P4 board, connected through its **native USB OTG** port
- A PC with Python 3 and `pyserial`
- A separate Serial connection for logs

## Requirements

A partition scheme with **two application partitions**. The Arduino "Default"
scheme, used by every profile in `sketch.yaml`, has them. The sketch prints
`NO_OTA_PARTITION` when it finds only one, and answers `ERR no OTA partition`.
Keep a recovery route (the ROM loader) available before testing.

## Protocol

```
host -> device   "FW <size>\n"
device -> host   "READY\n"          (or "ERR <reason>\n")
host -> device   <size> bytes
device -> host   "OK\n"             (or "ERR <reason>\n"), then it restarts
```

Length-prefixed because CDC is a byte stream with no framing: without a length
the device cannot tell the end of the image from a pause. `<reason>` is a short
text or an `esp_err_t` name such as `ESP_ERR_INVALID_SIZE` (image larger than
the partition).

## Usage

```sh
uv run --with pyserial python3 firmware_cdc.py /dev/ttyACM0 build/FirmwareCDC.ino.bin
# or, with pyserial installed:
python3 firmware_cdc.py /dev/ttyACM0 build/FirmwareCDC.ino.bin   # COM5 etc. on Windows
```

[`firmware_cdc.py`](firmware_cdc.py) checks that the file starts with `0xE9`
(an ESP application image), sends it in 4 KiB chunks and reports the rate.
It is unchanged from 2.x: the protocol is the same.

The image is a plain Arduino `.bin` (`<build-path>/<sketch>.ino.bin`), not
`*.merged.bin` or `*.bootloader.bin`.

## Key APIs

- `EspUsbDeviceCdcSerial port(device, "Firmware")` is the transport;
  `port.configure(EspUsbBulkStreamConfig)` sizes it - here an 8 KiB RX ring
  and a 256-byte TX ring.
- `EspUsbDeviceFirmwareUpdate update` does the flash side:
  `begin(size)` / `write()` / `end()` / `abort()`, with `lastErrorName()`.
- `update.begin(size)` with the real size refuses an image larger than the
  partition **before the host sends a byte of it**.
- `update.end()` verifies before moving the boot partition, so a truncated
  upload fails there rather than at the next boot.
- `port.flush()` waits (up to `setTxTimeoutMs()`, 1000 ms by default) for the
  reply to leave, so `OK` reaches the host before the restart.

## Expected Serial Output

```text
Update target: app1 (1310720 bytes)
Ready. python3 firmware_cdc.py <port> <firmware.bin>
Receiving 301456 bytes into app1
1 KiB
...
294 KiB
Verified - restarting into the new firmware
```

## Notes

- **The flash writing happens in `loop()`, not in a USB callback.** Class
  callbacks run on the USB worker, and a flash erase takes milliseconds that
  would be taken from the whole device. This sketch polls `port.available()`
  instead - which is the pattern to copy, not an accident of how it was
  written. While `loop()` is erasing, the RX ring absorbs what arrives and the
  host is then NAKed; nothing is lost.
- Full-speed bulk caps at 1.216 MB/s on paper and CDC framing plus a flash
  write per chunk keeps you well under it. Fine for a 300 KB image; for a 1 MB
  one on a P4, [FirmwareVendor](../FirmwareVendor/) is the faster shape and
  [FirmwareDFU](../FirmwareDFU/) is the one with a standard host tool.
- The protocol has no authentication and no integrity check of its own - the
  image's own checksum is what `end()` verifies. Anything that can open the
  port can reflash the board. `markValid()` is not an authenticity check.
- The image you send replaces this sketch; send one that keeps an update route.
- Do not enable Arduino's built-in USB classes (USB MSC / DFU / CDC on boot)
  together with this library.

## Checks

The host sends `FW <size>` and waits for `READY` before streaming exactly that
many binary bytes. `readCommand()` and `readImage()` handle different states; a
serial terminal sending text is not a binary uploader. Receive progress,
`update.end()` verification and the restart are separate stages.

## Changes from v2

- The RX/TX rings are sized at run time with `port.configure()` (8 KiB RX, so
  USB keeps moving while `loop()` erases flash) instead of build-time CDC
  buffer macros; `loop()` reads 1 KiB at a time.
- `port.flush()` now waits for the reply to be sent (bounded by the TX
  timeout), which is what the `OK` before the restart relies on.
- Same protocol and host script as 2.x.

## See Also

- [FirmwareVendor](../FirmwareVendor/) - same job over vendor bulk, faster
- [FirmwareDFU](../FirmwareDFU/) - same job with `dfu-util`
- [Firmware update over USB](../../docs/ota-over-usb.md)
- [CDC API guide](../../docs/classes/cdc.md)
- [Sketch](FirmwareCDC.ino) / [All examples](../README.md) /
  [Example workflow](../../docs/example-workflow.md)
