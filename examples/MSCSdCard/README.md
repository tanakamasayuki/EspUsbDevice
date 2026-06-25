# EspUsbDevice MSC SD Card

> 日本語版: [README.ja.md](README.ja.md)

Exposes an SPI-connected SD card as a USB Mass Storage Class device.

SD is already a 512-byte-sector block device, so it is a more practical
persistent-storage MSC example than RAM disk. This example uses Arduino-ESP32
`SD.readRAW()` / `SD.writeRAW()` and connects host `READ(10)` / `WRITE(10)`
requests to SD sector I/O.

## Hardware

- ESP32-S3 or another Arduino-ESP32 board with USB device support
- SPI-connected SD card socket
- A USB host such as a PC
- A separate Serial monitor connection for logs when available

## Usage

1. Change `SD_CS_PIN` for your board.
2. Insert an SD card.
3. Flash the sketch and open Serial monitor.
4. Wait for `USB SD MSC ready`.
5. Connect the USB device port to the PC.
6. Read/write the SD card from the PC.
7. Eject or unmount from the OS before disconnecting.

## Important Notes

- Do not use ESP32-side file APIs such as `SD.open()` while the host owns the SD
  card through MSC.
- This example calls `SD.begin()`, which also mounts the Arduino-side
  filesystem, but the sketch intentionally avoids file APIs during MSC use.
- Read the SD filesystem on the ESP32 side only after host eject / unmount.
- Concurrent writes from the host and ESP32 side can corrupt the FAT.
- Host writes directly modify the SD card. Back it up first if needed.

## Key APIs

- `EspUsbDeviceMscSdCard sdMsc(SD)` wraps the Arduino `SD` instance as an MSC
  block device.
- `sdMsc.begin(cs, SPI, frequency)` initializes SD and reads sector count / size.
- `sdMsc.attach(msc)` installs MSC read/write callbacks.
- `sdMsc.onEject(callback)` runs after host eject / stop.
- `sdMsc.readOnly(true)` rejects host writes.

## See Also

- [MSC](../MSC/) - minimal raw block I/O MSC example
- [MSCFatRamDisk](../MSCFatRamDisk/) - file handoff through a RAM FAT image
