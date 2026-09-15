# Firmware Update over USB

> 日本語版: [ota-over-usb.ja.md](ota-over-usb.ja.md)

Two completely different things get called "OTA" on an ESP32 USB device, and
they fail in different ways:

- **Route A - hand the chip to the ROM.** The application stops existing. The
  ROM bootloader takes the USB port, and `esptool` or `dfu-util` rewrites the
  whole flash. This is what "get into boot mode and flash over the OTG cable"
  means. It cannot fail halfway into a brick, because the loader lives in mask ROM.
- **Route B - the sketch writes the next application itself.** Your code keeps
  running, keeps its USB identity, receives an image over whatever USB function
  it already has, and writes it into the other OTA partition. Nothing about
  this is USB-specific once the bytes have arrived; it is `esp_ota` / Arduino
  `Update`, exactly as over Wi-Fi.

Route A is a recovery and factory path. Route B is the product path. A shipping
device usually wants both, and the interesting question is how the user gets
from B to A when B is what is currently running - which is
[section 2.4](#24-entering-boot-mode-from-the-sketch).

## Contents

1. [What this library does and does not own](#1-what-this-library-does-and-does-not-own)
2. [Route A: hand the chip to the ROM](#2-route-a-hand-the-chip-to-the-rom)
3. [Route B: the sketch writes the next application](#3-route-b-the-sketch-writes-the-next-application)
4. [Route comparison](#4-route-comparison)
5. [Library feature or sketch code](#5-library-feature-or-sketch-code)
6. [Not implemented yet](#6-not-implemented-yet)

---

## 1. What this library does and does not own

EspUsbDevice owns the USB *transport*: the descriptors, the endpoints, the
class that carries your bytes. It does not own flash.

Two pieces of route B are the library's, because they are the parts that have
to be right rather than the parts that are a matter of taste:

- **`EspUsbDeviceDfu`** - a DFU function, in either shape, so a standard host
  tool can update the device. [3.5](#35-the-dfu-function).
- **`EspUsbDeviceMscFirmwareDisk`** - a drive the host drops a firmware file
  onto. [3.6](#36-the-firmware-drive).
- **`EspUsbDeviceFirmwareUpdate`** - the OTA partition writer both of those, and
  every hand-written route, sit on.
  [3.2](#32-the-write-side-is-always-the-same).

Everything else in route B is written with the classes this library already
ships (`EspUsbDeviceCdcSerial`, `EspUsbDeviceVendor`, `EspUsbDeviceNet`,
`EspUsbDeviceMsc`) on top of that writer.
[Section 6](#6-not-implemented-yet) says what is still missing.

Route A does not involve this library at all once `esp_restart()` has run - the
ROM owns the port from there. Getting *to* that point is library work, though:
`EspUsbDevice::rebootToBootloader()` ([2.4](#24-entering-boot-mode-from-the-sketch))
exists because the register differs per target and the obvious Arduino way of
doing it does not link here
([2.5](#25-usb_persist_restart-does-not-link-in-this-library)).

---

## 2. Route A: hand the chip to the ROM

### 2.1 What boot mode is

At reset the ROM reads a strapping pin. High (the default) means "boot the
application from flash". Low means "stay in the ROM download loader and wait for
a host". The download loader speaks the esptool protocol over UART, and - on the
chips this library supports - over USB as well.

It is not firmware. It cannot be corrupted by a bad flash write, which is what
makes route A the recovery path behind every other route on this page.

### 2.2 Entering it by hand, per chip

| Chip | Hold low at reset | Also required | Notes |
|---|---|---|---|
| ESP32-S2 | GPIO0 | - | |
| ESP32-S3 | GPIO0 | GPIO46 floating or low | |
| ESP32-P4 | GPIO35 | **GPIO36 high** | GPIO35 has a ~45 kΩ internal pull-up; a button needs a strong pull-down (e.g. 10 kΩ to GND). GPIO36 = 0 together with GPIO35 = 0 is an invalid combination and behaves unpredictably. |

On a devkit this is the BOOT button: hold BOOT, tap RESET, release BOOT. On a
board whose USB-UART bridge has DTR/RTS wired to the strapping pin and EN,
`esptool` does it for you and you never see it.

The pin is different on P4 (**GPIO35**, not GPIO0), and P4 has a *second*
strapping pin that must be high. This is the single most common reason a P4
board that "used to work like an S3" refuses to enter the loader.

### 2.3 Which USB interface the ROM answers on

This is the part that decides whether the OTG cable you already have plugged in
is usable, and it is where S3 and P4 genuinely differ.

| | ESP32-S2 | ESP32-S3 | ESP32-P4 |
|---|---|---|---|
| USB-Serial-JTAG peripheral | none | yes | yes |
| ROM serial loader over USB | USB-OTG, CDC-ACM in ROM | USB-Serial-JTAG (`303a:1001`) | USB-Serial-JTAG (`303a:1001`) |
| ROM DFU over USB-OTG | yes | yes, but see below | yes, **defective on P4 v3.1 and later** |
| Which pins | the OTG pins | GPIO19/20, shared | see the board schematic |

The ROM's DFU interface enumerates under VID `303a` with a per-target product ID
in the `00xx` range - ESP-IDF's own udev rule matches `303a:00??` rather than a
fixed PID, and so should yours.

Three consequences worth internalising:

**On S3 the internal full-speed PHY is shared.** One PHY, one pin pair
(GPIO19 = D-, GPIO20 = D+), two possible owners: the USB-Serial-JTAG peripheral
or the USB-OTG controller. Your sketch switched it to OTG when
`EspUsbDevice::begin()` created the PHY. **A reset switches it back**, because
the selection defaults to USB-Serial-JTAG unless the `USB_PHY_SEL` eFuse is
burned. So on a one-connector S3 board the same cable that was carrying your
HID device comes back as the ROM's serial loader after a reset into boot mode.
That is the single-cable flashing story, and it needs no eFuse and no DFU.

**ROM DFU on S3 is the awkward one.** Because the PHY defaults to
USB-Serial-JTAG, the ROM's *DFU* interface is not what you get after a plain
reset. ESP-IDF's answer is to burn `USB_PHY_SEL` permanently, which then costs
you USB-Serial-JTAG forever. The other answer is the ROM persist flag, which is
not permanent - [2.6](#26-rom-dfu-instead-of-the-rom-serial-loader-s2s3).

**On P4, prefer USB-Serial-JTAG.** Espressif's own guidance is that P4 v3.1 and
later have a defect in ROM DFU download and that USB-Serial-JTAG should be used
for flashing. Treat P4 ROM DFU as unavailable and plan around the
USB-Serial-JTAG port. Note that this is a *different port* from the high-speed
OTG connector your device is probably on: on P4 the high-speed controller has
its own PHY, so unlike S3 there is no "the same cable comes back as the loader"
effect for an HS device. A P4 device on the *full-speed* port does get it - that
PHY is shared with USB-Serial-JTAG the same way the S3's is
([guide, 3.2](usb-device-guide.md#32-choosing-fs-or-hs-on-esp32-p4)), which is
one more reason `examples/P4FullSpeedDevice` is the friendlier starting point.

### 2.4 Entering boot mode from the sketch

This is the reliable, button-free path, and it is one call:

```cpp
device.rebootToBootloader();   // does not return
```

It detaches the USB device so the host records a disconnect rather than a
vanished device, sets the target's download-boot flag, and restarts. The ROM
checks that flag in addition to the strapping pin, so the chip lands in the
download loader exactly as if BOOT had been held.

The reason it is a library call rather than four lines in your sketch is that
those four lines are not the same on every chip. On S2/S3 the flag is bit 0 of
`RTC_CNTL_OPTION1_REG`, a register that holds nothing else, so a whole-register
write is safe (it is what Arduino-ESP32 itself does). On P4 the RTC controller
is gone; the flag is bit 2 of `LP_SYSTEM_REG_SYS_CTRL_REG`, which also carries
the software-reset bit, the `DIG_FIB` field and `IO_MUX_RESET_DISABLE` - so it
has to be **set**, never written. Copying the S3 recipe onto a P4 clobbers three
unrelated fields, and the library call exists to stop that.

`device.rebootToRomDfu()` is the same thing for a host that drives `dfu-util`
rather than `esptool` ([2.6](#26-rom-dfu-instead-of-the-rom-serial-loader-s2s3)).

Verified on hardware while EspUsbDevice's own USB stack was running: an
ESP32-S3 (rev v0.2) and an ESP32-P4 (rev v1.3) both landed in the download
loader, and `esptool --before no-reset chip-id` connected without any button or
DTR/RTS reset. [`examples/FirmwareBootMode`](../examples/FirmwareBootMode/) is
the sketch.

**The flag does not stick.** After `esptool` flashed and hard-reset the board,
the application booted normally - the ROM clears it on the way through. There is
no risk of a board that reboots into the loader forever.

Three things to get right around those four lines:

- **Trigger it from the sketch's own task, not from a USB callback.** Every
  class callback in this library runs on the usbd task ([advanced guide, section
  8](usb-device-advanced.md#8-callback-context)). Set a flag there and call
  `rebootToBootloader()` from `loop()`: restarting from inside a callback cuts
  off the control transfer the host is still finishing.
- **Gate it behind something deliberate.** A stray byte on a CDC port should not
  brick a user's session. The convention hosts already speak is the 1200-baud
  touch: the Arduino IDE and `arduino-cli` open the port at 1200 baud and drop
  DTR to ask a board to enter its loader. `EspUsbDeviceCdcSerial` hands you that
  request directly:

```cpp
EspUsbDeviceCdcSerial serial(device);
volatile bool rebootRequested = false;

serial.onLineCoding([](const EspUsbDeviceCdcLineCoding &coding) {
  if (coding.baud == 1200) {
    rebootRequested = true;   // usbd task - just raise the flag
  }
});

void loop() {
  if (rebootRequested) {
    device.rebootToBootloader();
  }
}
```

  `dfu-util -e` is the other gesture hosts already have: add an
  `EspUsbDeviceDfu` in `Runtime` mode and the host asks with `DFU_DETACH`, which
  the class answers by restarting into the loader. On a vendor or WebUSB
  interface, use a dedicated control request instead. On a device with no data
  port at all, a long button press is fine - the point is that it is deliberate.

### 2.5 `usb_persist_restart()` does not link in this library

Arduino-ESP32 ships `usb_persist_restart(RESTART_BOOTLOADER)` for exactly this
job, and every tutorial reaches for it. **It cannot be used from an
EspUsbDevice sketch.** Calling it pulls `esp32-hal-tinyusb.c` into the link, and
that file defines two of the same TinyUSB callbacks this library defines:

```
esp32-hal-tinyusb.c:405: multiple definition of `tud_descriptor_bos_cb';
  EspUsbDevice.cpp:451: first defined here
esp32-hal-tinyusb.c:417: multiple definition of `tud_vendor_control_xfer_cb';
  EspUsbDevice.cpp:759: first defined here
```

This is the same exclusivity as `USB.begin()`
([guide, section 3.5](usb-device-guide.md#35-mutually-exclusive-with-the-stock-arduino-esp32-usb-stack)),
just arriving as a link error rather than a runtime conflict. Use
`EspUsbDevice::rebootToBootloader()` ([2.4](#24-entering-boot-mode-from-the-sketch));
it does what `usb_persist_restart()` does, minus the parts that only make sense
for the core's own stack.

It is also worth knowing that `usb_persist_restart()` is a **no-op on P4** even
in a stock Arduino sketch - the whole implementation sits behind
`#if CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3`. Any P4 guidance
that tells you to call it is wrong.

### 2.6 ROM DFU instead of the ROM serial loader (S2/S3)

If you specifically want `dfu-util` rather than `esptool`, the S2/S3 ROM takes a
persist flag in addition to the download-boot flag. Setting it asks the ROM to
come up as a DFU device on USB-OTG rather than as a serial loader, without
burning `USB_PHY_SEL`:

```cpp
device.rebootToRomDfu();   // ESP32-S2 / ESP32-S3; false without restarting on P4
```

Underneath it is `chip_usb_set_persist_flags(USBDC_BOOT_DFU)` followed by the
same download-boot flag and restart. Those are ROM symbols exported by the S2/S3
ESP-IDF builds. The call was **not** verified end-to-end on the test rig, whose
S3 boards do not have their native USB port wired to the test PC - the chip
reaching the loader is verified, the host binding a DFU interface is not. P4 has
no equivalent: its ESP-IDF build exports no ROM USB headers at all, and ROM DFU
there is the defective path from
[2.3](#23-which-usb-interface-the-rom-answers-on), so the call returns `false`
and restarts nothing.

The host side is then `dfu-util` or `idf.py dfu-flash`, against a DFU image
built by `idf.py dfu` - not a plain `.bin`.

### 2.7 What disables all of this

- **Secure Boot or flash encryption disables the ROM's USB-OTG stack.** Serial
  emulation and DFU on that port both stop working. UART and USB-Serial-JTAG
  remain.
- **Secure Download Mode disables DFU outright**, and restricts the serial
  loader to a small command set.
- **`USB_PHY_SEL` is a one-way eFuse.** Burning it to reach ROM DFU on S3 costs
  you USB-Serial-JTAG permanently, including the ROM's serial loader over USB.
  Think hard before making that trade on a product.

If your product enables Secure Boot, route A over USB is not your recovery path;
a UART header is.

---

## 3. Route B: the sketch writes the next application

### 3.1 Partitions

Route B needs two application partitions and an `otadata` partition. The Arduino
default partition scheme already has them:

```
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x140000,
app1,     app,  ota_1,   0x150000,0x140000,
```

1.25 MB per slot. For scale, an EspUsbDevice HID sketch is about 320 KB and a
CDC-NCM sketch with a web server and `HTTPUpdateServer` is about 540 KB, so the
default scheme is comfortable. `huge_app` is **not** usable: it has one app
partition, so route B cannot work at all under it. Check early -
`esp_ota_get_next_update_partition(NULL)` returning `NULL` is the symptom.

### 3.2 The write side is always the same

Whatever USB function carried the bytes, the flash side is
`EspUsbDeviceFirmwareUpdate`:

```cpp
EspUsbDeviceFirmwareUpdate update;

if (!EspUsbDeviceFirmwareUpdate::available()) { /* no second app partition */ }

update.begin();                       // or begin(imageSize) when it is known
update.write(chunk, chunkLength);     // repeatedly, in arrival order
if (update.end()) {                   // verifies, then moves the boot partition
  esp_restart();
}
```

Three things it does that a hand-rolled `esp_partition_write()` loop forgets:

- `end()` checks the image header, the declared length and the checksum before
  moving the boot partition, so a truncated or corrupt upload fails there rather
  than at the next boot.
- `write()` refuses the first byte past the partition, rather than letting a
  host that keeps sending discover the problem at the end of a long upload.
- `begin()` opens the partition in sequential-erase mode, so the flash is erased
  as the write advances. Asking for the whole partition up front costs seconds
  in one block, and a USB callback is the wrong place to spend them.

`available()`, `capacity()` and `targetLabel()` answer before an upload starts
whether one can work at all - under a single-app scheme there is nowhere to put
a new image, and that is worth saying at startup rather than on the last block.

For a device that must survive a bad image, call
`EspUsbDeviceFirmwareUpdate::markValid()` once the new firmware has proved
itself - after it enumerates, not in `setup()` - and build the bootloader with
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`. Without that pair, a new image that
comes up as garbage is only recoverable through route A.
`rollback()` and `cancelPendingBoot()` are the two ways back.

### 3.3 Transports that work today

All four of these are buildable now with the classes the library already ships.
None of them needs a library change.

| Transport | Host side | Endpoint cost | Shape |
|---|---|---|---|
| **CDC-ACM** (`EspUsbDeviceCdcSerial`) | any serial tool, a Python script | 2 (notif IN + bulk duplex) | Simplest. Send a length, stream the bytes, hand each chunk to `Update.write()`. Full-speed bulk caps at 1.216 MB/s on paper and a CDC framing plus a flash write per chunk keeps you well under it - fine for a 300 KB image, slow for a 1 MB one. |
| **Vendor / WebUSB** (`EspUsbDeviceVendor`) | PyUSB, WinUSB, or a browser page over WebUSB | 1 (bulk duplex) | The best fit for a purpose-built updater. Control requests give you a clean command channel (`START`, `size`, `COMMIT`) alongside the bulk data. On P4 HS this is by far the fastest route - the library's own 4096/4096 FIFO settings measured 21.5 MB/s one-way ([advanced guide, 6.3](usb-device-advanced.md#63-measured-throughput)). |
| **CDC-NCM + HTTP** (`EspUsbDeviceNet`) | a browser | 2 (notif IN + bulk duplex) | The device is a USB network adapter with its own DHCP server; `HTTPUpdateServer` gives you the stock `/update` upload form at `http://192.168.7.1/update`. No drivers, no host tool, no library code. Verified to build at 40% of the default app partition on S3. |
| **MSC** (`EspUsbDeviceMscFirmwareDisk`) | drag and drop | 1 (bulk duplex) | The nicest UX, and the one with the most host-dependent behaviour. [3.6](#36-the-firmware-drive). |
| **DFU** (`EspUsbDeviceDfu`) | `dfu-util` | **0** | A standard host tool and a standard protocol, on EP0. The library implements the whole path; the sketch supplies callbacks. [3.5](#35-the-dfu-function). |

All five ship as examples:
[`FirmwareCDC`](../examples/FirmwareCDC/),
[`FirmwareVendor`](../examples/FirmwareVendor/),
[`FirmwareHTTP`](../examples/FirmwareHTTP/),
[`FirmwareMSC`](../examples/FirmwareMSC/) and
[`FirmwareDFU`](../examples/FirmwareDFU/).

The NCM route deserves a note: it is the only one where the host needs **no**
software at all beyond a browser. If your users are not developers, that is the
one to reach for; if they are, DFU is one command and costs no endpoints.

### 3.4 Where the bytes must not be written

Flash erase is slow - milliseconds per 4 KB sector - and every class callback in
this library runs on the usbd task at the highest priority
([advanced guide, section 8.1](usb-device-advanced.md#81-everything-runs-on-the-usbd-task)).
Calling `Update.write()` straight from `onRx()` or an MSC `write10` callback
stalls the whole device for the duration, including every other function of a
composite device.

For a bulk transport this is usually survivable - the host just sees NAKs and
slows down - but it is not free, and on an audio or HID composite it is audible
and visible. The pattern that holds up:

```
USB callback  ->  push into a queue / ring buffer  ->  return immediately
sketch task   ->  pop, Update.write(), repeat
```

MSC is the exception where it usually *cannot* be deferred: `write10` must
report success or failure synchronously, so the flash write happens inside the
callback. `CFG_TUD_MSC_EP_BUFSIZE` is 4096 here, which lines up with the flash
sector size and keeps the stall to one erase-plus-write per callback.

### 3.5 The DFU function

`EspUsbDeviceDfu` is route B with a standard protocol and a standard host tool,
and it is the cheapest function in this library:

```cpp
EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Download, "Firmware");
```

```sh
dfu-util -D firmware.bin
```

**It costs no endpoints.** DFU is an interface descriptor, a functional
descriptor and a set of control requests, all of which travel on EP0. On the S3,
where the endpoint budget is what stops composite devices
([guide, 3.3](usb-device-guide.md#33-the-endpoint-budget)), that makes it the one
function that can always be added - the device above is a keyboard with its two
endpoints and a DFU interface with none, which is what the peer test asserts.

Two modes:

| Mode | Answers | Default action |
|---|---|---|
| `Download` | the whole DFU download | writes the image, verifies it, restarts into it |
| `Runtime` | `DFU_DETACH` only | `rebootToBootloader()` - the ROM loader, route A |

`Runtime` is the smaller idea and worth knowing about on its own: it makes
`dfu-util -e` a standard way for a host to ask any EspUsbDevice device to enter
boot mode, with no protocol of your own
([2.4](#24-entering-boot-mode-from-the-sketch)).

Verified end to end against a PC: an ESP32-P4 running this class enumerated as a
DFU device at high speed, a host tool read `wTransferSize=1024`,
`bcdDFU=0x0110`, `canDnload`, `manifestationTolerant=0` off the wire, and a
385 KB image went across in 376 blocks in 2.7 s (139 KiB/s) - on EP0, with the
device holding no endpoints of its own. The device then reported
`dfuMANIFEST-WAIT-RESET`, moved the boot partition and restarted into the image
it had just been given.

Things worth knowing about the `Download` mode:

- **The image replaces the running sketch.** Send a build that also has a DFU
  interface, or the next update goes through route A.
- **Failures are DFU status codes, not stalls.** A file that is not an ESP
  application is refused on its first block with status 3 (`errWRITE`); one that
  does not survive verification fails at the end with status 7 (`errVERIFY`).
  Neither moves the boot partition, so a wrong file costs nothing but time.
  `CLRSTATUS` - which `dfu-util` sends by itself - returns the device to idle.
- **The block size is `CFG_TUD_DFU_XFER_BUFSIZE`**, 1024 bytes by default, and
  is also the `wTransferSize` the functional descriptor declares. Raise it from
  `build_opt.h` (`-DCFG_TUD_DFU_XFER_BUFSIZE=4096`) for large images; the
  descriptor follows. The cost is a static buffer that every sketch carries,
  which is why the default is not a whole flash sector.
- **The function declares itself manifestation-intolerant**, because it restarts
  into the new image. That is what tells the host to expect the device to
  disappear rather than return to idle. `restartWhenComplete(false)` keeps the
  sketch running instead, and then it is the sketch's job to restart.
- The callbacks run on the usbd task like every other class callback, but here
  the flash write is *supposed* to happen inline: DFU has a `bwPollTimeout`
  field for exactly this, so the host is told to wait rather than being left to
  guess ([3.4](#34-where-the-bytes-must-not-be-written) is about the transports
  that have no such field).

### 3.6 The firmware drive

`EspUsbDeviceMscFirmwareDisk` presents a small FAT volume **whose data region is
the OTA partition**. The host copies a `.bin` onto the drive; the device writes
it to flash as the sectors arrive, verifies it, and restarts into it.

```cpp
EspUsbDevice device;
EspUsbDeviceMsc msc(device);
static uint8_t diskStorage[16 * 1024];
EspUsbDeviceMscFirmwareDisk disk(diskStorage, sizeof(diskStorage));

disk.begin("ESPUSB");
disk.addTextFile("README.TXT", "Copy a firmware .bin here.\r\n");
disk.attach(msc);
```

**The image is never in RAM.** `diskStorage` holds the boot sector, both FAT
copies, the root directory and a scratch area - a few kilobytes. Every sector
past that is the partition, read back through `esp_partition_read()` and written
through `EspUsbDeviceFirmwareUpdate`. That is what lets a board with 320 KB of
RAM accept a 1.25 MB image, and it is the single thing that makes this route
possible at all.

What the class decides for you, and why each one is a decision rather than an
oversight:

- **Cluster size.** FAT12 addresses 4084 clusters, so `begin()` picks the
  smallest cluster from 4 KB upwards that keeps the whole partition inside that
  limit. 4 KB is also the flash sector size, so a cluster boundary is an erase
  boundary. (Arduino-ESP32's `FirmwareMSC` instead switches to FAT16 above 0xFF4
  sectors; raising the cluster size is the simpler half of that trade.)
- **What counts as firmware.** A write into the firmware region whose first byte
  is the ESP image magic `0xE9` starts an update. Anything else there is
  dropped - it is a file the user copied by mistake, or host metadata that
  spilled out of the scratch area, and neither should reach flash.
- **When it is done.** Two answers, whichever comes first: the file's directory
  entry says how long it is and the byte count reaches that, or the host ejects
  the drive. The directory entry usually lands first; the eject is the backstop
  for a host that never writes one this code can read.
- **What a raw `.bin` will not do.** Writes must ascend. A raw image says
  nothing about where its bytes belong, so the write position is the only thing
  that can; a write that jumps backwards or leaves a hole is refused with
  `ESP_ERR_INVALID_STATE`, `onError()` fires and the update is abandoned. It is
  never left half-applied and looking finished.

**Give it a `.uf2` and that last restriction goes away.** A UF2 file is a
sequence of self-describing 512-byte blocks, each carrying its own target
address, block index and total count, and the drive takes those too - the format
is chosen from the first bytes to arrive, so the same drive accepts either.
What the container buys:

| | raw `.bin` | `.uf2` |
|---|---|---|
| Write order | ascending only | **any** - each block says where it goes |
| Host metadata | ignored by the `0xE9` heuristic | rejected by the block magic |
| Length | from the FAT directory entry | from the header |
| Completion | byte count reaches that length, or eject | exact: every block index seen |
| A block written twice | indistinguishable from progress | counted once |
| Image for the wrong chip | accepted, fails verification later | refused on the first block |

The last two are worth dwelling on. A host that rewrites a block is normal, and
with a raw image it inflates the byte count toward an early commit; with UF2 a
seen-block bitmap makes the count exact. And the family ID is the only check in
any of this that catches an ESP32-S3 image on an ESP32-P4 *before* flash is
touched - `EspUsbDeviceMscFirmwareDisk::uf2FamilyId()` returns the one this
build expects, which is what `uf2conv.py --family` has to be given.

Underneath, UF2 switches the writer into
`EspUsbDeviceFirmwareUpdate::beginRandomAccess()` / `writeAt()`: the header says
how long the image is, so that much partition is erased before the first block
lands and every later block can go anywhere inside it. A raw stream cannot do
that, because nothing tells it the length until the end.

Verified on hardware: eight UF2 blocks written in **reverse order** produce the
same image, a duplicated block does not advance the count, and a block carrying
the original ESP32's family ID is refused before anything is written.

The friction is host-side: producing a `.uf2` from an Arduino build means a
conversion step (`uf2conv.py --family <id> --base 0x0`) that Arduino does not do
for you. That is the trade - a raw `.bin` is what the IDE already gives you.

The scratch area is the part to size deliberately. It is what absorbs
`System Volume Information`, `.fseventsd` and `.Spotlight-V100`, and a host that
fills it starts allocating clusters inside the firmware region. 16 KB of
`storage` leaves a comfortable scratch; 8 KB is the floor.

Compared with [3.5](#35-the-dfu-function): the drive has the better UX, DFU has
the better guarantees for less - it costs no endpoints, where a drive costs a
bulk pair. Fed a `.uf2` the two are close on correctness; fed a raw `.bin` the
drive is trusting the host to behave. Ship DFU when the person doing the update
has a terminal, the drive when they do not, and both when you do not know -
DFU is free to add.

### 3.7 Windows binds the driver itself

DFU has no in-box Windows driver of its own: nothing claims interface class
0xFE, so a DFU device would land in Device Manager with a yellow mark and the
user would be sent to [Zadig](https://zadig.akeo.ie/) - which is not something
you can ship.

The device asks for WinUSB instead, in a Microsoft OS 2.0 descriptor set, and
the library builds that set for every interface that needs it: the vendor
interface and DFU. Nothing to configure.

Two rules decide the shape of that set, and both were measured rather than
assumed:

- **One interface: flat.** The compatible ID goes directly under the set header.
  The configuration and function subsets exist to attach a compatible ID to one
  *function* of a composite device, and Windows resolves them through
  `usbccgp.sys` - which it only loads for a composite. On a single-interface
  device the subsets have nothing to attach to and Windows binds nothing.
- **More than one: one function subset per interface that wants WinUSB.** A
  function subset naming only some of them leaves the others with no driver.

Measured on an ESP32-P4 against Windows 11, same board, same composite, only
the descriptor set changed:

| Configuration | DFU interface | vendor interface |
|---|---|---|
| DFU alone, no Microsoft descriptor | `Error`, no driver | - |
| DFU + vendor, one function subset (vendor only) | `problem=28` | `WINUSB` |
| **DFU alone, flat set** | **`WINUSB`** | - |
| **DFU + vendor, two function subsets** | **`WINUSB`** | **`WINUSB`** |

The DFU function is given the compatible ID and **no** `DeviceInterfaceGUIDs`
property: libusb - and so `dfu-util` - finds a WinUSB device through the USB
device interface class rather than a per-function GUID, and the measurement
above confirms the binding does not need one. That keeps the set at 30 bytes for
a DFU-only device.

`bcdUSB` is raised to 0x0201 whenever a BOS exists, which is the threshold at
which a host asks for one at all. Not 0x0210: that would claim a level of USB
2.1 compliance this device does not implement, and 0x0201 is measured to bind
WinUSB on Windows 11.

On Linux and macOS none of this is needed - they bind by class or let libusb
claim the interface directly - but emitting it costs 30 bytes and removes the
one platform where a user would otherwise have to install something.

---

## 4. Route comparison

| Route | Chips | Needs boot mode | Host tool | Can brick | Library support today |
|---|---|---|---|---|---|
| ROM serial loader over USB | S2 (OTG CDC), S3 / P4 (USB-Serial-JTAG) | yes | `esptool`, `esptool-js` in a browser | no | ✅ `rebootToBootloader()` - [2.4](#24-entering-boot-mode-from-the-sketch) |
| ROM DFU over USB-OTG | S2, S3; P4 defective | yes | `dfu-util` | no | ✅ `rebootToRomDfu()` - [2.6](#26-rom-dfu-instead-of-the-rom-serial-loader-s2s3) |
| **Device-implemented DFU** | all | no | `dfu-util` | yes, without rollback | ✅ `EspUsbDeviceDfu` - [3.5](#35-the-dfu-function) |
| Self-OTA over CDC | all | no | any serial tool | yes, without rollback | ✅ classes + `EspUsbDeviceFirmwareUpdate` |
| Self-OTA over Vendor / WebUSB | all | no | PyUSB / browser | yes, without rollback | ✅ classes + `EspUsbDeviceFirmwareUpdate` |
| Self-OTA over CDC-NCM + HTTP | all | no | a browser | yes, without rollback | ✅ [`FirmwareHTTP`](../examples/FirmwareHTTP/) |
| Self-OTA over MSC (drag and drop) | all | no | the file manager | yes, without rollback | ✅ `EspUsbDeviceMscFirmwareDisk` - [3.6](#36-the-firmware-drive) |
| Self-OTA over MSC, UF2 container | all | no | the file manager | yes, without rollback | ✅ same class, any write order - [3.6](#36-the-firmware-drive) |
| UF2 *bootloader* (TinyUF2) | S2 / S3 | n/a | drag and drop | no | ❌ out of scope - [6.1](#61-uf2-as-a-bootloader) |

---

## 5. Library feature or sketch code

The dividing line this library already uses elsewhere: **the library owns
anything that has to be right in the descriptors or the endpoint budget; the
sketch owns policy.** Applied to firmware update:

**In the library** (the first three now exist)

- ✅ A DFU and DFU-runtime *class*. It is an interface descriptor, a functional
  descriptor, a state machine and a set of control requests - exactly the things
  a sketch must not hand-roll, and exactly what `EspUsbDeviceClass` exists for.
  `EspUsbDeviceDfu`, [3.5](#35-the-dfu-function).
- ✅ `rebootToBootloader()` as a supported call. Small, but per-target register
  knowledge that every sketch would otherwise copy wrongly from an S3 tutorial
  onto a P4. [2.4](#24-entering-boot-mode-from-the-sketch).
- ✅ A firmware sink: "here are bytes, put them in the other OTA partition,
  verify, switch". `EspUsbDeviceFirmwareUpdate`, shared by every route in
  [3.3](#33-transports-that-work-today); it is where the size clamp, the
  sequential erase and the verify-before-commit rule live.
- ✅ The FAT layer that notices a file appearing on a drive. That is filesystem
  parsing, it is subtle, and getting the geometry wrong is invisible until a
  particular host mounts it. `EspUsbDeviceMscFirmwareDisk`,
  [3.6](#36-the-firmware-drive).

**In a sample sketch**

- *When* to enter boot mode: the 1200-baud touch, a button, a vendor command, a
  menu item. Product policy, and different per device.
  [`FirmwareBootMode`](../examples/FirmwareBootMode/) shows three.
- The host-side counterpart: the Python uploader, the WebUSB page, the HTML
  form. Not library code in any sense.
  [`FirmwareHTTP`](../examples/FirmwareHTTP/) is the case where the host side is
  a browser and there is nothing to write.
- The wire protocol of a vendor-specific updater - framing, checksums,
  acknowledgement. A sample should show *one* good one; the library should not
  impose it. This is why DFU is in the library and a vendor protocol is not: DFU
  is somebody else's standard, so implementing it commits the library to nothing.
- Rollback policy, progress indication, what the device does while it updates.
  [`FirmwareDFU`](../examples/FirmwareDFU/) shows the callbacks and where
  `markValid()` belongs.

---

## 6. Not implemented yet

Everything the first version of this document listed is now in the library:
`EspUsbDeviceDfu`, `EspUsbDeviceMscFirmwareDisk` (raw and UF2),
`EspUsbDeviceFirmwareUpdate`, `EspUsbDevice::rebootToBootloader()` - and the
Microsoft OS 2.0 descriptors that let Windows bind WinUSB to a DFU interface
without Zadig ([3.7](#37-windows-binds-the-driver-itself)). What follows is one
thing that is deliberately out of scope.

### 6.1 UF2 as a bootloader

[TinyUF2](https://github.com/adafruit/tinyuf2) replaces the second-stage
bootloader with one that presents a UF2 drive, and Espressif's
[`esp_tinyuf2`](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_device/esp_tinyuf2.html)
packages it for ESP-IDF along with an application-side variant.

The application-side variant is [3.6](#36-the-firmware-drive), which this
library now has. The bootloader variant is **out of scope**: it replaces the
bootloader, it is an ESP-IDF component rather than an Arduino library, and its
advantage - working when the application is too broken to run - is the one
[route A](#2-route-a-hand-the-chip-to-the-rom) already provides from mask ROM
with nothing to install and nothing to go wrong.

---

## Related documents

- [USB Device Development Guide](usb-device-guide.md) - fundamentals, connectors, bring-up
- [USB Device Development Guide (Advanced)](usb-device-advanced.md) - callback context, endpoint budget, adding a class
- [Troubleshooting](troubleshooting.md) - symptom-first fixes
- [examples/FirmwareDFU](../examples/FirmwareDFU/) - `dfu-util` updates a running sketch
- [examples/FirmwareHTTP](../examples/FirmwareHTTP/) - a browser uploads over the USB network interface
- [examples/FirmwareMSC](../examples/FirmwareMSC/) - drag a firmware file onto a drive the board presents
- [examples/FirmwareBootMode](../examples/FirmwareBootMode/) - three ways to ask for the ROM loader
- [examples/UsbNetwork](../examples/UsbNetwork/) - the CDC-NCM + web server base the HTTP route builds on
- [tests/peer/usb_dfu](../tests/peer/usb_dfu/) - the two-board test behind the DFU claims here
- [tests/single/msc_firmware_disk](../tests/single/msc_firmware_disk/) - the test behind the firmware-drive claims here
- [ESP-IDF: Device Firmware Upgrade via USB](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/dfu.html)
- [esptool: Boot Mode Selection (ESP32-S3)](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html) / [(ESP32-P4)](https://docs.espressif.com/projects/esptool/en/latest/esp32p4/advanced-topics/boot-mode-selection.html)
- [ESP-IoT-Solution: USB-OTG peripheral introduction](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_overview/usb_otg.html) - the P4 v3.1 DFU defect
