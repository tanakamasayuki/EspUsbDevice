# EspUsbDevice MSC SD Card

> 日本語版: [README.ja.md](README.ja.md)

Exposes an SPI-connected SD card as a USB Mass Storage Class device.

An SD card is already a 512-byte-sector block device, so it is the practical
persistent-storage MSC example. The sketch uses Arduino-ESP32's
`SD.readRAW()` / `SD.writeRAW()` (through `EspUsbDeviceMscSdCard`) and connects
host `READ(10)` / `WRITE(10)` requests to SD sector I/O.

## Hardware

- ESP32-S3, ESP32-S2 or ESP32-P4 board, connected through its **native USB OTG** port
- An SPI SD card socket (module or on-board) and a card you can afford to lose
- A USB host such as a PC
- A separate Serial connection for logs

## Wiring

The sketch uses the board variant's default SPI pins. Change `SD_SCK_PIN`,
`SD_MISO_PIN`, `SD_MOSI_PIN` and `SD_CS_PIN` at the top of the sketch to match
your board; the pins in use are printed at startup.

| SD socket | ESP32 | ESP32-S3 default | ESP32-S2 default | ESP32-P4 default |
|---|---|---|---|---|
| CS (DAT3) | `SD_CS_PIN` (`SS`) | GPIO10 | GPIO34 | GPIO26 |
| SCK (CLK) | `SD_SCK_PIN` (`SCK`) | GPIO12 | GPIO36 | GPIO36 |
| MOSI (CMD / DI) | `SD_MOSI_PIN` (`MOSI`) | GPIO11 | GPIO35 | GPIO32 |
| MISO (DAT0 / DO) | `SD_MISO_PIN` (`MISO`) | GPIO13 | GPIO37 | GPIO33 |
| VCC | 3.3 V | | | |
| GND | GND | | | |

- Power the card from 3.3 V. A 5 V module must have its own regulator and
  level shifting.
- A bare socket wants pull-ups (10 kOhm to 3.3 V) on CS, MOSI and MISO; most
  modules already have them.
- Keep the wires short. `SD_SPI_HZ` is 4 MHz; lower it if `SD_BEGIN_FAILED`
  appears with long wires, raise it (e.g. 20 MHz) once the link is reliable.
- Boards that wire their slot for SDMMC (4-bit, e.g. many ESP32-P4 boards) need
  the slot in SPI mode or a separate SPI socket; this helper drives `SD`, not
  `SD_MMC`.

## Usage

1. Wire the socket and adjust the pin constants if needed.
2. Insert an SD card formatted as FAT16/FAT32. `SD.begin()` mounts it, so a
   card it cannot mount stops the sketch at `SD_BEGIN_FAILED`.
3. Flash the sketch and open the Serial monitor at 115200.
4. Wait for `USB SD MSC ready`.
5. Connect the native USB port to the PC.
6. Read and write the SD card from the PC like any USB card reader.
7. Eject or unmount from the OS before disconnecting. The sketch prints
   `SD_EJECT` and reports the medium as removed.
8. Send `r` on the Serial monitor to give the card back to the host
   (`SD_REINSERTED`); the host sees a medium change and mounts it again.

## Important Notes

- Do not use ESP32-side file APIs such as `SD.open()` while the host owns the
  card through MSC.
- `sdMsc.begin()` calls `SD.begin()`, which also mounts the Arduino-side
  filesystem; the sketch deliberately does not use file APIs during MSC use.
- Read the SD filesystem on the ESP32 side only after host eject / unmount, and
  re-mount it first (`SD.end()` then `sdMsc.begin(...)`) so that no cached
  FAT state from before the host's writes is used.
- Concurrent writes from the host and the ESP32 side corrupt the FAT.
- Host writes modify the card directly. Back it up first if needed.

## Key APIs

- `EspUsbDeviceMscSdCard sdMsc(SD)` wraps the Arduino `SD` instance as an MSC
  block device.
- `sdMsc.begin(cs, SPI, frequency, mountpoint, maxFiles)` initializes the card
  and reads the sector count and size (512-byte sectors only).
- `sdMsc.attach(msc)` installs the read/write and `START STOP UNIT` callbacks
  and the geometry, and returns false if any of them was refused (for example
  while the device runs). Then call `msc.mediaPresent(true)`.
- `sdMsc.onEject(callback)` runs after host eject / stop, on the USB worker.
- `sdMsc.readOnly(true)` reports the card to the host as write-protected: MODE
  SENSE returns the WP bit, so the host mounts it read-only, and a write that
  still arrives fails with DATA PROTECT (sense 07/27/00). It sets
  `msc.isWritable(false)` itself, at `attach()` or immediately when already
  attached; no separate call is needed.
- `msc.mediaPresent(false)` / `true` remove and re-insert the medium while USB
  stays up. Re-inserting reports a UNIT ATTENTION (medium changed) once.
- `msc.configure(EspUsbMscConfig)` with `transferBytes = 4096`: a larger DMA
  staging buffer means fewer USB round trips per callback burst. Any multiple
  of 512 works at both FS and HS.

## Expected Serial Output

```text
SD pins SCK=12 MISO=13 MOSI=11 CS=10
USB SD MSC ready blocks=31116288 block_size=512
SD_EJECT - send 'r' on Serial to give the card back to the host
SD_REINSERTED
```

## Checks

- Check the module's supply, the SPI route and the CS pin before
  `sdMsc.begin()`. `SD_BEGIN_FAILED` means the card never answered; USB has not
  started at that point.
- Compare capacity (`blocks=`), mount success on the PC and file operations
  separately.
- Use expendable media while learning, and eject before removing the cable or
  the card.

## Changes from v2

- The SPI pins are explicit constants and printed at startup; the README has a
  wiring table per chip. (v2 used the default bus and only `SD_CS_PIN`.)
- `msc.configure()` sets a 4 KiB staging buffer (valid at FS and HS) instead
  of the build-time `CFG_TUD_MSC_EP_BUFSIZE`.
- After eject the sketch reports the medium as removed
  (`msc.mediaPresent(false)`) and can hand it back with `r`; v2 only logged
  `SD_EJECT`.
- The eject callback only sets a flag; logging happens in `loop()`.
- Startup failures also print `msc.startError()`.
- `sdMsc.readOnly(true)` now tells the host the card is write-protected. v2 only
  failed each write, so the host saw a writable card whose writes errored.
- `sdMsc.attach()` now returns false when a callback could not be installed.
- Same VID/PID (303a:4009).

## See Also

- [MSC](../MSC/) - minimal raw block I/O example
- [MSCFatRamDisk](../MSCFatRamDisk/) - file handoff through a RAM FAT image
- [MSC API guide](../../docs/classes/msc.md)
- [Sketch](MSCSdCard.ino) / [All examples](../README.md) /
  [Example workflow](../../docs/example-workflow.md)
