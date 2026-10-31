# EspUsbDevice MscCdcBlocks

> 日本語版: [README.ja.md](README.ja.md)

A composite device with a raw MSC block device written by hand and a CDC port
that reports how often the host used it.

The MSC part does not use a helper class. The sketch keeps 16 blocks of 512
bytes in RAM and supplies its own `onRead()` / `onWrite()` callbacks, which is
the shape any custom storage backend takes. The CDC port prints how many times
each callback ran.

## Hardware

- ESP32-S2 / S3 / P4 board with a native USB OTG port
- A USB host that mounts USB storage and opens a serial port (a PC, or another
  ESP32 running EspUsbHost)
- A separate Serial monitor connection for start-up errors (optional)

## What It Does

- Keeps an 8 KiB array (`blockCount = 16`, `blockBytes = 512`), zero-filled at
  reset, with no filesystem
- Registers read and write callbacks that copy to and from the array after a
  range check, and count their calls in `std::atomic` counters
- Sets the SCSI INQUIRY strings (`ESPUSB` / `RAM blocks`), marks the medium
  present, sets `transferBytes = 512` and the geometry with
  `disk.begin(16, 512)`
- Configures the CDC port (interface name `disk statistics`, 512-byte receive
  ring, 256-byte send ring, `transferBytes = 64`)
- Starts `EspUsbDevice` with the product string `RAM blocks and console`
- While a terminal holds DTR on the CDC port, prints `reads=N writes=M` every
  second, and at once when `?` arrives
- Prints the error code of a failed step on the Serial monitor

## Checking From the Host

1. Flash the sketch and connect the USB device port.
2. The host sees an 8 KiB removable disk with no partition table or
   filesystem. The OS may offer to format it; that is the content, not a USB
   failure. Do not format it if you want to compare raw contents.
3. Open the CDC port (`picocom /dev/ttyACM0` on Linux). The counters appear.
   `reads` is usually above 0 already, because the OS reads the disk while it
   probes it.
4. On Linux, find the disk with `lsblk` (8K size) and read and write it raw.
   Double-check the device name: writing to the wrong disk destroys its data.

   ```sh
   sudo dd if=/dev/sdX bs=512 count=1 iflag=direct | xxd | head -2
   printf 'hello' | sudo dd of=/dev/sdX bs=512 seek=1 conv=sync oflag=direct
   sudo dd if=/dev/sdX bs=512 skip=1 count=1 iflag=direct | xxd | head -1
   ```

   The first read shows zeros. After the write, block 1 starts with `hello`,
   and `writes` in the CDC output has increased.
5. Reset the board: the contents are zero again.

Windows and macOS have not been checked with this example.

## Key APIs

- `EspUsbDeviceMsc disk(device)` registers an MSC (Bulk-Only Transport)
  function.
- `disk.onRead(cb)` / `disk.onWrite(cb)`: `int32_t(lba, offset, buffer, size)`.
  One call covers `size` bytes starting at byte `offset` of block `lba`.
  `size` is at most `transferBytes` and may cross a block boundary. Return
  exactly `size` on success; anything else fails the command (Medium Error).
- `disk.vendorID()` / `productID()` set the INQUIRY strings (8 and 16
  characters at most). `disk.mediaPresent(true)` reports a medium.
- `disk.configure(EspUsbMscConfig)`: `transferBytes` is the staging buffer
  size. It must be a multiple of 64; at `device.begin()` it must also be a
  multiple of the bulk packet size (64 at full speed, 512 at high speed). 512
  works on both.
- `disk.begin(blockCount, blockSize)` only sets the geometry. It fails when
  there is no read callback. USB starts with `device.begin()`.
- `disk.startError()` explains a failure of either `begin()`.
- `console.connected()` is "configured and DTR=1".

## Notes

- The counters count callback calls, not SCSI commands, USB packets or bytes.
- Callbacks run on the USB worker. Keep them short and do not call
  `device.end()` there. The counters are atomic because `loop()` reads them
  from another task.
- The host owns the medium while it is mounted. Do not change `storage` from
  the sketch at the same time.
- For a disk with files, use [MSCFatRamDisk](../MSCFatRamDisk/); for an SD card,
  [MSCSdCard](../MSCSdCard/).

## See Also

- [MSC](../MSC/) - the RAM disk helper with raw blocks
- [MSCFatRamDisk](../MSCFatRamDisk/) - FAT image in RAM
- [CompositeHidCdcMsc](../CompositeHidCdcMsc/) - HID, CDC and MSC in one device
- [CdcConsole](../CdcConsole/) - CDC through `Stream &`
- [MSC API](../../docs/classes/msc.md), [CDC API](../../docs/classes/cdc.md)

## Walkthrough and checks

### Before running

Follow the hardware requirements above and select the board and the example's
profile. [Example workflow](../../docs/example-workflow.md) explains
installation, connectors, USB roles and the difference between startup and a
usable peer.

### Follow the code

`inRange()` is the only safety check between the host and the array. It
rejects a block number past the end and any request that would run past the
last byte. Each `setup()` step stops at the first error and prints it.

### API use and adaptation

To back the disk with other storage, replace the two `memcpy()` calls and keep
the contract: finish the write before returning, and return exactly `size`.
Slow storage blocks the USB worker for the duration of the call.

### Verify one change at a time

Check the raw read and write first, then change the block count or the
storage backend, one at a time.

## Changes from v2

This example is new in v3. v2 has no corresponding example.

- [Sketch](MscCdcBlocks.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
