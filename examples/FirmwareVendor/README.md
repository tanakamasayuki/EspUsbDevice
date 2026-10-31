# EspUsbDevice FirmwareVendor

> 日本語版: [README.ja.md](README.ja.md)

Firmware update over a vendor-specific interface: control requests for the
commands, a bulk OUT endpoint for the image. **The fastest route in the
library** - bulk OUT on an ESP32-P4 high-speed link moves an image in a
fraction of the time DFU's EP0 transfers take.

The cost is that the host side is yours to write and to ship.
[FirmwareDFU](../FirmwareDFU/) is the same job with a standard tool instead,
for no endpoints.

## Hardware

- An ESP32-S2, ESP32-S3 or ESP32-P4 board, connected through its **native USB OTG** port
- A PC with Python 3 and `pyusb` (plus a libusb backend)
- A separate Serial connection for logs

## Requirements

A partition scheme with **two application partitions**. The Arduino "Default"
scheme, used by every profile in `sketch.yaml`, has them. The sketch prints
`NO_OTA_PARTITION` when it finds only one. Keep a recovery route (the ROM
loader) available before testing.

## Protocol

| Direction | Request | Meaning |
|---|---|---|
| control OUT `0x40` | `0x01`, `wValue`=size low 16, `wIndex`=size high 16 | start |
| bulk OUT | - | the image |
| control OUT `0x40` | `0x02` | commit (verify) and restart |
| control OUT `0x40` | `0x03` | abort |
| control IN `0xc0` | `0x04` -> 8 bytes | `[0]` active, `[1..3]` last `esp_err_t` (LE), `[4..7]` bytes written (LE) |

Commands on EP0 and data on bulk is the shape worth copying: the status request
can be answered at any time, including while the bulk endpoint is busy, so the
host can watch progress without interrupting the transfer. Any other vendor
request is stalled.

## Usage

```sh
uv run --with pyusb python3 firmware_vendor.py build/FirmwareVendor.ino.bin
```

[`firmware_vendor.py`](firmware_vendor.py) finds `303a:4000`, claims the vendor
interface, sends START, streams the image in 16 KiB bulk transfers, polls
STATUS until the device has written everything, then sends COMMIT.

On Linux this needs write access to the device node - a udev rule such as
`SUBSYSTEM=="usb", ATTR{idVendor}=="303a", ATTR{idProduct}=="4000", MODE="0666"`,
or `sudo`. On Windows the vendor interface gets the WinUSB driver automatically
from the Microsoft OS 2.0 descriptor this library emits.

The image is a plain Arduino `.bin` (`<build-path>/<sketch>.ino.bin`), not
`*.merged.bin` or `*.bootloader.bin`.

## Key APIs

- `EspUsbDeviceVendor vendor(device)` is the transport;
  `vendor.configure(EspUsbVendorConfig)` sizes it. Here a 16 KiB RX ring and
  no TX ring (`txCapacity = 0`, `transferBytes = 0`), since replies go over EP0.
- `vendor.onControlRequest()` handles the commands (called at SETUP, DATA and
  ACK; this sketch decides at SETUP) and `sendControlResponse()` answers them.
  Register it while stopped; it returns `esp_err_t`.
- `EspUsbDeviceFirmwareUpdate update` does the flash side:
  `begin(size)` / `write()` / `end()` / `abort()` / `written()` / `lastError()`.
- `vendor.available()` / `read()` drain the RX ring from `loop()`.

## Expected Serial Output

```text
Ready. python3 firmware_vendor.py <firmware.bin>
Receiving 301456 bytes into app1
1 KiB
...
294 KiB
Verified - restarting into the new firmware
```

## Notes

- **The control callback sets flags; `loop()` does the work.** Control
  requests run on the USB worker, and a flash erase there stalls the whole
  device. When the RX ring is full the bulk endpoint NAKs while `loop()`
  writes, which is the host slowing down rather than the device stalling.
- The RX ring size is the knob for sustained throughput, especially on an
  ESP32-P4 at high speed: a deeper ring keeps the endpoint ahead of the flash
  writes. It is a run-time `configure()` value; nothing needs rebuilding with
  other flags.
- The protocol has no authentication. Anything that can claim the interface can
  reflash the board. `markValid()` is not an authenticity check.
- The image you send replaces this sketch; send one that keeps an update route.
- Do not enable Arduino's built-in USB classes (USB MSC / DFU / CDC on boot)
  together with this library.

## Checks

Control requests only reserve start/commit/abort work; `loop()` performs the
update and drains bulk bytes. A control acknowledgment is not completed flash
programming - the host watches `written` in STATUS and only then commits. A
host uploader must also handle partial input and reported errors.

## Changes from v2

- The RX ring is sized at run time with `vendor.configure()` (16 KiB, no TX
  ring) instead of `CFG_TUD_VENDOR_RX_BUFSIZE` in `build_opt.h`.
- The callback checks that the request is a vendor request before acting on
  it.
- STATUS now carries the low 24 bits of the last `esp_err_t` in bytes 1..3
  (v2: low 8 bits in byte 1, bytes 2..3 zero). The bundled script reads all
  three and prints the code in hex; a v2 script still sees a non-zero byte 1
  for the common errors.
- `loop()` drains in 1 KiB reads.

## See Also

- [FirmwareCDC](../FirmwareCDC/) - the same job over a serial port
- [FirmwareDFU](../FirmwareDFU/) - the same job with `dfu-util`
- [VendorEcho](../VendorEcho/VendorEcho.ino) - minimal vendor bulk example
- [Firmware update over USB](../../docs/ota-over-usb.md)
- [Vendor API guide](../../docs/classes/vendor.md)
- [Sketch](FirmwareVendor.ino) / [All examples](../README.md) /
  [Example workflow](../../docs/example-workflow.md)
