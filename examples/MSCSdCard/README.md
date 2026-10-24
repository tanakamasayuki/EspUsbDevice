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

## Walkthrough and checks

### Before running

Expose an SPI SD card to the USB host as block storage. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Check the SD module supply, SPI route and SD_CS_PIN before `sdMsc.begin()`. Initialize the card, attach it to msc, then start USB. While the PC owns the medium, do not also modify its filesystem from the sketch; use expendable media when learning and eject before removal.

### API use and adaptation

A block is addressed by LBA and sized by the medium; a file needs a filesystem on top. Compare capacity, mount success and file operations separately. Check returned byte counts, close handles, and allow outstanding host writes to finish before removing or reusing storage. Neither USB connection nor a FAT signature guarantees a valid filesystem.

Give the medium one writer at a time. A host-mounted volume and the sketch must not independently edit the same filesystem. RAM examples lose changes on reset; physical-media examples can change persistent data. Read the operation list before using a medium containing files you want to retain.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](MSCSdCard.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
