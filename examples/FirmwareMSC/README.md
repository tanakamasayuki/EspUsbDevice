# EspUsbDevice FirmwareMSC

> 日本語版: [README.ja.md](README.ja.md)

Firmware update by **dragging a file onto a drive**. The board appears as a
small USB disk; copy a firmware `.bin` onto it and the device writes it into the
spare OTA partition, verifies it, and restarts into it. Nothing to install on
the host - the file manager is the update tool.

## Hardware

- An ESP32-S2, ESP32-S3 or ESP32-P4 board, connected through its **native USB OTG** port
- A PC as the USB host
- A separate Serial connection for logs

## Requirements

A partition scheme with **two application partitions** (OTA). The Arduino
"Default" scheme, which every profile in `sketch.yaml` uses, has them
(`app0` / `app1`, 1.25 MB each on a 4 MB flash). "Huge APP" does not, and
`disk.begin()` returns false when it finds only one.

Keep a recovery route available before testing: the ROM loader
([FirmwareBootMode](../FirmwareBootMode/), or BOOT + reset) can always rewrite
the whole flash.

## What It Does

- Presents a FAT12 volume whose data region *is* the OTA partition
- Shows a `README.TXT` explaining what to drop on it
- Detects an ESP application image by its `0xE9` magic byte and streams it into
  flash as it arrives; also accepts a `.uf2` file (any write order)
- Commits when the file's directory entry says the whole image has landed, or
  when the drive is ejected
- Verifies before switching the boot partition, then restarts
- Confirms the running image with `markValid()` once the host has configured
  the device

## Usage

1. Flash the sketch and open the Serial monitor at 115200.
2. Connect the native USB port to the PC. A drive labelled `ESPUSB` appears.
3. Copy a `.bin` onto it. Progress prints per kilobyte.
4. The board restarts into the new firmware. If nothing happens, eject the
   drive - that is the second commit point.

The `.bin` is a plain Arduino application build: Sketch > Export Compiled
Binary, or `<build-path>/<sketch>.ino.bin` from `arduino-cli compile`.
**Not** `*.merged.bin`, `*.bootloader.bin` or `*.partitions.bin`.

### Using a UF2 file

A raw `.bin` must be written in ascending order. A `.uf2` lifts that
restriction, is rejected if built for another chip, and knows its own length.
The sketch prints the family ID at startup; convert with Microsoft's
[uf2conv.py](https://github.com/microsoft/uf2/blob/master/utils/uf2conv.py):

```sh
python3 uf2conv.py --convert --family 0xc47e5767 --base 0x0 \
  --output firmware.uf2 build/FirmwareMSC.ino.bin   # 0xc47e5767 = ESP32-S3
```

Use the value printed as `UF2 family ID` for your chip. See
[firmware update guide, 3.6](../../docs/ota-over-usb.md#36-the-firmware-drive).

## Key APIs

- `EspUsbDeviceMscFirmwareDisk disk(storage, size)` - `storage` holds the
  volume metadata and the scratch area, not the image. 16 KiB is comfortable;
  8 KiB is the minimum.
- `disk.begin(label)` lays the volume out for the OTA partition this firmware
  would update, choosing a cluster size that keeps it inside FAT12.
- `disk.addTextFile(name, text)` puts a file on the drive before anything is
  copied to it (call between `begin()` and `attach()`).
- `disk.attach(msc)` wires it to `EspUsbDeviceMsc` and, unlike the RAM disks,
  marks the medium present and writable.
- `disk.onProgress()` / `onComplete()` / `onError()` / `onEject()` are the
  hooks; all run on the USB worker, so keep them short. `onComplete()`
  returning `false` refuses the image and puts the boot partition back.
- `disk.restartWhenComplete(false)` keeps the sketch running after a successful
  update instead of restarting into it.
- `disk.ramSectorCount()` is where the RAM-backed part of the volume ends and
  the partition begins - useful when sizing `storage`.
- `EspUsbDeviceMscFirmwareDisk::uf2FamilyId()` is the UF2 family this build
  accepts.
- `msc.configure()` with `transferBytes = 4096` (a multiple of 512, valid at
  FS and HS).
- `EspUsbDeviceFirmwareUpdate::targetLabel()` / `capacity()` / `markValid()`.

## Expected Serial Output

```text
Update target: app1 (1310720 bytes), volume <n> sectors, <m> of them in RAM
UF2 family ID for uf2conv.py: 0xc47e5767
Ready - a drive should appear on the host
1 KiB
...
301 KiB
Verified - restarting into the new firmware
```

(Sector counts depend on the partition and `storage` size.)

## Notes

- **The host must write a raw `.bin` in ascending order.** Every mainstream
  file manager does when copying onto an empty volume. A write that jumps
  backwards or leaves a gap is refused with `ESP_ERR_INVALID_STATE` and the
  update is abandoned rather than half-applied. A `.uf2` has no such limit.
- **Do not make the scratch area tight.** It absorbs
  `System Volume Information`, `.fseventsd` and `.Spotlight-V100`. A host that
  fills it starts allocating clusters inside the firmware region, where a write
  that is not the start of an ESP image is ignored.
- A file that is not an ESP application is silently dropped rather than
  flashed. One that is malformed fails verification at the end, and the boot
  partition is left alone.
- Reads of the firmware region come back from the partition itself, so a host
  that verifies what it copied sees what was actually written.
- **The image you copy replaces this sketch.** Send a build that also has the
  firmware drive (or another update route), or the next update has to go
  through boot mode.
- `markValid()` only matters with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`; it
  is not an authenticity check. Products need their own authorization and
  image-signing policy - anything that can write to the drive can reflash the
  board.
- Ordinary firmware replacement keeps NVS; do not assume a fresh application
  has fresh settings.
- Of the routes in the [firmware update guide](../../docs/ota-over-usb.md)
  this has the nicest UX and the most host-dependent behaviour.
  [FirmwareDFU](../FirmwareDFU/) is the one to reach for when the person doing
  the update has a terminal.
- Do not enable Arduino's built-in USB classes (USB MSC / DFU / CDC on boot)
  together with this library.

## Changes from v2

- `msc.configure()` sets the function name and a 4 KiB staging buffer
  (`transferBytes`, a multiple of 512 so it is valid at HS on ESP32-P4) instead
  of the build-time `CFG_TUD_MSC_EP_BUFSIZE`.
- The sketch prints the UF2 family ID and the README documents the `.uf2`
  route (the drive accepted UF2 in 2.x too, but the example did not show it).
- Startup failures also print `msc.startError()`.
- Behaviour, partition requirements and commit points are unchanged.

## See Also

- [FirmwareDFU](../FirmwareDFU/), [FirmwareHTTP](../FirmwareHTTP/),
  [FirmwareCDC](../FirmwareCDC/), [FirmwareVendor](../FirmwareVendor/) - other
  update routes
- [FirmwareBootMode](../FirmwareBootMode/) - the ROM loader recovery path
- [Firmware update over USB](../../docs/ota-over-usb.md)
- [MSC API guide](../../docs/classes/msc.md)
- [Sketch](FirmwareMSC.ino) / [All examples](../README.md) /
  [Example workflow](../../docs/example-workflow.md)
