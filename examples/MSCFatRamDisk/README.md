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

- Formats a 96 KB RAM buffer as a FAT12 disk
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
