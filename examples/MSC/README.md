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

## Important Limitation

The RAM disk in this example is not formatted as FAT. When connected to a PC,
the OS may ask to initialize or format it. This sketch validates MSC transport;
it is not a normal USB flash drive example for file-level reads and writes.

To make a host-mountable storage device, use one of these approaches:

- Put a valid FAT image in the RAM buffer.
- Back `onRead()` / `onWrite()` with SD, SPI flash, LittleFS, or another block
  storage implementation.

This library first provides a block device helper to reduce MSC setup overhead.
A FAT image helper is a possible next layer.

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
