# MSC (Mass Storage)

[日本語](msc.ja.md)

## Overview and when to use it

`EspUsbDeviceMsc` is a USB Mass Storage Class Function. The Host sees a drive,
like a USB stick. The library handles BOT (Bulk-Only Transport) and the SCSI
commands; reading and writing blocks is left to your callbacks.

When to use it:

- Exchanging configuration files or logs with the Host without a driver.
- Presenting an SD card as a USB card reader.
- Updating firmware by copying a file (`EspUsbDeviceMscFirmwareDisk`, [OTA over USB](../ota-over-usb.md)).

Ready-made helper classes:

| Class | Purpose |
| --- | --- |
| `EspUsbDeviceMscRamDisk` | Raw blocks in RAM (no filesystem) |
| `EspUsbDeviceMscFatRamDisk` | A FAT12 disk in RAM. Files can be placed on it before it is exposed |
| `EspUsbDeviceMscSdCard` | An SD card (when the Arduino SD library is available) |
| `EspUsbDeviceMscFirmwareDisk` | A disk that updates the firmware when a file is written to it |

One Function exposes LUN 0 only. When several MSC Functions are registered,
each has its own capacity, callbacks, buffer and bulk endpoint pair.

Operation has been checked over a full-speed link between two ESP32-S3 boards,
over a full-speed loopback and a high-speed link on ESP32-P4 (all with an ESP32
such as EspUsbHost as the Host), and over a high-speed link between a P4X and a
Linux PC (with usb-storage detached, checking BOT and SCSI directly with pyusb).
An OS mounting it as a disk has not been checked.

## Basic usage

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceMsc msc(device);
uint8_t storage[32 * 512];                   // 512 bytes × 32 blocks
EspUsbDeviceMscRamDisk volume(storage, 32);  // block size defaults to 512

void setup() {
  EspUsbMscConfig config;                    // the default transferBytes of 512 works at FS and HS
  config.name = "RAM Disk";
  if (msc.configure(config) != ESP_OK) return;
  if (!volume.attach(msc)) return;           // sets the read/write callbacks and the capacity
  msc.mediaPresent(true);                    // medium "inserted" (the default is "absent")
  device.begin();
}

void loop() {}
```

Writing the callbacks yourself:

```cpp
msc.onRead([](uint32_t lba, uint32_t offset, void *buffer, uint32_t size) -> int32_t {
  memcpy(buffer, storage + lba * 512 + offset, size);
  return size;                               // returning the requested size means success
});
msc.onWrite([](uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size) -> int32_t {
  memcpy(storage + lba * 512 + offset, buffer, size);
  return size;
});
msc.begin(32, 512);                          // capacity (block count, block size). Call after onRead
msc.mediaPresent(true);
device.begin();
```

`msc.begin(blockCount, blockSize)` only sets the capacity; `device.begin()`
allocates the transfer region. It returns false if no read callback is
registered, so call it after `onRead()`.

## Settings

Every field of `EspUsbMscConfig`:

| Item | Default | Range / meaning | FS/HS difference |
| --- | --- | --- | --- |
| `transferBytes` | 512 | Size in bytes of the DMA transfer region (staging). A multiple of 64, 64-65472 | A configuration that may run at HS needs a multiple of 512 (512-65024); otherwise begin fails with `ESP_ERR_INVALID_SIZE` |
| `transferBuffer` | `nullptr` | Your own DMA region. `nullptr` allocates at begin | none |
| `name` | `nullptr` | Function name (UTF-8). Copied at begin and published as the interface name | none |

Set while stopped:

| Function | Default | Meaning |
| --- | --- | --- |
| `vendorID(s)` / `productID(s)` / `productRevision(s)` | "ESP32" / "MSC" / "1.0" | Strings returned by INQUIRY (up to 8 / 16 / 4 characters; longer is cut, shorter is padded with spaces) |
| `begin(blockCount, blockSize)` | not set | Capacity. The helpers' `attach()` calls it |
| `onRead` / `onWrite` / `onStartStop` | none | Storage callbacks |

Can also change while running:

| Function | Default | Meaning |
| --- | --- | --- |
| `mediaPresent(bool)` | false (no medium) | Whether a medium is present ([Removing and changing the medium](#removing-and-changing-the-medium)) |
| `isWritable(bool)` | true | Whether writes are allowed. With false the Host sees the medium as write-protected |

- `configure()`, callback registration, `vendorID` and the like return `ESP_OK` only while stopped. While running they return `ESP_ERR_INVALID_STATE` and keep the previous settings.
- To change settings, call `device.end()` → settings → `device.begin()`.

`configure()` returns:

| Return value | Condition |
| --- | --- |
| `ESP_OK` | Accepted |
| `ESP_ERR_INVALID_STATE` | Running |
| `ESP_ERR_INVALID_SIZE` | `transferBytes` is 0, above 65535, or not a multiple of 64 |
| `ESP_ERR_INVALID_ARG` | `transferBuffer` is not 64-byte aligned, not in internal RAM, or not DMA-capable |

After `device.begin()`, `msc.startError()` tells why MSC failed to start.

| `startError()` | Condition |
| --- | --- |
| `ESP_ERR_INVALID_ARG` | No capacity set, or no read callback |
| `ESP_ERR_INVALID_SIZE` | `transferBytes` is not a multiple of 512 in a configuration that may run at HS |
| `ESP_ERR_NO_MEM` | The transfer region or driver registration could not be allocated |
| `ESP_ERR_INVALID_STATE` | Not registered, or already started |

## API

### Callbacks

```cpp
int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t size);
int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size);
bool onStartStop(uint8_t powerCondition, bool start, bool loadEject);
```

- read/write work synchronously and complete by returning exactly `size`. A negative value, 0, or anything other than `size` is an I/O failure (Medium Error).
- One callback covers `size` bytes starting at byte `offset` of block `lba`. `size` is at most `transferBytes` and may cross block boundaries.
- Without a write callback the medium is treated as read-only.
- A write callback must finish writing before it returns. SYNCHRONIZE CACHE simply reports success on that assumption.
- `onStartStop` is called for START STOP UNIT. A Host "eject" is usually `start = false`, `loadEject = true`. Returning false fails the command (05/24/00).
- Callbacks run on the USB worker, without the library's lock held. Do not call `device.end()`, change settings or destroy the Function there.

### State queries

| Function | Description |
| --- | --- |
| `mounted()` | The Host has selected a configuration and MSC is running |
| `transferCapacity()` | The actual transfer region in bytes. 0 before start and after end |
| `configuration()` | The requested settings |
| `testUnitReady()` | Whether a medium is present |
| `writable()` | `isWritable(true)` and a write callback is registered |

### Removing and changing the medium

`mediaPresent()` represents inserting and removing the medium.

```cpp
msc.mediaPresent(false);  // removed
// prepare the medium
msc.mediaPresent(true);   // inserted
```

| Operation | What the Host sees |
| --- | --- |
| true → false (removal) | Later commands fail with Not Ready / Medium Not Present (sense 02/3A/00). The rest of a running READ/WRITE also fails with 02/3A/00, and no more callbacks are made for it |
| false → true (insertion while the Host has the device configured) | The next command (other than INQUIRY and REQUEST SENSE) fails once with UNIT ATTENTION (06/28/00). That is how the Host learns of the change |
| Setting the same value again | No change |
| Changes before start or while unconfigured; true at first enumeration | Not reported |

- However many times it is re-inserted before the report, only one report is made.
- USB reset, deconfiguration and end clear an unreported insertion and the sense. BOT reset recovery keeps both.
- `mediaPresent()` does not stop I/O synchronously. A callback already running is not interrupted; it is allowed to return.
- A command that began before the removal does not resume, even after a quick re-insertion. Data from an old command is never written to a new medium. Writes that already succeeded are not rolled back.
- Calling `mediaPresent(false)` does not make it safe to discard or swap the storage. Keep the callbacks and what they reference valid until `device.end()` succeeds. Changes that alter the capacity or block size go through end → reconfigure → begin.
- A medium that can be physically pulled (an SD card, for instance) must be able to report an access failure from the callback.

### SCSI commands and sense

Supported SCSI commands: INQUIRY, REQUEST SENSE, TEST UNIT READY, READ
CAPACITY(10), READ FORMAT CAPACITIES, MODE SENSE(6/10) (header only, carrying
the write-protect bit), START STOP UNIT, PREVENT/ALLOW MEDIUM REMOVAL,
SYNCHRONIZE CACHE, VERIFY(10) (no comparison), READ(10), WRITE(10).

| Situation | Sense (key/ASC/ASCQ) |
| --- | --- |
| No medium | 02/3A/00 Medium Not Present |
| Reporting a medium change | 06/28/00 Not Ready to Ready Change |
| Read callback failed | 03/11/00 Unrecovered Read Error |
| Write callback failed | 03/0C/00 Write Error |
| Write to a read-only medium | 07/27/00 Write Protected |
| LBA out of range | 05/21/00 |
| Unknown command | 05/20/00 |
| Invalid CDB field, or `onStartStop` returned false | 05/24/00 |
| LUN that does not exist | 05/25/00 |

- INQUIRY answers as a removable (RMB = 1) direct-access device.
- PREVENT/ALLOW MEDIUM REMOVAL only reports success; it does not stop `mediaPresent(false)`.
- The block count per READ(10)/WRITE(10) is limited by the 16-bit CDB field, and the capacity by the 32-bit READ CAPACITY(10) field (under 2 TiB with 512-byte blocks).

## BOT handling

- The thirteen BOT cases (mismatches in direction and length between Host and Device) are handled, and the CSW residue reports what was actually processed.
- An invalid CBW halts both endpoints and asks the Host for reset recovery (Bulk-Only Mass Storage Reset plus CLEAR_FEATURE on both endpoints) (BOT 1.0 §5.3, §6.6.1).
- A class reset stops transfers, and no new CBW is accepted until CLEAR_FEATURE has been done on both endpoints.
- SET_INTERFACE (alt 0) discards any unfinished exchange at whatever stage, returns to DATA0 and waits for a new CBW. The sense and the medium settings are kept.
- The block size is independent of `transferBytes`. Block sizes that are not a multiple of 512 (511 bytes, for example) work. Extra bytes the Host sends in the last packet are never written to the next block.

## FS and HS differences

| Item | FS (S3, P4 full-speed controller) | HS (P4/P4X high-speed controller) |
| --- | --- | --- |
| Bulk MPS | 64 bytes | 512 bytes |
| `transferBytes` requirement | multiple of 64 (64-65472) | multiple of 512 (512-65024) |
| Default `transferBytes` (512) | works | works |

- On P4, the HS requirement applies whenever `EspUsbDeviceConfig::controller` is anything other than `FullSpeed` (including the default `Auto`). A sketch meant to run on both should use a multiple of 512.
- SCSI and BOT behave the same at FS and HS.
- One MSC Function uses one non-control IN endpoint.

## Buffers and memory

| Region | PSRAM | Notes |
| --- | --- | --- |
| Storage (the `storage` given to `EspUsbDeviceMscRamDisk`, or what your callbacks reference) | allowed | The callbacks only memcpy; DMA is not involved |
| `transferBuffer` (DMA staging) | not allowed | DMA-capable internal RAM, 64-byte aligned. Otherwise `configure()` returns `ESP_ERR_INVALID_ARG` |

- One BOT command never runs IN and OUT at the same time, so a single DMA region is used in turn for the CBW, the data and the CSW.
- A smaller `transferBytes` saves memory but means more callback calls.
- Giving a `transferBuffer` skips the internal allocation. Keep it valid from begin to end; the library does not free it.
- RAM disk storage in PSRAM has been checked over the P4 full-speed loopback and the high-speed link.
- A program that does not use MSC does not link its implementation.

## Helper classes

### EspUsbDeviceMscRamDisk

`EspUsbDeviceMscRamDisk(storage, blockCount, blockSize = 512)`. Raw blocks in RAM.

- `attach(msc)` sets the read/write callbacks and the capacity. It leaves the medium-present and writable settings alone, so call `msc.mediaPresent(true)` before `device.begin()`.
- Every helper's `attach()` checks that the callbacks were registered. While the device runs, registration is refused and `attach()` returns false. Attach before `device.begin()` or after `device.end()`.
- `readBlock()` / `writeBlock()` / `clear()` let the application read and write the content too. Consistency while the Host is using it is not guaranteed.
- There is no filesystem. The Host treats it as unformatted.

### EspUsbDeviceMscFatRamDisk

`EspUsbDeviceMscFatRamDisk(storage, size)`. Builds a FAT12 disk in RAM.

```cpp
uint8_t disk[64 * 1024];
EspUsbDeviceMscFatRamDisk fat(disk, sizeof(disk));
fat.format("ESPUSB");
fat.addTextFile("README.TXT", "hello\n");
fat.attach(msc);
msc.mediaPresent(true);
```

- `format(label)` creates FAT12 with 512-byte sectors and 64 root entries. Below 16 sectors it returns false. The label goes into the boot sector and, as the FAT specification requires, into a volume-label entry in the root directory (which takes one of the 64 entries); an empty or null label is written as `NO NAME` with no entry.
- The volume is FAT12 whatever its size. FAT12 holds fewer than 4085 clusters, so `format()` picks the smallest cluster (1, 2, 4 ... 128 sectors) that keeps the count below that. A large RAM disk, for example in PSRAM, is still a valid FAT12 volume. If even 128 sectors per cluster is not enough, `format()` returns false.
- A volume of 65536 sectors or more records its size in BPB_TotSec32 (BPB_TotSec16 is 0).
- File names are 8.3 (letters, digits, `_` `-` `~`, converted to upper case), in the root directory only. `addFile` / `addTextFile` write to contiguous clusters.
- `exists()` / `fileSize()` / `readFile()` look up the 8.3 name in the root directory. `readFile()` follows the cluster chain only inside the volume, so a FAT the Host wrote badly cannot make it read outside the storage.
- `onEject(cb)` is called when the Host sends a stop (`start = false`) with START STOP UNIT.
- `attach()` does not call `mediaPresent(true)`. Call it yourself.

### EspUsbDeviceMscSdCard

Available only when the Arduino SD library is present.

```cpp
EspUsbDeviceMscSdCard card;            // SD by default
if (card.begin(SS) && card.attach(msc)) msc.mediaPresent(true);
```

- `begin(ssPin, spi, frequency, mountpoint, maxFiles)` opens an SPI SD card. It fails unless the sector size is 512 bytes.
- `attach(msc)` sets the read/write/start-stop callbacks and the capacity. Call `mediaPresent(true)` yourself.
- `readOnly(true)` is reported to the Host as write protection: MODE SENSE returns the WP bit, and a write fails with DATA PROTECT, 07/27/00 Write Protected. The setting can be changed before or after `attach()`.
- `attach()` fails while the device runs.
- If the ESP32-side filesystem and the Host write the same SD card at once, one of them corrupts the other. Do not write from the ESP32 while the card is exposed to the Host.

### EspUsbDeviceMscFirmwareDisk

A disk that updates the firmware when a file is written to it. `attach()` sets
the medium present and writable. See [OTA over USB](../ota-over-usb.md).

## Host OS side

MSC works with each OS's standard driver. The table describes how each OS
generally handles it. An OS mounting this version of the library as a disk has
not been checked.

| OS | Standard driver | Appears as |
| --- | --- | --- |
| Windows | USBSTOR | Removable disk (drive letter) |
| Linux | usb-storage | `/dev/sd*` (a desktop environment may mount it automatically) |
| macOS | IOUSBMassStorageDriver | A volume in Finder |

- **The Host owns the filesystem.** If the ESP32 changes the content while the Host has it mounted, the Host keeps using its cache: the change may not show, or the filesystem may be corrupted. To change it from the ESP32, use `mediaPresent(false)` → change → `mediaPresent(true)` so the Host learns of a medium change. Whether the OS re-reads or remounts automatically is up to the OS and is not guaranteed.
- **Writes collect in the Host's cache.** Closing a file in a Host application does not mean it has been written over USB yet. Tell users to "eject" or `sync` on the Host before removing power.
- When raw blocks (no filesystem) are exposed, Windows and macOS offer to format them.
- Enumerating with no medium (`mediaPresent(false)`) can make some Hosts repeat TEST UNIT READY and delay the other interfaces of a composite device (behaviour seen with some versions of the TinyUSB Host).
- To check BOT directly on Linux with usb-storage detached, claim the interface with pyusb or similar.

## Standards and design rationale

- **Descriptors**: interface class 0x08, subclass 0x06 (SCSI transparent command set), protocol 0x50 (BOT), with one bulk IN/OUT pair (BOT 1.0 §4).
- **BOT**: the CBW/CSW format and the thirteen cases follow BOT 1.0 §5-§6, and reset recovery follows §5.3.4. SET_INTERFACE or a HALT clear is never treated as a BOT reset.
- **Sense**: follows the T10 ASC/ASCQ assignments. A medium change is reported once with 06/28/00.
- **Why run-time configuration**: capacity changes without a rebuild, several MSC Functions, natural read-only media, and nothing linked when unused. The cost is dynamic allocation at begin, more callbacks with a small buffer, and the move to synchronous callbacks.
- **Why `mediaPresent()` only states the medium's presence**: so it is not mistaken for an API that also stops I/O or releases the storage. The added cost is two words of MSC state and a comparison; no monitor task or lock is added.
- **No workarounds for Host bugs**: the Device behaves as the specifications say and adds no special behaviour to work around a faulty Host.

## Changes from v2

- `CFG_TUD_MSC` and `CFG_TUD_MSC_EP_BUFSIZE` are gone; leaving them in is a compile error. Set capacities in `EspUsbMscConfig`.
- Calling `tud_msc_*` directly is not possible.
- Callback registration and `vendorID` and friends return `esp_err_t` and work only while stopped.
- Callbacks that relied on TinyUSB's asynchronous completion or on BUSY (returning 0 to defer) must return the full amount synchronously.
- Right after the medium is re-inserted, the first command fails once with 06/28/00. Fix code that expects TEST UNIT READY to succeed immediately after insertion.

See [Migrating from v2](../migrating-from-v2.md) for details.

## Related examples

| Example | Content |
| --- | --- |
| [MSC](../../examples/MSC/) | RAM disk (raw blocks) |
| [MSCFatRamDisk](../../examples/MSCFatRamDisk/) | A FAT RAM disk with files on it |
| [MSCSdCard](../../examples/MSCSdCard/) | SD card reader |
| [FirmwareMSC](../../examples/FirmwareMSC/) | Firmware update by copying a file |
| [CompositeHidCdcMsc](../../examples/CompositeHidCdcMsc/) | HID with CDC and MSC |

Specifications: [USB MSC BOT 1.0](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf), [T10 ASC/ASCQ assignments](https://www.t10.org/lists/asc-num.htm)
