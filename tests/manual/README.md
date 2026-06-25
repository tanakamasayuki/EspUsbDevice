# Manual Tests

> 日本語: [README.ja.md](README.ja.md)

Manual tests are reserved for behavior that cannot be fully controlled by
pytest, such as host OS enumeration dialogs, visual LED confirmation, external
USB analyzers, or physical cabling changes.

## `examples/MSCFatRamDisk`

Purpose:

- Verify that the host OS can mount the `EspUsbDeviceMscFatRamDisk` FAT12 RAM
  disk.
- Copy `CONFIG.TXT` from the host and verify that the device can read it after
  eject / unmount.

Steps:

1. Flash `examples/MSCFatRamDisk` to the USB device board.
2. Open Serial monitor and wait for `USB FAT RAM disk ready`.
3. Connect the USB device port to the PC.
4. Verify that the `ESPUSB` drive appears on the PC.
5. Copy `CONFIG.TXT` to the drive root.
6. Eject or unmount the drive from the OS.
7. Verify that Serial monitor prints `MSC_EJECT`, `CONFIG_SIZE`, and
   `CONFIG_BEGIN` / `CONFIG_END`.

Expected:

- Initial file `README.TXT` is visible on the host.
- `CONFIG.TXT` content is printed on Serial.
- The ESP32 side does not scan files before eject.

Notes:

- RAM disk contents are lost on reset or power cycle.
- If the host OS asks to format the drive, it may not accept this small FAT12
  image.
- Do not read the FAT image on the ESP32 side while the host may still be
  writing it.
- Large firmware images should use PSRAM, SD card, or streaming update instead
  of this small example.
