# EspUsbDevice FirmwareDFU

> 日本語版: [README.ja.md](README.ja.md)

Updates the board's firmware over USB with `dfu-util`, while the sketch keeps
running and keeps being a USB keyboard. The ROM bootloader is not involved, and
no button is pressed.

DFU costs **one interface and no endpoints** - every transfer travels on EP0 -
so this function can be added to a device whose endpoint budget is already
spent. See the [firmware update guide](../../docs/ota-over-usb.md) for the other
routes.

## Hardware

- An ESP32-S2, ESP32-S3 or ESP32-P4 board, connected through its **native USB OTG** port
- A PC with [`dfu-util`](https://dfu-util.sourceforge.net/) installed
- A separate Serial connection for logs

## Requirements

A partition scheme with **two application partitions**. The Arduino "Default"
scheme, used by every profile in `sketch.yaml`, has them; "Huge APP" does not,
and the sketch prints `NO_OTA_PARTITION` at startup when it finds only one.

Keep a recovery route (the ROM loader - [FirmwareBootMode](../FirmwareBootMode/)
or BOOT + reset) available before testing.

## What It Does

- Presents a HID keyboard and a DFU interface on one device (the keyboard never
  types; it is there to show DFU sharing a device)
- Declares a 4096-byte DFU block (`wTransferSize`)
- Accepts a firmware image over DFU and writes it into the spare OTA partition
- Verifies the image, switches the boot partition, and restarts into it
- Prints progress per kilobyte and reports failures as DFU status codes
- Confirms the running image with `markValid()` once the host has configured
  the device

## Usage

```sh
dfu-util -l                                          # find the device (303a:4000)
dfu-util -d 303a:4000 -D build/FirmwareDFU.ino.bin   # write it
```

On Linux, `dfu-util` needs a udev rule for `303a:*` or `sudo`, for example
`/etc/udev/rules.d/99-espusb.rules`:

```text
SUBSYSTEM=="usb", ATTR{idVendor}=="303a", MODE="0666"
```

On Windows the DFU interface needs the WinUSB driver, which
[Zadig](https://zadig.akeo.ie/) installs.

The image is a plain Arduino `.bin` - Sketch > Export Compiled Binary, or
`<build-path>/<sketch>.ino.bin` from `arduino-cli compile`. It is **not** the
`dfu.bin` container `idf.py dfu` produces; that one is for the ROM's DFU, which
is a different thing
([guide, 2.6](../../docs/ota-over-usb.md#26-rom-dfu-instead-of-the-rom-serial-loader-s2s3)).
Nor is it `*.merged.bin` or `*.bootloader.bin`.

## Key APIs

- `EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Download, "Firmware")`
  registers the function. `EspUsbDeviceDfuMode::Runtime` instead makes it a
  device that only answers `dfu-util -e` by restarting into the ROM loader -
  see [FirmwareBootMode](../FirmwareBootMode/).
- `dfu.configure(EspUsbDfuConfig)` sets the block size (`transferBytes`, 64 to
  65535, default 1024), the poll times and `manifestationTolerant`. Call while
  stopped.
- `dfu.onProgress(cb)` reports bytes written so far. There is no total: DFU 1.1
  never tells the device how long the image is.
- `dfu.onComplete(cb)` runs after the image verified and the boot partition
  moved; returning `false` puts it back.
- `dfu.onError(cb)` receives the `EspUsbDfuStatus` the host is told
  (`Write` = 3, `Verify` = 7, ...).
- `dfu.restartWhenComplete(false)` keeps the sketch running after a successful
  download instead of restarting into it (about 500 ms after it verifies).
- `dfu.setBackend()` replaces the built-in OTA writer with your own storage
  (external flash, another processor) - see the
  [DFU guide](../../docs/classes/dfu.md).
- `EspUsbDeviceFirmwareUpdate::available()` / `targetLabel()` / `capacity()`
  describe the partition an update would be written into.
- `EspUsbDeviceFirmwareUpdate::markValid()` confirms the running image, which
  cancels a pending bootloader rollback.

## Expected Serial Output

```text
Update target: app1 (1310720 bytes)
Ready (wTransferSize=4096). dfu-util -D <firmware.bin>
DFU 4 KiB
...
DFU 300 KiB
DFU complete - restarting into the new firmware
```

## Verified

In 2.x, an ESP32-P4 running this sketch was driven from a PC: the DFU interface
enumerated at high speed, the host read `wTransferSize=1024` /
`bcdDFU=0x0110` / `canDnload` / `manifestationTolerant=0` from the functional
descriptor, and a 385 KB image transferred in 376 blocks in 2.7 s (139 KiB/s)
over EP0 alone. The device reported `dfuMANIFEST-WAIT-RESET`, switched the boot
partition and restarted into the new image.

In v3 the DFU driver is the library's own; its state machine, block sizes up to
65535 and HS operation were tested with a RAM backend. This example itself is
compile-checked only; a full `dfu-util` OTA run with the 4096-byte block has
not been recorded yet (see [DFU guide](../../docs/classes/dfu.md)).

## Notes

- **The image you send replaces this sketch.** Send a build that also has a DFU
  interface, or the next update has to go through boot mode.
- A file that is not an ESP application is rejected on its first block with DFU
  status 3 (`errWRITE`); one that is malformed fails verification at the end
  with status 7 (`errVERIFY`). Neither touches the boot partition, so a wrong
  file costs nothing.
- `dfu-util` may print an error on its very last status read. The device
  restarts into the new image about half a second after the download verifies,
  and a host that polls once more in that window sees the device go away. The
  update landed; check the board, not the message.
- Rollback is not automatic. A device that must survive a bad image needs
  `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` as well as the `markValid()` call this
  sketch makes. `markValid()` is not an authenticity check: anything that can
  reach the DFU interface can reflash the board.
- Ordinary firmware replacement keeps NVS.
- Do not enable Arduino's built-in USB classes (USB MSC / DFU / CDC on boot)
  together with this library.

## Changes from v2

- The block size is set at run time with `dfu.configure()`
  (`EspUsbDfuConfig::transferBytes`), not with `-DCFG_TUD_DFU_XFER_BUFSIZE` in
  `build_opt.h`; the example now uses 4096 (one flash sector, a quarter of the
  requests of the 1024 default).
- `onError()` receives `EspUsbDfuStatus` instead of `uint8_t`; print it with
  `unsigned(status)` to get the same number as before.
- `onProgress()` / `onComplete()` / `onError()` / `restartWhenComplete()`
  return `esp_err_t` and succeed only while stopped.
- Startup failures also print `dfu.startError()`; the ready line prints the
  declared `wTransferSize`.

## See Also

- [FirmwareBootMode](../FirmwareBootMode/) - DFU Runtime / ROM loader
- [FirmwareMSC](../FirmwareMSC/), [FirmwareHTTP](../FirmwareHTTP/),
  [FirmwareCDC](../FirmwareCDC/), [FirmwareVendor](../FirmwareVendor/) - other
  update routes
- [Firmware update over USB](../../docs/ota-over-usb.md)
- [DFU API guide](../../docs/classes/dfu.md)
- [Sketch](FirmwareDFU.ino) / [All examples](../README.md) /
  [Example workflow](../../docs/example-workflow.md)
