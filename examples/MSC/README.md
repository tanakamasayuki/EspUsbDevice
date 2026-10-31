# EspUsbDevice MSC

> 日本語版: [README.ja.md](README.ja.md)

Creates a USB Mass Storage Class (MSC) device and exposes a small RAM-backed
block device to a USB host.

This example is for SCSI / READ(10) / WRITE(10) bring-up.
`EspUsbDeviceMscRamDisk` provides the read/write callbacks and the block range
checks, so the sketch does not implement every MSC callback by hand. For
callbacks written directly against `msc.onRead()` / `onWrite()`, see
[MscCdcBlocks](../MscCdcBlocks/MscCdcBlocks.ino).

## Hardware

- ESP32-S3, ESP32-S2 or ESP32-P4 board, connected to the host through its
  **native USB OTG** port
- A USB host such as a PC, another ESP32 running EspUsbHost, or a test fixture
- A separate Serial connection (USB-UART or the board's debug port) for logs

## What It Does

- Starts `EspUsbDevice` as an MSC device (full speed on S2/S3, high speed on P4)
- Exposes a 16 block x 512 byte RAM disk, zero-filled, with `55 AA` at the end
  of block 0
- Sets the SCSI INQUIRY vendor / product / revision strings
- Handles `INQUIRY`, `TEST UNIT READY`, `READ CAPACITY(10)`, `READ(10)`,
  `WRITE(10)` and the rest of the command set listed in the
  [MSC guide](../../docs/classes/msc.md)
- Logs `START STOP UNIT` requests (eject) to Serial

## Usage

1. Flash the sketch (`arduino-cli compile --profile esp32s3` etc.) and open the
   Serial monitor at 115200.
2. Connect the native USB port to the host. `USB MSC RAM disk ready` appears.
3. On a PC the 8 KiB disk shows up as an unformatted removable drive. Read it
   with a raw tool, e.g. on Linux
   `sudo dd if=/dev/sdX bs=512 count=1 | xxd | tail -2` (find `sdX` with
   `lsblk`); the last two bytes are `55 aa`.
4. Ejecting the drive (`eject /dev/sdX`, or "Eject" in the file manager) prints
   `MSC_START_STOP ...`.

## Important Limitation and Direction

The RAM disk is not formatted. When connected to a PC, the OS may offer to
initialize or format it; that is a property of the content, not a failed USB
connection. This sketch validates MSC transport, not file-level use. Everything
is lost on reset.

MSC separates the block device from the filesystem. To make host-mountable
storage, either:

- put a valid FAT image in RAM - [MSCFatRamDisk](../MSCFatRamDisk/), or
- back `onRead()` / `onWrite()` with block storage such as an SD card -
  [MSCSdCard](../MSCSdCard/).

Do not expose internal flash, SPIFFS or LittleFS directly as MSC. MSC hands the
host raw sectors, while SPIFFS and LittleFS are filesystems the ESP32 mounts;
internal flash also has firmware partitions, erase blocks and write endurance
to worry about. The one deliberate exception is
[FirmwareMSC](../FirmwareMSC/), whose data region is the OTA partition and
which only accepts firmware images.

The rule for every MSC medium is exclusive ownership: while the host owns it,
the sketch must not mount or write the same filesystem. Hand ownership back
after eject / `START STOP UNIT`.

## Practical RAM Disk Uses

A RAM disk is not only for tests. With the FAT helper it becomes a small
temporary drive for host-to-device file handoff:

- drop a configuration file and read it on the ESP32 side,
- receive a file from a PC and forward it over Wi-Fi,
- expose logs or diagnostic data to the host.

Parse the FAT image only after the host has synced, ejected or stopped the
drive, never while it may still be writing.

| Helper | Use |
|---|---|
| `EspUsbDeviceMscRamDisk` | raw block I/O, tests, temporary buffers (this example) |
| `EspUsbDeviceMscFatRamDisk` | small FAT image in RAM for file handoff |
| `EspUsbDeviceMscSdCard` | an SPI SD card as an MSC block device |
| `EspUsbDeviceMscFirmwareDisk` | drag-and-drop firmware update |

## Key APIs

- `EspUsbDeviceMsc msc(device)` registers the MSC function.
- `msc.configure(EspUsbMscConfig)` sets the function name and the DMA staging
  size (`transferBytes`, default 512). Call while stopped; it returns
  `esp_err_t`.
- `msc.vendorID()`, `productID()`, `productRevision()` set the INQUIRY strings
  (8 / 16 / 4 characters).
- `msc.mediaPresent(true)` reports the medium as inserted. The RAM disk helper
  does **not** do this for you.
- `msc.isWritable(true)` allows host writes; `false` makes the medium
  write-protected.
- `msc.onStartStop(callback)` receives eject / start / stop requests. It runs
  on the USB worker; return quickly.
- `EspUsbDeviceMscRamDisk disk(storage, blocks, blockSize)` wraps an external
  buffer (internal RAM or PSRAM) as a block device.
- `disk.attach(msc)` installs the read/write callbacks and calls
  `msc.begin(blocks, blockSize)`, which only sets the geometry. The staging
  buffer is allocated by `device.begin()`.
- `disk.clear()`, `readBlock()`, `writeBlock()`, `writeByte()` are handy for
  small test initializers.
- After a failed `device.begin()`, `device.lastErrorName()` and
  `msc.startError()` give the reason.

## Expected Serial Output

```text
USB MSC RAM disk ready blocks=16 block_size=512 bytes=8192 staging=512
MSC_START_STOP pc=0 start=0 eject=1
```

The second line depends on the host and may not appear.

## Checks

- Run the unchanged sketch first and compare its log with the steps above.
  Note which stage succeeds - enumeration, capacity, a raw read - instead of
  expecting a mounted filesystem.
- A block is addressed by LBA and sized by the medium; a file needs a
  filesystem on top. Neither a USB connection nor a boot signature means a
  valid filesystem.
- At high speed (ESP32-P4) `transferBytes` must be a multiple of 512; anything
  else makes `device.begin()` fail with `ESP_ERR_INVALID_SIZE`.
- The error logging here is a demonstration, not a retry or recovery policy.

## Changes from v2

- The staging buffer is set per function with `msc.configure()`
  (`transferBytes`), not with `CFG_TUD_MSC_EP_BUFSIZE`; the old macros are a
  compile error.
- `onStartStop()`, `vendorID()` and the other setters return `esp_err_t` and
  succeed only while stopped.
- Startup failures report `msc.startError()` as well as
  `device.lastErrorName()`; the ready line also prints the allocated staging
  size (`msc.transferCapacity()`).
- Re-inserting a medium (`mediaPresent(false)` then `true`) now reports a
  UNIT ATTENTION (06/28/00) to the host once.
- The sketch otherwise behaves as in v2 (same VID/PID 303a:4007, same disk).

## See Also

- [MSCFatRamDisk](../MSCFatRamDisk/) - file handoff through a RAM FAT image
- [MSCSdCard](../MSCSdCard/) - SD card as USB storage
- [MscCdcBlocks](../MscCdcBlocks/MscCdcBlocks.ino) - hand-written callbacks plus CDC
- [MSC API guide](../../docs/classes/msc.md)
- [Sketch](MSC.ino) / [All examples](../README.md) /
  [Example workflow](../../docs/example-workflow.md)
