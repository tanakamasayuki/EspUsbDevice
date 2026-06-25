# Unit Tests

> 日本語: [README.ja.md](README.ja.md)

Unit tests cover host-independent logic:

- Device descriptor bytes.
- Configuration descriptor layout.
- FS/HS endpoint MPS selection.
- HID report descriptor bytes.
- HID keyboard/mouse report builders.
- MSC FAT RAM disk helper boot sector, FAT, root directory, and file-read
  helpers.

## `compile_smoke`

This is the first environment check. In `--run-mode=build`, it verifies Arduino
CLI, sketch.yaml, the ESP32 board package, library resolution, and minimal
public header compilation. It does not validate the USB device stack at runtime.

## `descriptor`

This verifies USB device, configuration, and HID report descriptor bytes. The
initial spec fixes HID keyboard and HID mouse interrupt endpoint MPS to 8 bytes
for both FS and HS. Keyboard + mouse composite uses one HID interface with
report IDs and 16-byte endpoint MPS so the report-ID-prefixed keyboard report
fits in one interrupt packet.

## `fat_ramdisk`

This verifies host-independent `EspUsbDeviceMscFatRamDisk` logic:

- FAT12 boot sector fields.
- Volume label, FAT type, and boot signature.
- 8.3 filename normalization.
- Root directory entries.
- FAT12 cluster chains.
- `exists()`, `fileSize()`, and `readFile()`.
- `EspUsbDeviceMsc` attach, read/write callbacks, and eject callback.
