# EspUsbDevice FirmwareDFU

> 日本語版: [README.ja.md](README.ja.md)

Updates the board's firmware over USB with `dfu-util`, while the sketch keeps
running and keeps being a USB keyboard. The ROM bootloader is not involved, and
no button is pressed.

DFU costs **one interface and no endpoints** — every transfer travels on EP0 —
so this function can be added to a device whose endpoint budget is already
spent. See the [firmware update guide](../../docs/ota-over-usb.md) for the other
routes.

## Hardware

- An ESP32-S2, ESP32-S3 or ESP32-P4 board with USB device support
- A PC with [`dfu-util`](https://dfu-util.sourceforge.net/) installed
- A separate Serial connection for logs

## Requirements

A partition scheme with **two application partitions**. The Arduino "Default"
scheme has them; "Huge APP" does not, and the sketch prints
`NO_OTA_PARTITION` at startup when it finds only one.

## What It Does

- Presents a HID keyboard and a DFU interface on one device
- Accepts a firmware image over DFU and writes it into the spare OTA partition
- Verifies the image, switches the boot partition, and restarts into it
- Prints progress per kilobyte and reports failures as DFU status codes

## Usage

```sh
dfu-util -l                                   # find the device
dfu-util -D build/FirmwareDFU.ino.bin         # write it
```

On Linux, `dfu-util` needs a udev rule for `303a:*` or `sudo`. On Windows the
DFU interface needs the WinUSB driver, which [Zadig](https://zadig.akeo.ie/)
installs.

The image is a plain Arduino `.bin` — Sketch → Export Compiled Binary, or the
`build/` directory `arduino-cli compile` leaves behind. It is **not** the
`dfu.bin` container `idf.py dfu` produces; that one is for the ROM's DFU, which
is a different thing ([guide, 2.6](../../docs/ota-over-usb.md#26-rom-dfu-instead-of-the-rom-serial-loader-s2s3)).

## Key APIs

- `EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Download, "Firmware")`
  registers the function. `EspUsbDeviceDfuMode::Runtime` instead makes it a
  device that only answers `dfu-util -e` by restarting into the ROM loader —
  see [`FirmwareBootMode`](../FirmwareBootMode/).
- `dfu.onProgress(cb)` reports bytes written so far. There is no total: DFU 1.1
  never tells the device how long the image is.
- `dfu.onComplete(cb)` runs after the image verified and the boot partition
  moved; returning `false` puts it back.
- `dfu.onError(cb)` receives the DFU status code the host is told.
- `dfu.restartWhenComplete(false)` keeps the sketch running after a successful
  download instead of restarting into it.
- `EspUsbDeviceFirmwareUpdate::available()` / `targetLabel()` / `capacity()`
  describe the partition an update would be written into.
- `EspUsbDeviceFirmwareUpdate::markValid()` confirms the running image, which
  cancels a pending bootloader rollback.

## Verified

An ESP32-P4 running this sketch, driven from a PC: the DFU interface enumerated
at high speed, the host read `wTransferSize=1024` / `bcdDFU=0x0110` /
`canDnload` / `manifestationTolerant=0` from the functional descriptor, and a
385 KB image transferred in 376 blocks in 2.7 s (139 KiB/s) over EP0 alone. The
device reported `dfuMANIFEST-WAIT-RESET`, switched the boot partition and
restarted into the new image.

## Notes

- **The image you send replaces this sketch.** Send a build that also has a DFU
  interface, or the next update has to go through boot mode.
- A file that is not an ESP application is rejected on its first block with DFU
  status 3 (`errWRITE`); one that is malformed fails verification at the end
  with status 7 (`errVERIFY`). Neither touches the boot partition, so a wrong
  file costs nothing.
- The block size is `CFG_TUD_DFU_XFER_BUFSIZE`, 1024 bytes by default, and is
  also what the functional descriptor declares. Raise it from `build_opt.h`
  (`-DCFG_TUD_DFU_XFER_BUFSIZE=4096`) for large images.
- `dfu-util` may print an error on its very last status read. The device
  restarts into the new image half a second after the download verifies, and a
  host that polls once more in that window sees the device go away. The update
  landed; check the board, not the message.
- Rollback is not automatic. A device that must survive a bad image needs
  `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` as well as the `markValid()` call this
  sketch makes.
- This library is not designed to run together with Arduino's built-in USB
  device classes.
