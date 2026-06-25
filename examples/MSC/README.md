# EspUsbDevice MSC

> 日本語版: [README.ja.md](README.ja.md)

Creates a USB Mass Storage Class device and exposes a small RAM-backed block
device to a USB host.

This example is for SCSI / READ(10) / WRITE(10) bring-up. `EspUsbDeviceMscRamDisk`
handles read/write callbacks and block range checks, so the minimal sketch does
not need to implement every MSC callback manually.

## Hardware

- ESP32-S3 or another Arduino-ESP32 board with USB device support
- A USB host such as a PC, another ESP32 running EspUsbHost, or a test fixture
- A separate Serial monitor connection for logs when available

## What It Does

- Starts `EspUsbDevice` as a full-speed MSC device
- Exposes a 16 block x 512 byte RAM disk
- Configures SCSI inquiry vendor / product / revision strings
- Handles `TEST UNIT READY`, `READ CAPACITY(10)`, `READ(10)`, and `WRITE(10)`
- Logs `START STOP UNIT` requests to Serial

## Important Limitation and Direction

The RAM disk in this example is not formatted as FAT. When connected to a PC,
the OS may ask to initialize or format it. This sketch validates MSC transport;
it is not a normal USB flash drive example for file-level reads and writes.

MSC separates the block device from the filesystem. To make host-mountable
storage, use one of these approaches:

- Put a valid FAT image in the RAM buffer.
- Back `onRead()` / `onWrite()` with block storage such as an SD card.

This library first provides a block device helper to reduce MSC setup overhead.
A FAT image helper is a possible next layer.

This project should avoid exposing internal flash, SPIFFS, or LittleFS directly
as USB MSC. USB MSC exposes sector-level block devices to the host, while
SPIFFS and LittleFS are filesystem APIs mounted by the ESP32. Internal flash also
has firmware partition, erase block, and write endurance concerns, so it is not a
good user-facing example target.

For practical persistent storage examples, SD card support should come first.
SD is already a block device and works naturally with FAT and USB MSC. The key
rule is exclusive ownership: while the USB host owns the SD card through MSC, the
ESP32 side should not mount or write the same filesystem. Hand ownership back to
the ESP32 side after eject or `START STOP UNIT`.

## Practical RAM Disk Uses

RAM disk is not only for tests. With a FAT image helper, it can become a small
temporary USB drive for host-to-device file handoff.

- Drop a firmware image from a PC, then apply a firmware update after eject.
- Drop a configuration file and read it on the ESP32 side.
- Receive a file from a PC and forward it over Wi-Fi.
- Place logs or diagnostic data in a RAM FAT image and expose it to the host.

To keep this safe, the ESP32 side should not parse the FAT image while the host
may still be writing it. File listing and reads should happen after host sync,
eject, or stop has been observed.

Planned helpers should be split like this:

- `EspUsbDeviceMscRamDisk`: raw block I/O, tests, and temporary buffers.
- `EspUsbDeviceMscFatRamDisk`: small FAT image in RAM for file handoff.
- `EspUsbDeviceMscSdCard`: expose an SD card as a USB MSC block device.
- Direct flash / SPIFFS / LittleFS exposure: not part of standard examples.

## Key APIs

- `EspUsbDeviceMsc msc(device)` registers the MSC function.
- `msc.vendorID()`, `productID()`, and `productRevision()` configure SCSI inquiry
  strings.
- `msc.mediaPresent(true)` reports inserted media to the host.
- `msc.isWritable(true)` allows host writes.
- `msc.onStartStop(callback)` receives eject / start / stop requests.
- `EspUsbDeviceMscRamDisk disk(storage, blocks, blockSize)` wraps an external
  buffer as a block device.
- `disk.attach(msc)` installs MSC read/write callbacks and calls
  `msc.begin(blocks, blockSize)`.
- `disk.clear()`, `readBlock()`, `writeBlock()`, and `writeByte()` are useful for
  small test initializers.

## Expected Serial Output

```text
USB MSC RAM disk ready blocks=16 block_size=512 bytes=8192
MSC_START_STOP pc=0 start=0 eject=1
```

The second line depends on the host or OS and may not appear.

## See Also

- [Keyboard](../Keyboard/) - HID keyboard device
- [Mouse](../Mouse/) - HID mouse device
- [KeyboardMouse](../KeyboardMouse/) - composite keyboard and mouse device
