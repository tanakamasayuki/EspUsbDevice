# EspUsbDevice MSC FAT RAM Disk

> 日本語版: [README.ja.md](README.ja.md)

Creates a small FAT12 image in RAM and exposes it to the host as a USB Mass
Storage Class device.

The [MSC](../MSC/) example validates raw block I/O. This one shows the next
layer: the host copies a file onto the drive, ejects it, and the ESP32 reads
the file afterwards.

## Hardware

- ESP32-S3, ESP32-S2 or ESP32-P4 board, connected through its **native USB OTG** port
- A USB host such as a PC
- A separate Serial connection for logs

## What It Does

- Formats a 64 KiB RAM buffer as a FAT12 volume labelled `ESPUSB` - small
  enough for the ESP32-S2's internal DRAM
- Adds `README.TXT` as an initial file
- Exposes the volume as a writable MSC device so the host can copy `CONFIG.TXT`
- After the host ejects / stops the drive, reads `CONFIG.TXT` on the ESP32 side
  and prints its size and (up to 255 bytes of) content

## Usage

1. Flash the sketch and open the Serial monitor at 115200.
2. Connect the native USB port to the PC.
3. When the `ESPUSB` drive appears, copy a file named `CONFIG.TXT` into its root
   directory.
4. Eject or unmount the drive from the OS (file manager "Eject", or
   `eject /dev/sdX` / `udisksctl unmount -b /dev/sdX1` on Linux).
5. The Serial monitor prints `MSC_EJECT`, `CONFIG_SIZE` and the file content.

## Key APIs

- `EspUsbDeviceMscFatRamDisk disk(storage, sizeof(storage))` wraps a RAM buffer
  as a FAT disk.
- `disk.format("ESPUSB")` creates a FAT12 image (and erases whatever was in the
  buffer). It works for any buffer size: it picks the smallest cluster, from 1 to
  128 sectors, that keeps the volume below 4085 clusters (the FAT12 limit), and a
  volume of 65536 sectors or more records its size in `BPB_TotSec32`. It returns
  false only when the buffer is below 16 sectors or too large for FAT12 even with
  128-sector clusters.
- `disk.addTextFile("README.TXT", text)` / `addFile()` add files the host sees.
- `disk.attach(msc)` installs the read/write and `START STOP UNIT` callbacks
  and the geometry. It does not mark the medium present: call
  `msc.mediaPresent(true)` before `device.begin()`.
- `disk.onEject(callback)` runs after host eject / stop, on the USB worker;
  this sketch only sets a flag there.
- `disk.exists()`, `fileSize()`, `readFile()` read files written by the host,
  from `loop()`, after the eject. `readFile()` stops at a cluster chain that
  leaves the volume instead of following it.
- `msc.configure(EspUsbMscConfig)` sets the function name and the 512-byte
  staging buffer (valid at FS and HS).

## Limitations

- Fixed 512-byte sectors; always FAT12, with one root directory of 64 entries
- 8.3 filenames only; no long file names
- Regular files in the root directory only
- The ESP32 must not read the FAT image while the host may still be writing;
  wait for the eject
- RAM contents are lost on power cycle or reset
- The buffer is sized for internal DRAM. The image is only `memcpy`'d, never
  DMA'd, so a larger one may live in PSRAM; for persistent or much larger media
  use an SD card ([MSCSdCard](../MSCSdCard/)), and for firmware use
  [FirmwareMSC](../FirmwareMSC/), which streams to flash instead of holding the
  file

For firmware-update or Wi-Fi forwarding uses, scan and read files only after
the host has ejected or unmounted the drive.

## Expected Serial Output

```text
USB FAT RAM disk ready blocks=128 bytes=65536
MSC_EJECT
CONFIG_SIZE 12
CONFIG_BEGIN
hello=world
CONFIG_END
```

## Checks

- `format()`, `addTextFile()` and `attach()` prepare the disk before
  `device.begin()`. Copy the file, let the host flush, eject, then follow the
  `onEject()` flag into `loop()`.
- The display is a bounded prefix; do not assume an arbitrarily large file fits
  the buffer.
- Some hosts do not send `START STOP UNIT` on unmount alone; use the file
  manager's "Eject" (or `eject`) if `MSC_EJECT` does not appear.
- One writer at a time: the host while mounted, the sketch after eject.

## Changes from v2

- `msc.configure()` names the function and sets the staging buffer
  (`transferBytes`) instead of the build-time `CFG_TUD_MSC_EP_BUFSIZE`.
- The eject callback only sets a flag; the `MSC_EJECT` line is printed from
  `loop()` rather than from the USB worker.
- Startup failures also print `msc.startError()`.
- The README notes that the RAM image may be placed in PSRAM (it is never used
  for DMA in v3).
- Same VID/PID (303a:4008), volume and files as v2.

## See Also

- [MSC](../MSC/) - minimal raw block I/O example
- [MSCSdCard](../MSCSdCard/) - persistent storage on an SD card
- [MSC API guide](../../docs/classes/msc.md)
- [Sketch](MSCFatRamDisk.ino) / [All examples](../README.md) /
  [Example workflow](../../docs/example-workflow.md)
