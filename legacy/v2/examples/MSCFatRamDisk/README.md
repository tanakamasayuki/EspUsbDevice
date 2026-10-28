# EspUsbDevice MSC FAT RAM Disk

> 日本語版: [README.ja.md](README.ja.md)

Creates a small FAT12 image in RAM and exposes it to the host as a USB Mass
Storage Class device.

The `MSC` example validates raw block I/O. This example shows the next layer:
the host can copy a file to the drive, eject it, and the ESP32 side can read the
file afterward.

## Hardware

- ESP32-S3 or another Arduino-ESP32 board with USB device support
- A USB host such as a PC
- A separate Serial monitor connection for logs when available

## What It Does

- Formats a 64 KB RAM buffer as a FAT12 disk, small enough for ESP32-S2
  internal DRAM
- Adds `README.TXT` as an initial file
- Exposes a writable MSC device so the host can copy `CONFIG.TXT`
- Reads and prints `CONFIG.TXT` on the ESP32 side after host eject / stop

## Usage

1. Flash the sketch and open Serial monitor.
2. Connect the USB device port to the PC.
3. When the `ESPUSB` drive appears, copy `CONFIG.TXT` to its root directory.
4. Eject or unmount the drive from the OS.
5. Serial monitor prints `CONFIG_SIZE` and the file content.

## Key APIs

- `EspUsbDeviceMscFatRamDisk disk(storage, sizeof(storage))` wraps a RAM buffer
  as a FAT disk.
- `disk.format("ESPUSB")` creates the minimal FAT12 image.
- `disk.addTextFile("README.TXT", text)` adds a file visible to the host.
- `disk.attach(msc)` installs MSC read/write callbacks.
- `disk.onEject(callback)` runs after host eject / stop.
- `disk.exists()`, `fileSize()`, and `readFile()` read files written by the host
  after eject.

## Limitations

- Fixed 512-byte sectors
- Small FAT12 RAM disk
- 8.3 filenames only; no long filename support
- Regular files in the root directory only
- The ESP32 side must not read the FAT image while the host may still be writing
- RAM contents are lost on power cycle
- The example keeps the buffer small enough for normal DRAM. For larger firmware
  files, use PSRAM, SD card, or a streaming update design.

For firmware update or Wi-Fi forwarding use cases, wait for host eject / unmount
before scanning and reading files on the ESP32 side.

## See Also

- [MSC](../MSC/) - minimal raw block I/O MSC example

## Walkthrough and checks

### Before running

Read back a host-written file after ejecting a FAT RAM disk. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

`format()`, `addTextFile()` and `attach()` prepare the disk before `begin()`. Copy CONFIG.TXT from the PC, flush/eject it, then follow the `onEject()` request into `loop()`'s `readFile()`. The example reads a bounded prefix; do not assume an arbitrarily large file fits its display buffer.

### API use and adaptation

A block is addressed by LBA and sized by the medium; a file needs a filesystem on top. Compare capacity, mount success and file operations separately. Check returned byte counts, close handles, and allow outstanding host writes to finish before removing or reusing storage. Neither USB connection nor a FAT signature guarantees a valid filesystem.

Give the medium one writer at a time. A host-mounted volume and the sketch must not independently edit the same filesystem. RAM examples lose changes on reset; physical-media examples can change persistent data. Read the operation list before using a medium containing files you want to retain.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](MSCFatRamDisk.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
