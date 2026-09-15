# EspUsbDevice FirmwareMSC

> 日本語版: [README.ja.md](README.ja.md)

Firmware update by **dragging a file onto a drive**. The board appears as a
small USB disk; copy a firmware `.bin` onto it and the device writes it into the
spare OTA partition, verifies it, and restarts into it. Nothing to install on
the host — the file manager is the update tool.

## Hardware

- An ESP32-S2, ESP32-S3 or ESP32-P4 board with USB device support
- A PC as the USB host
- A separate Serial connection for logs

## Requirements

A partition scheme with **two application partitions**. The Arduino "Default"
scheme has them; "Huge APP" does not, and `disk.begin()` returns false when it
finds only one.

## What It Does

- Presents a FAT12 volume whose data region *is* the OTA partition
- Shows a `README.TXT` explaining what to drop on it
- Detects an ESP application image by its `0xE9` magic byte and streams it into
  flash as it arrives
- Commits when the file's directory entry says the whole image has landed, or
  when the drive is ejected
- Verifies before switching the boot partition, then restarts

## Usage

1. Flash the sketch and open the Serial monitor.
2. Connect the USB device port to the PC. A drive appears.
3. Copy a `.bin` onto it. Progress prints per kilobyte.
4. The board restarts into the new firmware. If nothing happens, eject the
   drive — that is the second commit point.

The `.bin` is a plain Arduino build: Sketch → Export Compiled Binary, or the
`build/` directory `arduino-cli compile` leaves behind.

## Key APIs

- `EspUsbDeviceMscFirmwareDisk disk(storage, size)` — `storage` holds the
  volume metadata and the scratch area, not the image. 16 KB is comfortable;
  8 KB is the minimum.
- `disk.begin(label)` lays the volume out for the OTA partition this firmware
  would update, choosing a cluster size that keeps it inside FAT12.
- `disk.addTextFile(name, text)` puts a file on the drive before anything is
  copied to it.
- `disk.attach(msc)` wires it to `EspUsbDeviceMsc`.
- `disk.onProgress()` / `onComplete()` / `onError()` / `onEject()` are the
  hooks; all run on the usbd task.
- `disk.restartWhenComplete(false)` keeps the sketch running after a successful
  update instead of restarting into it.
- `disk.ramSectorCount()` is where the RAM-backed part of the volume ends and
  the partition begins — useful when sizing `storage`.

## Notes

- **The host must write the file in ascending order.** Every mainstream file
  manager does when copying onto an empty volume. A write that jumps backwards
  or leaves a gap is refused with `ESP_ERR_INVALID_STATE` and the update is
  abandoned rather than half-applied — nothing is ever left in a state where a
  broken image looks complete.
- **Do not make the scratch area tight.** It is what absorbs
  `System Volume Information`, `.fseventsd` and `.Spotlight-V100`. A host that
  fills it starts allocating clusters inside the firmware region, where a write
  that is not the start of an ESP image is ignored.
- A file that is not an ESP application is silently dropped rather than flashed.
  One that is malformed fails verification at the end, and the boot partition is
  left alone.
- Reads of the firmware region come back from the partition itself, so a host
  that verifies what it copied sees what was actually written.
- Of the update routes in [docs/ota-over-usb.md](../../docs/ota-over-usb.md)
  this has the nicest UX and the most host-dependent behaviour.
  [`FirmwareDFU`](../FirmwareDFU/) is the one to reach for when the person doing
  the update has a terminal.
- This library is not designed to run together with Arduino's built-in USB
  device classes.
