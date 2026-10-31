# Firmware Update over USB

> 日本語版: [ota-over-usb.ja.md](ota-over-usb.ja.md)

Two different things are called "OTA" on an ESP32 USB device, and they fail in
different ways:

- **Route A: hand the chip to the ROM.** The application stops. The ROM
  bootloader takes a USB port, and `esptool` (or `dfu-util` on some chips)
  rewrites the flash. This is "boot mode". It cannot be bricked by a bad write,
  because the loader is in mask ROM.
- **Route B: the sketch writes the next application.** Your sketch keeps
  running with its USB identity, receives an image over a USB Function it
  already has, and writes it into the other OTA partition. Once the bytes have
  arrived, nothing is USB-specific: it is `esp_ota`, the same as over Wi-Fi.

Route A is the recovery and factory path. Route B is the product path. A
shipping device usually wants both, and the question is how a user gets from B
to A while B is running; that is [2.4](#24-entering-boot-mode-from-the-sketch).

## Contents

1. [What this library owns](#1-what-this-library-owns)
2. [Route A: hand the chip to the ROM](#2-route-a-hand-the-chip-to-the-rom)
3. [Route B: the sketch writes the next application](#3-route-b-the-sketch-writes-the-next-application)
4. [Route comparison](#4-route-comparison)
5. [Library feature or sketch code](#5-library-feature-or-sketch-code)
6. [What has been checked on hardware](#6-what-has-been-checked-on-hardware)
7. [Out of scope: UF2 as a bootloader](#7-out-of-scope-uf2-as-a-bootloader)

---

## 1. What this library owns

EspUsbDevice owns the USB transport: descriptors, endpoints, and the class that
carries the bytes. It does not own flash. Four pieces are in the library,
because they have to be right rather than chosen per product:

| Piece | Kind | Role |
| --- | --- | --- |
| `EspUsbDevice::rebootToBootloader()` | device call | enter the ROM download loader from a running sketch ([2.4](#24-entering-boot-mode-from-the-sketch)) |
| `EspUsbDeviceFirmwareUpdate` | helper, not a Function | writes an image into the spare OTA partition, verifies it, and switches the boot partition ([3.2](#32-the-write-side-is-always-the-same)) |
| `EspUsbDeviceDfu` | Function | USB DFU 1.1, in Runtime or Download mode ([3.5](#35-the-dfu-function)) |
| `EspUsbDeviceMscFirmwareDisk` | MSC helper | a drive whose data region is the OTA partition ([3.6](#36-the-firmware-drive)) |

Every other route uses the Functions the library already has
(`EspUsbDeviceCdcSerial`, `EspUsbDeviceVendor`, `EspUsbDeviceNet`) on top of
`EspUsbDeviceFirmwareUpdate`. The `Firmware*` examples show each one.

---

## 2. Route A: hand the chip to the ROM

### 2.1 What boot mode is

At reset the ROM reads a strapping pin. High (the default) boots the
application from flash. Low keeps the chip in the ROM download loader, waiting
for a host. The loader speaks the esptool protocol over UART and, on the chips
this library supports, over USB too. It is not firmware and cannot be damaged
by a bad flash write, which is why it is the recovery path behind every other
route here.

### 2.2 Entering it by hand

| Chip | Hold low at reset | Also required | Notes |
| --- | --- | --- | --- |
| ESP32-S2 | GPIO0 | - | |
| ESP32-S3 | GPIO0 | GPIO46 floating or low | |
| ESP32-P4 | GPIO35 | **GPIO36 high** | GPIO35 has a weak internal pull-up, so a button needs a strong pull-down. GPIO35 = 0 with GPIO36 = 0 is an invalid combination |

On a devkit: hold BOOT, tap RESET, release BOOT. On a board whose USB-UART
bridge drives the strapping pin and EN through DTR/RTS, `esptool` does it for
you. On P4 the pin is GPIO35, not GPIO0, and a second strapping pin must be
high; this is the most common reason a P4 board refuses to enter the loader.

### 2.3 Which USB port the ROM answers on

| | ESP32-S2 | ESP32-S3 | ESP32-P4 |
| --- | --- | --- | --- |
| USB Serial/JTAG peripheral | none | yes | yes |
| ROM serial loader over USB | USB OTG (CDC in ROM) | USB Serial/JTAG (`303a:1001`) | USB Serial/JTAG (`303a:1001`) |
| ROM DFU over USB OTG | yes | only with the `USB_PHY_SEL` eFuse burned | treat as unavailable (defective from chip revision v3.1) |
| Pins | the OTG pins | GPIO19/20, shared | see the board schematic |

The ROM's DFU device uses VID `303a` with a per-chip product ID in the `00xx`
range; ESP-IDF's udev rule matches `303a:00??`, and so should yours.

Consequences:

- **On S3 the full-speed PHY is shared.** One PHY and one pin pair (GPIO19 = D-,
  GPIO20 = D+) serve either USB Serial/JTAG or USB OTG. `device.begin()` routes
  it to OTG. The routing lives in `RTC_CNTL_USB_CONF_REG`, which survives a
  software reset; if nothing routes it back, the ROM loader starts with the
  pads on a controller it does not drive and the connector goes dark.
  `rebootToBootloader()` routes the PHY back to USB Serial/JTAG before it
  restarts, so the same cable then shows `303a:1001` and `esptool` works over
  it.
- **ROM DFU on S3 needs the eFuse.** At boot the ROM routes the pads from the
  `USB_PHY_SEL` eFuse, whatever the application left behind. Burning it costs
  the board USB Serial/JTAG permanently. If you want DFU on an S3, use
  `EspUsbDeviceDfu` ([3.5](#35-the-dfu-function)) instead; no eFuse is involved.
- **On P4, flash over USB Serial/JTAG.** Espressif's guidance is that ROM DFU
  download is defective on P4 v3.1 and later. USB Serial/JTAG is a different
  connector from the HS OTG connector, which has its own PHY, so a P4 HS device
  does not "come back as the loader" on the same cable. The P4 FS PHY is shared
  with USB Serial/JTAG the way the S3's is.

### 2.4 Entering boot mode from the sketch

```cpp
device.rebootToBootloader();   // does not return on S2/S3/P4
```

It detaches from USB so the host records a disconnect, sets the chip's
download-boot flag, and restarts. The ROM checks the flag as well as the
strapping pin, so the chip enters the loader as if BOOT had been held. The ROM
clears the flag on the way through: after `esptool` flashes and resets the
board, the application boots normally.

It is a library call because the register differs per chip. On S2/S3 the flag
is the only bit in `RTC_CNTL_OPTION1_REG`, so a whole-register write is safe.
On P4 it is bit 2 of `LP_SYSTEM_REG_SYS_CTRL_REG`, which also holds the
software-reset bit, `DIG_FIB` and `IO_MUX_RESET_DISABLE`; it must be set, never
written. Copying the S3 recipe onto a P4 clobbers those fields.

Three rules around it:

- **Call it from the sketch's task, not from a USB callback.** Callbacks run on
  the USB worker. Set a flag there and act on it in `loop()`; restarting inside
  a callback cuts off the control transfer the host is still finishing.
- **Gate it behind something deliberate.** A stray byte must not throw a user
  into the loader. Hosts already have two gestures for this:
  - The **1200-baud touch**: the Arduino IDE and `arduino-cli` open the port at
    1200 baud to ask for the loader.
  - **`dfu-util -e`**: an `EspUsbDeviceDfu` in Runtime mode receives
    `DFU_DETACH`. With no handler it restarts into the ROM loader by itself
    about 100 ms after the request completes.
- On a vendor interface, use a dedicated control request; on a device with no
  data port, a long button press.

```cpp
EspUsbDeviceCdcSerial port(device, "Console");
EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Runtime, "Bootloader");
volatile bool bootloaderRequested = false;

void setup() {
  // Register before device.begin(); afterwards these return ESP_ERR_INVALID_STATE.
  port.onLineCoding([](const EspUsbDeviceCdcLineCoding &coding) {
    if (coding.baud == 1200) bootloaderRequested = true;   // USB worker: flag only
  });
  dfu.setDetachHandler([]() { bootloaderRequested = true; });
  device.begin();
}

void loop() {
  if (bootloaderRequested) device.rebootToBootloader();
}
```

`dfu.onDetach()` decides at SETUP whether to accept `DFU_DETACH` (returning
`false` stalls it). `setDetachHandler()` replaces the default restart after the
request has been accepted; once accepted, `end()` or a bus reset does not
cancel it. See [`FirmwareBootMode`](../examples/FirmwareBootMode/).

### 2.5 Do not use `usb_persist_restart()`

Arduino-ESP32's `usb_persist_restart(RESTART_BOOTLOADER)` belongs to the core's
own USB stack, which an EspUsbDevice sketch does not run. It is also a no-op on
P4: the implementation exists only for S2/S3. Use `rebootToBootloader()`.

### 2.6 ROM DFU instead of the ROM serial loader (S2/S3)

```cpp
device.rebootToRomDfu();
```

This sets the ROM's persist flag (`chip_usb_set_persist_flags(USBDC_BOOT_DFU)`)
and the download-boot flag, then restarts, so the ROM comes up as a DFU device
on USB OTG. The host side is `dfu-util` or `idf.py dfu-flash` with an image
built by `idf.py dfu`, not a plain `.bin`.

- **ESP32-S3:** the call reads the `USB_PHY_SEL` eFuse and, when it is not
  burned, returns `false` with `ESP_ERR_NOT_SUPPORTED` **without restarting**.
  Restarting anyway would leave the ROM DFU stack on a controller with no pads
  and the connector dark. The library never burns the eFuse.
- **ESP32-S2:** the ROM's only USB is USB OTG, so the call proceeds. Not checked
  on hardware.
- **ESP32-P4:** returns `false` with `ESP_ERR_NOT_SUPPORTED` and restarts
  nothing.

If what you want is `dfu-util`, `EspUsbDeviceDfu` ([3.5](#35-the-dfu-function))
works the same on every chip.

### 2.7 What disables all of this

- **Secure Boot or flash encryption** disables the ROM's USB OTG stack (serial
  emulation and DFU). UART and USB Serial/JTAG remain.
- **Secure Download Mode** disables DFU and restricts the serial loader to a
  small command set.
- **`USB_PHY_SEL` is a one-way eFuse.** Burning it on S3 removes USB
  Serial/JTAG, including the ROM serial loader over USB.

If your product enables Secure Boot, route A over USB is not your recovery
path; a UART header is.

---

## 3. Route B: the sketch writes the next application

### 3.1 Partitions

Route B needs two application partitions and `otadata`. The Arduino default
scheme has them:

```
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x140000,
app1,     app,  ota_1,   0x150000,0x140000,
```

That is 1.25 MB per slot. `huge_app` has one app partition, so route B cannot
work under it. Check at startup:

```cpp
if (!EspUsbDeviceFirmwareUpdate::available()) {
  // No second app partition: say so now, not on the last block.
}
Serial.printf("%s, %u bytes\n", EspUsbDeviceFirmwareUpdate::targetLabel(),
              unsigned(EspUsbDeviceFirmwareUpdate::capacity()));
```

### 3.2 The write side is always the same

Whatever Function carried the bytes, the flash side is
`EspUsbDeviceFirmwareUpdate`. It is not a USB Function and registers nothing
with the device.

```cpp
EspUsbDeviceFirmwareUpdate update;

update.begin(imageSize);              // or begin() when the size is unknown
update.write(chunk, chunkLength);     // repeatedly, in order
if (update.end()) {                   // verifies, then switches the boot partition
  esp_restart();
} else {
  Serial.println(update.lastErrorName());
}
```

What it does that a hand-written `esp_partition_write()` loop tends to miss:

- `end()` checks the image header, length and checksum before switching the
  boot partition, so a truncated or corrupt upload fails there, not at the next
  boot. The running firmware is untouched until the restart.
- With a size given to `begin()`, `write()` refuses the first byte past it
  instead of failing at the end of a long upload. It also refuses anything past
  the partition.
- The partition is erased as the write advances, never all at once. Erasing a
  1.25 MB partition takes seconds, which is too long for a USB callback.
- `beginRandomAccess(imageSize)` + `writeAt(offset, ...)` accept writes in any
  order. The image's length is erased up front, because flash cannot be erased
  around an out-of-order write. UF2 uses this ([3.6](#36-the-firmware-drive)).
- `abort()` gives up and leaves the boot partition alone.

**Rollback.** `end()` makes the new image the one that boots, and nothing in the
library undoes that by itself. To survive a bad image, build the bootloader with
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` and call
`EspUsbDeviceFirmwareUpdate::markValid()` once the new firmware has proved
itself, for example after `device.ready()` first becomes `true`, not in
`setup()`. A stock Arduino build has no rollback; there `markValid()` returns
`true` and does nothing. `rollback()` returns to the previous image and
restarts; `cancelPendingBoot()` undoes a successful `end()` before the restart.
`markValid()` is not an authenticity check: anyone who can reach the update
route can install an image unless the sketch checks one.

### 3.3 Transports

| Transport | Host side | Endpoints (non-control IN) | Notes | Example |
| --- | --- | --- | --- | --- |
| **DFU** (`EspUsbDeviceDfu`) | `dfu-util` | **0** (EP0 only) | Standard protocol and tool. The library implements the whole path | [`FirmwareDFU`](../examples/FirmwareDFU/) |
| **MSC** (`EspUsbDeviceMscFirmwareDisk`) | file manager (drag and drop) | 1 | Best UX; behaviour depends most on the host | [`FirmwareMSC`](../examples/FirmwareMSC/) |
| **CDC ACM** (`EspUsbDeviceCdcSerial`) | any serial tool, a Python script | 2 | Simplest to write. Send a length, then the bytes | [`FirmwareCDC`](../examples/FirmwareCDC/) |
| **Vendor / WebUSB** (`EspUsbDeviceVendor`) | PyUSB, libusb, a browser page | 1 | Control requests as the command channel, bulk OUT for data. Fastest at HS | [`FirmwareVendor`](../examples/FirmwareVendor/) |
| **CDC-NCM + HTTP** (`EspUsbDeviceNet`) | a browser | 2 | The board is a USB network adapter; `HTTPUpdateServer` serves `/update` | [`FirmwareHTTP`](../examples/FirmwareHTTP/) |

The CDC and Vendor examples come with host scripts (`firmware_cdc.py`,
`firmware_vendor.py`). In v3 each transport's buffers are set with its
`configure()`; for example `FirmwareCDC` uses an 8 KB RX ring and
`FirmwareVendor` a 16 KB one, so the host can keep sending while a sector is
erased.

The NCM route is the only one where the host needs nothing but a browser. In
v3 the DHCP server is off by default, so the example calls
`net.dhcpServer(true)` before `net.beginNetwork()`; the page is then at
`http://192.168.7.1/update`. `HTTPUpdateServer` writes through Arduino's
`Update` library into the same OTA partition. If your users are not
developers, this route needs no host software; if they are, DFU is one command
and costs no endpoints.

### 3.4 Where the bytes must not be written

Flash erase takes milliseconds per 4 KB sector, and every class callback runs
on the USB worker, at the highest FreeRTOS priority by default. Calling
`update.write()` directly from `onRx()` stalls the whole device for that time,
including the other Functions of a composite device. For bulk transfers the
host only sees NAKs and slows down, but on an Audio or HID composite it is
audible or visible. The pattern that holds up:

```
USB callback  ->  data stays in the Function's RX ring (or your queue)  ->  return
sketch task   ->  read(), update.write(), repeat
```

With a CDC or Vendor stream, simply reading the ring from `loop()` is enough:
when the ring is full the library stops accepting bulk OUT, which is USB
flow control, not data loss.

Two transports write inside the callback on purpose:

- **MSC** must report the result of each WRITE(10) synchronously, so the flash
  write happens in the callback. Each callback carries up to the MSC
  `transferBytes`; `FirmwareMSC` uses 4096, one flash sector, which keeps the
  stall to one erase plus one write per callback (4096 is a multiple of 512, so
  it is also valid at HS).
- **DFU** has `bwPollTimeout` for exactly this: the device tells the host how
  long to wait before asking again ([3.5](#35-the-dfu-function)).

### 3.5 The DFU function

```cpp
EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);      // any other Functions
EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Download, "Firmware");

void setup() {
  EspUsbDfuConfig config;
  config.transferBytes = 4096;                 // wTransferSize; default 1024
  if (dfu.configure(config) != ESP_OK) return;
  dfu.onProgress([](size_t written) { /* bytes accepted so far */ });
  dfu.onError([](EspUsbDfuStatus status) { /* why it failed */ });
  device.begin();
}
```

```sh
dfu-util -D firmware.bin
```

**It costs no endpoints.** DFU is an interface descriptor, a functional
descriptor and control requests, all on EP0. On S3, where the IN endpoint
budget is what limits composite devices, it is the one Function that can always
be added.

Two modes (USB DFU 1.1):

| Mode | Answers | Default action |
| --- | --- | --- |
| `Download` (default) | the whole download | writes the image with `EspUsbDeviceFirmwareUpdate`, verifies it, restarts into it |
| `Runtime` | `DFU_DETACH` | `rebootToBootloader()` about 100 ms after the request completes (route A) |

Settings (`EspUsbDfuConfig`, set with `configure()` while stopped):

| Field | Default | Meaning |
| --- | --- | --- |
| `transferBytes` | 1024 | largest block (one `DFU_DNLOAD`), 64..65535. Declared as `wTransferSize` |
| `transferBuffer` | `nullptr` | your own block buffer (Download only), at least `transferBytes`. CPU copy, no DMA alignment |
| `detachTimeoutMs` | 1000 | `wDetachTimeOut`; how long Runtime stays in appDETACH at most |
| `downloadPollMs` | 1 | `bwPollTimeout` returned while a block is being written |
| `manifestPollMs` | 250 | `bwPollTimeout` returned while the image is verified and activated |
| `manifestationTolerant` | `false` | `false`: after manifest the device waits for a USB reset (it restarts). `true`: it returns to dfuIDLE |

Things to know about Download mode:

- **The image replaces the running sketch.** Send a build that also has a DFU
  interface, or the next update has to go through route A.
- **Failures are DFU status codes, not stalls.** A file that is not an ESP
  application is refused on its first block with `EspUsbDfuStatus::Write`
  (`errWRITE`, 3); an image that fails verification ends with
  `EspUsbDfuStatus::Verify` (`errVERIFY`, 7). Neither moves the boot partition.
  `CLRSTATUS`, which `dfu-util` sends by itself, returns the device to idle.
  `onComplete()` returning `false` refuses a verified image and puts the boot
  partition back.
- **When the block is written.** A block is stored after its data and status
  stages and the following `GETSTATUS` have completed. The flash write runs on
  the USB worker, and `bwPollTimeout` (`downloadPollMs`) tells the host to wait.
- **Restart.** `restartWhenComplete(true)` (default) restarts about 500 ms after
  a successful manifest. With `false`, the sketch restarts when it chooses.
- **Your own storage.** `setBackend()` with `download` and `manifest` callbacks
  replaces the built-in OTA writer (external flash, another processor). Then
  verification, activation and restart are the backend's job, and
  `onComplete()`/`restartWhenComplete()` do not apply.
- Several DFU interfaces can coexist, but only one of them can use the built-in
  OTA writer at a time.
- All setters return `esp_err_t` and work only while stopped.

### 3.6 The firmware drive

`EspUsbDeviceMscFirmwareDisk` presents a small FAT volume **whose data region is
the OTA partition**. The host copies a `.bin` onto the drive; the device writes
it to flash as sectors arrive, verifies it, and restarts into it.

```cpp
EspUsbDevice device;
EspUsbDeviceMsc msc(device);
static uint8_t diskStorage[16 * 1024];
EspUsbDeviceMscFirmwareDisk disk(diskStorage, sizeof(diskStorage));

void setup() {
  if (!disk.begin("ESPUSB")) return;          // no OTA partition, or storage too small
  disk.addTextFile("README.TXT", "Copy a firmware .bin here.\r\n");
  disk.onError([](esp_err_t error) { /* ... */ });

  EspUsbMscConfig mscConfig;
  mscConfig.transferBytes = 4096;             // one flash sector; valid at FS and HS
  if (msc.configure(mscConfig) != ESP_OK) return;
  if (!disk.attach(msc)) return;              // before device.begin()
  device.begin();
}
```

**The image is never in RAM.** `diskStorage` holds the boot sector, the FATs,
the root directory and a scratch area: a few kilobytes. Every sector past that
is the partition, read with `esp_partition_read()` and written through
`EspUsbDeviceFirmwareUpdate`. That is how a board with 320 KB of RAM accepts a
1.25 MB image.

`attach()` sets the medium present and writable, unlike the RAM-disk helpers.
Call `begin()` and `addTextFile()` before `attach()`, and all of them before
`device.begin()`.

What the class decides, and why:

- **Cluster size.** FAT12 addresses 4084 clusters, so `begin()` picks the
  smallest cluster from 4 KB up that covers the partition. 4 KB is also the
  flash sector size, so a cluster boundary is an erase boundary.
- **What counts as firmware.** A write into the firmware region whose first
  byte is the ESP image magic `0xE9` starts an update. Anything else there is
  dropped: a file copied by mistake, or host metadata that spilled out of the
  scratch area.
- **When it is done.** Whichever comes first: the file's directory entry gives
  its length and the byte count reaches it, or the host ejects the drive.
- **A raw `.bin` must arrive in ascending order.** A raw image says nothing about
  where its bytes belong. A write that jumps back or leaves a hole is refused,
  `onError()` receives `ESP_ERR_INVALID_STATE`, and the update is abandoned. It is
  never left half-applied and looking finished.

**A `.uf2` removes that restriction.** Each 512-byte UF2 block carries its own
target address, block index and total count. The drive accepts either format
and chooses from the first bytes that arrive.

| | raw `.bin` | `.uf2` |
| --- | --- | --- |
| Write order | ascending only | any |
| Host metadata | ignored by the `0xE9` check | rejected by the block magic |
| Length | from the FAT directory entry | from the block header |
| Completion | byte count reaches the length, or eject | every block index seen |
| A block written twice | indistinguishable from progress | counted once |
| Image for another chip | accepted, fails verification later | refused on the first block |

`EspUsbDeviceMscFirmwareDisk::uf2FamilyId()` returns the family ID this build
expects; pass it to `uf2conv.py --family <id> --base 0x0`. Arduino does not
produce a `.uf2` itself, so that conversion is the cost.

**Size the scratch area deliberately.** It absorbs `System Volume Information`,
`.fseventsd` and `.Spotlight-V100`; a host that fills it starts allocating
clusters inside the firmware region. 16 KB of storage is comfortable; 8 KB is
the minimum.

**Drive or DFU.** The drive has the better UX; DFU gives stronger guarantees
for less: no endpoints, where the drive costs a bulk pair. With a `.uf2` the
two are close on correctness; with a raw `.bin` the drive trusts the host to
write in order. Ship DFU when the person updating has a terminal, the drive
when they do not, and both when you do not know.

### 3.7 Windows binds the driver itself

No in-box Windows driver claims DFU (interface class 0xFE). The library
therefore emits a Microsoft OS 2.0 descriptor set that asks for WinUSB, for
every DFU and Vendor interface, without configuration.

- **One such interface: flat.** The compatible ID sits directly under the set
  header. Function subsets are resolved only through usbccgp.sys, which
  Windows loads only for composite devices.
- **More than one: one function subset per interface.**
- The DFU interface gets the compatible ID and no `DeviceInterfaceGUIDs`;
  libusb, and so `dfu-util`, finds a WinUSB device without one.
- `bcdUSB` becomes 0x0201 whenever a BOS descriptor exists, the level at which
  a host asks for one.

These rules were measured on Windows 11 with v2, which builds the same
descriptor set. v3 has not been run on Windows. Linux and macOS bind by class or
let libusb claim the interface, so they do not need the set.

---

## 4. Route comparison

| Route | Chips | Boot mode | Host tool | Can brick | API |
| --- | --- | --- | --- | --- | --- |
| ROM serial loader over USB | S2 (OTG), S3/P4 (USB Serial/JTAG) | yes | `esptool` | no | `rebootToBootloader()` ([2.4](#24-entering-boot-mode-from-the-sketch)) |
| ROM DFU over USB OTG | S2; S3 only with `USB_PHY_SEL` burned; not P4 | yes | `dfu-util` | no | `rebootToRomDfu()` ([2.6](#26-rom-dfu-instead-of-the-rom-serial-loader-s2s3)) |
| DFU in the application | all | no | `dfu-util` | yes, without rollback | `EspUsbDeviceDfu` ([3.5](#35-the-dfu-function)) |
| Firmware drive (`.bin` / `.uf2`) | all | no | file manager | yes, without rollback | `EspUsbDeviceMscFirmwareDisk` ([3.6](#36-the-firmware-drive)) |
| CDC ACM | all | no | serial tool | yes, without rollback | `EspUsbDeviceCdcSerial` + `EspUsbDeviceFirmwareUpdate` |
| Vendor / WebUSB | all | no | PyUSB, browser | yes, without rollback | `EspUsbDeviceVendor` + `EspUsbDeviceFirmwareUpdate` |
| CDC-NCM + HTTP | all | no | browser | yes, without rollback | `EspUsbDeviceNet` + `HTTPUpdateServer` |

"Can brick" means the application route can install an image that does not
work; route A then remains, because the ROM loader cannot be overwritten.

---

## 5. Library feature or sketch code

The dividing line: **the library owns what has to be right in descriptors,
protocol state or chip registers; the sketch owns policy.**

**In the library**

- The DFU class: descriptors, the DFU 1.1 state machine and its control
  requests. A sketch should not hand-roll them.
- `rebootToBootloader()`: per-chip register knowledge that would otherwise be
  copied wrongly from an S3 tutorial onto a P4.
- `EspUsbDeviceFirmwareUpdate`: the size limit, sequential erase and
  verify-before-switch rules shared by every route.
- The FAT layer of the firmware drive: getting the geometry wrong is invisible
  until a particular host mounts it.

**In the sketch**

- When to enter boot mode: 1200-baud touch, a button, a vendor command.
  [`FirmwareBootMode`](../examples/FirmwareBootMode/) shows three.
- The host side: the Python uploader, the WebUSB page, the HTML form.
- The wire protocol of a vendor-specific updater (framing, checksums,
  acknowledgement). This is why DFU is in the library and a vendor protocol is
  not: DFU is an existing standard, so implementing it commits the library to
  nothing of its own.
- Rollback policy, authentication of images, progress display.

---

## 6. What has been checked on hardware

| Item | v3 status |
| --- | --- |
| DFU protocol (blocks of 64 to 65535 bytes, GETSTATUS and block order, CLRSTATUS, ABORT, manifestation, several interfaces) with a RAM backend | Checked S3 ↔ S3, on the P4 FS loopback, and on P4 ↔ P4X HS |
| DFU `DFU_DETACH` timing and refusal | Checked, P4X → Linux PC (HS) with pyusb |
| MSC BOT and media change at the wire level | Checked, P4X → Linux PC (HS) with pyusb (no OS mount) |
| DFU Download with the built-in OTA writer, ending in a restart into the new image | Not checked with v3 |
| Firmware drive mounted by an OS, raw `.bin` and `.uf2` | Not checked with v3 |
| `rebootToBootloader()`, `rebootToRomDfu()` | Not checked with v3 |
| `FirmwareCDC`, `FirmwareVendor`, `FirmwareHTTP` end to end | Not checked with v3 |
| `dfu-util`, Windows, macOS | Not checked with v3 |
| ESP32-S2 | Compile only |

`EspUsbDeviceFirmwareUpdate`, the firmware drive's FAT and UF2 logic and
`rebootToBootloader()` are the same code as in v2, where they were checked on
hardware: an ESP32-P4 DFU download at HS (385 KB in 2.7 s, then restart into
the image), UF2 blocks written in reverse order, and `rebootToBootloader()` on
ESP32-S3 and ESP32-P4. In v3 the USB transport underneath (MSC, DFU, the
controller driver) is new.

---

## 7. Out of scope: UF2 as a bootloader

[TinyUF2](https://github.com/adafruit/tinyuf2) replaces the second-stage
bootloader with one that presents a UF2 drive, and Espressif's
[`esp_tinyuf2`](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_device/esp_tinyuf2.html)
packages it for ESP-IDF together with an application-side variant. The
application-side variant is [3.6](#36-the-firmware-drive). The bootloader
variant is out of scope: it replaces the bootloader, it is an ESP-IDF component
rather than an Arduino library, and its advantage (working when the application
is broken) is what route A already provides from mask ROM.

---

## Related documents

- [USB Device Development Guide](usb-device-guide.md)
- [USB Device Development Guide (Advanced)](usb-device-advanced.md)
- [Troubleshooting](troubleshooting.md)
- [DFU class](classes/dfu.md), [MSC class](classes/msc.md)
- Examples: [`FirmwareDFU`](../examples/FirmwareDFU/), [`FirmwareMSC`](../examples/FirmwareMSC/),
  [`FirmwareCDC`](../examples/FirmwareCDC/), [`FirmwareVendor`](../examples/FirmwareVendor/),
  [`FirmwareHTTP`](../examples/FirmwareHTTP/), [`FirmwareBootMode`](../examples/FirmwareBootMode/)
- [USB DFU 1.1](https://www.usb.org/sites/default/files/DFU_1.1.pdf)
- [ESP-IDF: Device Firmware Upgrade via USB](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/dfu.html)
- [esptool: Boot Mode Selection (ESP32-S3)](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html) / [(ESP32-P4)](https://docs.espressif.com/projects/esptool/en/latest/esp32p4/advanced-topics/boot-mode-selection.html)
- [ESP-IoT-Solution: USB-OTG peripheral introduction](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_overview/usb_otg.html) (the P4 v3.1 ROM DFU defect)
