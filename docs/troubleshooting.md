# Troubleshooting

> 日本語版: [troubleshooting.ja.md](troubleshooting.ja.md)

Symptom-first fixes for EspUsbDevice v3, collected in one linkable place.
Fundamentals are in the [USB Device Development Guide](usb-device-guide.md),
library internals in the [advanced guide](usb-device-advanced.md), and each
class has its own page under [classes/](classes/).

Three sketches answer most questions before you look at the host:

| Sketch | What it tells you |
| --- | --- |
| [`EspUsbDeviceBringUpCheck`](../examples/Info/EspUsbDeviceBringUpCheck/) | whether `begin()` succeeded, whether a host configured the device, and at which speed |
| [`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/) | every descriptor the library built and the endpoint budget. No host needed (`DUMP_DESCRIPTORS_ONLY 1`) |
| [`EspUsbDeviceConsole`](../examples/Info/EspUsbDeviceConsole/) | live state (`ready`, `suspended`, keyboard LEDs, HID protocol) and manual HID/Vendor sends |

## Contents

1. [begin() fails](#1-begin-fails)
2. [begin() succeeds but the host sees nothing](#2-begin-succeeds-but-the-host-sees-nothing)
3. [Enumeration starts but ends badly](#3-enumeration-starts-but-ends-badly)
4. [Build and upload](#4-build-and-upload)
5. [Host-OS specifics](#5-host-os-specifics)
6. [Class-specific issues](#6-class-specific-issues)
7. [Stability](#7-stability)
8. [Per-chip notes](#8-per-chip-notes)
9. [Still stuck](#9-still-stuck)

## 1. begin() fails

### 1.1 Read the reason first

Every failure path records an `esp_err_t`. Three places report it:

| Where | What it returns |
| --- | --- |
| `Function.configure(config)` | `esp_err_t` directly. `ESP_OK` means the settings were stored |
| `device.begin(config)` | `bool`. The reason is in `device.lastError()` |
| `Function.startError()` | why that Function refused to start in the last `begin()` |

```cpp
if (vendor.configure(vendorConfig) != ESP_OK) { /* the settings themselves are wrong */ }
if (!device.begin(config)) {
  Serial.printf("begin: %s, vendor: %s\n",
                esp_err_to_name(device.lastError()),
                esp_err_to_name(vendor.startError()));
}
```

- `device.lastErrorName()` returns the `esp_err_to_name()` name of
  `device.lastError()`, for every code (`ESP_ERR_TIMEOUT` from a HID send
  included). A Function's `startError()` is a plain `esp_err_t`; print it with
  `esp_err_to_name()`.
- When a Function fails, `device.lastError()` is that Function's
  `startError()`. Every Function that had already started is ended again, and
  nothing is left allocated.
- A failed `begin()` leaves the device stopped. Fix the setting and call
  `begin()` again; no reset is needed.

### 1.2 What each code means

The checks run in this order: device settings, descriptors and the endpoint
budget, each Function's resources, then the PHY, controller and USB worker.
Everything before the PHY step happens without touching the bus, so the host
sees nothing.

#### `ESP_ERR_INVALID_SIZE` (0x104): something does not fit

| Cause | Fix |
| --- | --- |
| More non-control IN endpoints than the controller has: 4 on ESP32-S2/S3 and on the ESP32-P4 FS controller, 7 on the P4 HS controller. Endpoint numbers are limited too (1-6 on S2/S3) | Count with DescriptorDump (no host needed). See the [budget table](#endpoint-budget) below. Drop a Function, or use the P4 HS controller |
| `bulkInBuffering = EspUsbBulkInBuffering::Double`, and the TX FIFO cannot hold two packets for every bulk IN endpoint | Use `Auto` (doubles only when all fit) or `Single` |
| The IN endpoints of the whole configuration do not fit the controller's TX FIFO (256 words on S2/S3/P4 FS, 1024 words on P4 HS, shared with EP0 and the RX FIFO). On the P4 HS controller five default Vendor Functions (512-byte bulk IN each) fit and a sixth does not | Remove an IN endpoint or lower packet sizes (Audio/Video isochronous packets, `EspUsbDeviceHidCustom` `inputReportSize`) |
| MSC `transferBytes` is not a multiple of the bulk MPS (64 at FS, 512 at HS) | Use a multiple of 512, which is valid at both speeds |
| CCID `messageBytes` set explicitly below the Bulk-OUT MPS (512 at HS) | Leave it at 0 (automatic: `max(271, MPS)`) or use 512 or more |
| Vendor full-speed endpoint size is not 8, 16, 32 or 64 (values above 64 select 64; this is the Function's `startError()`) | Use one of those (USB 2.0 §5.8.3) |
| HID: the merged HID interrupt packet is above 64 bytes at FS or 1024 at HS, report IDs ran out, the merged report descriptor is over 65535 bytes, or `EspUsbDeviceHidVendor` `reportSize` is above `maxReportSize()` | Fewer or smaller HID Functions |
| A descriptor string is over 126 UTF-16 units, or the WebUSB URL is over 252 bytes after `http(s)://` | Shorten it |
| Audio: an external ring (`playbackBuffer`/`captureBuffer`) is smaller than frames x the largest PCM frame | Pass the real `sizeof()` in `playbackBufferBytes`/`captureBufferBytes`, or let the library allocate |
| `taskStackBytes` below the FreeRTOS minimum | Raise it (default 4096) |
| More than 251 Functions on one device | - |

`configure()` itself returns `ESP_ERR_INVALID_SIZE` for values outside a
Function's range; see [1.3](#13-configure-rejects-the-settings).

#### `ESP_ERR_INVALID_ARG` (0x102): settings that contradict each other

| Cause | Fix |
| --- | --- |
| `config.deviceInterfaceGuid` is not `{8-4-4-4-12}` with braces (38 characters) | Fix the GUID |
| `manufacturer`/`product`/`serialNumber`, a Function name or the WebUSB URL is not valid UTF-8; the URL is empty after `http(s)://` | Fix the string |
| `msOs20Layout = ESP_USB_DEVICE_MS_OS_20_FLAT` with more than one WinUSB interface (Vendor and DFU each count) | Use `ESP_USB_DEVICE_MS_OS_20_AUTO` |
| `taskPriority` or `taskCoreId` out of range, or an unknown `startMode` | -1 (default) or a valid value |
| Bulk Function in `EspUsbBulkRxMode::Callback` without `onRxData()`, or `Borrowed` without `onDirectRxComplete()`; MIDI in `EspUsbMidiRxMode::Callback` without `onPacket()` | Register the callback before `begin()` |
| MIDI capacities or `transferBytes` not a multiple of 4 bytes | Count in packets |
| MSC has no storage: `attach()` was not called (no geometry or no `onRead`) | Call `disk.attach(msc)` or set `onRead` + `msc.begin(blockCount, blockSize)` before `device.begin()` |
| Audio: no stream, a stream with no format, channel counts that differ within a direction, or sample-rate sets that differ across the shared clock | Make the formats consistent (see [Audio](classes/audio.md)) |
| `EspUsbDeviceHidCustom` with no report descriptor, or a descriptor the parser rejects | Check the descriptor bytes |
| Video frame size, rate or maximum frame size out of range | `setFrameSize()` up to 4096x4096 |
| External buffers overlap, or a buffer that must be DMA-capable is not (Video `payloadBuffer`, NCM buffers) | Give each its own internal-RAM region, or pass `nullptr` |

#### `ESP_ERR_INVALID_STATE` (0x103): wrong order or already in use

| Cause | Fix |
| --- | --- |
| `begin()` called while the device is already started | `end()` first |
| Another `EspUsbDevice` object is active | One device per controller |
| A Function was constructed (registered) while the device was running | Declare Functions before `begin()`, or `end()` → construct → `begin()` |
| CCID with `removable = false` and no card inserted | `insertCard()` before `begin()` |
| The PHY or controller is already taken (for example by the core's own USB stack) | See [4. Build and upload](#4-build-and-upload) |

A Function's own `begin()`, `afterDeviceStarted()` and `prepareToStop()` are
protected; only `device.begin()` and `device.end()` run them. Calling
`console.begin()` or `keyboard.begin()` from a sketch is a compile error.
`msc.begin(blockCount, blockSize)`, which only sets the geometry, and the
helpers' own `begin()` (for example `EspUsbDeviceMscSdCard::begin()`) stay
public.

#### `ESP_ERR_NO_MEM` (0x101)

- A Function's buffers, the descriptor storage or the USB worker could not be
  allocated. Everything acquired so far is released again. Reduce capacities
  (`rxCapacity`, `txCapacity`, ring frames, `messageBytes`, NTB sizes).
- A TX FIFO that is too small is not this code; it is `ESP_ERR_INVALID_SIZE`.

#### `ESP_ERR_NOT_SUPPORTED` (0x106)

- The target has no USB OTG controller. Only ESP32-S2, ESP32-S3 and ESP32-P4
  are supported; ESP32, C3, C6, H2 and S31 are not.
- `config.controller = EspUsbController::HighSpeed` on a chip without an HS
  controller (S2/S3).

#### `ESP_FAIL` and other codes

- `ESP_FAIL`: the controller failed to initialise, or a Function failed after
  the controller had started. Raise Core Debug Level to Verbose and read the
  ESP-IDF log.
- Codes from ESP-IDF's PHY driver (`usb_new_phy()`) are passed through as is.

### 1.3 configure() rejects the settings

`configure()` validates only the settings it is given. It does not know the
controller speed yet, so speed-dependent checks (MPS multiples) happen in
`begin()`.

| Function | `ESP_ERR_INVALID_SIZE` | `ESP_ERR_INVALID_ARG` |
| --- | --- | --- |
| CDC / Vendor (`EspUsbBulkStreamConfig`) | - | `rxCapacity` must be non-zero in `Stream` mode and 0 in the other modes; `transferBytes` 0 with a TX ring; unknown mode; external buffers that overlap or wrap |
| MIDI (`EspUsbMidiConfig`) | `transferPackets` above 16383 | cables outside 1..16; `rxPackets`/`rxBuffer` with a non-`Queue` mode |
| MSC (`EspUsbMscConfig`) | `transferBytes` 0, above 65535, or not a multiple of 64 | `transferBuffer` not 64-byte aligned or not internal DMA-capable RAM (PSRAM is refused) |
| CCID (`EspUsbCcidConfig`) | `messageBytes` non-zero and outside 64..65535 | - |
| NCM (`EspUsbNetConfig`) | NTB sizes outside 2048..65535, `maxFrameBytes + 28` larger than an NTB, `maxDatagrams` 0 or too large | an external buffer without its count or byte size |
| Audio (`EspUsbAudioConfig`) | sizes that overflow | a buffer pointer without its byte size (or the reverse); unknown overflow policy |
| Video (`EspUsbVideoConfig`) | `payloadBytes` outside 3..65535 (3..1024 for isochronous); `isochronousPacketBytes` outside 3..1024 | unknown transport |
| DFU (`EspUsbDfuConfig`) | `transferBytes` outside 64..65535; poll times above 0xFFFFFF | `transferBuffer` address overflow |

All of them return `ESP_ERR_INVALID_STATE` while the device is started.

### 1.4 Callbacks and setters return ESP_ERR_INVALID_STATE

Callback registration (`onRx`, `onLineCoding`, `onOutputReport`, `onPacket`,
`onRead`, `onProgress`, ...) and settings such as `vendorID()`, `setName()` or
`macAddress()` work **only while the device is stopped**. After
`device.begin()` they return `ESP_ERR_INVALID_STATE` and change nothing. A
sketch that ignores the return value simply never sees the callback fire.
Register everything before `begin()`, or `end()` → register → `begin()`.

## 2. begin() succeeds but the host sees nothing

`begin()` returning `true` means the configuration was valid and resources were
taken. It does not mean a host enumerated the device. Check in this order:

1. **Wrong connector.** Many boards also have a USB-UART or USB Serial/JTAG
   connector that is not wired to the OTG controller. Check the schematic, not
   the silkscreen.
2. **Charge-only cable.** Try a known-good data cable.
3. **USB Mode on ESP32-S2/S3.** Build with *USB Mode: USB-OTG (TinyUSB)*
   (`USBMode=default` in an FQBN). On ESP32-S3 the IDE default is "Hardware CDC
   and JTAG", which hands the shared PHY to USB Serial/JTAG. See
   [4. Build and upload](#4-build-and-upload).
4. **ESP32-P4: the controller does not match the connector.** `Auto` selects
   the HS controller. If your cable is on the FS port, set
   `config.controller = EspUsbController::FullSpeed`; for the HS connector use
   `Auto` or `HighSpeed`.
5. **No Function registered, or `DescriptorsOnly`.** With no Function, or with
   `config.startMode = EspUsbDeviceStartMode::DescriptorsOnly`, `begin()`
   succeeds and `started()` is `true`, but the controller never starts and
   `ready()` stays `false`.
6. **`config.vbusSensing = true` on a board without VBUS sensing.** Leave it
   `false` unless the board routes VBUS to the controller.
7. Then run BringUpCheck and read the [host's own log](#5-host-os-specifics).
   Past this point the remaining causes are visible only from the host side.

## 3. Enumeration starts but ends badly

### Windows shows "Unknown USB Device (Device Descriptor Request Failed)"

The device failed at the descriptor stage.

1. Open [USB Device Tree Viewer](https://www.uwe-sieber.de/usbtreeview_e.html)
   and read the raw descriptor bytes and the exact Windows error.
2. Compare those bytes with DescriptorDump output. On Linux, `lsusb -v` shows
   the same descriptors as the host parsed them.

### The host still shows the old device after a change

**Windows caches descriptors and properties per VID/PID/serial.** This is the
most common cause of "my change had no effect".

1. Change the PID while descriptors are changing during development.
2. Or change `config.serialNumber`.
3. Or uninstall the device in Device Manager and replug.

The Microsoft OS 2.0 set is re-read only when `wVendorRevision` changes. The
default `msOs20VendorRevision = 0` derives it from the set's bytes, so it
changes automatically whenever the set does.

### `SET_CONFIGURATION` completes but no driver binds

The descriptors are structurally valid, but the class/subclass/protocol
combination is not one the host OS maps to a driver. Compare with a commercial
device of the same class: interface order, IAD, `bInterfaceProtocol` and
endpoint intervals are the usual differences.

### A multi-function device shows as one function

Windows binds functions separately only when usbccgp.sys is the parent. That
needs `bDeviceClass/SubClass/Protocol = 0xEF/0x02/0x01`, which the library sets
automatically whenever the configuration contains an Interface Association
Descriptor (CDC ACM, NCM, Audio and Video emit one). If you still see one function, a
cached binding from an earlier firmware is the likely cause; see the cache entry
above. `config.msOs20CcgpDevice = true` asks Windows to treat even a
single-interface device as composite.

## 4. Build and upload

1. **Arduino-ESP32 3.3.12** is the reference core. The examples' `sketch.yaml`
   files pin it.
2. **USB Mode.** On ESP32-S2/S3 build with *USB-OTG (TinyUSB)*
   (`USBMode=default`). On ESP32-P4 the HS controller has its own PHY, and
   *Hardware CDC and JTAG* (`USBMode=hwcdc`, `CDCOnBoot=cdc`) has been used on
   P4X to keep logs on USB Serial/JTAG while EspUsbDevice runs on the HS port.
   With the P4 FS controller, use `USBMode=default`; the FS PHY is shared with
   USB Serial/JTAG (the hwcdc combination is untested there).
3. **Do not use the core's USB device stack** in the same sketch: no
   `USB.begin()`, `USBHIDKeyboard`, `USBCDC`, `USBMSC` and so on, and leave
   *USB CDC On Boot*, *USB MSC On Boot* and *USB DFU On Boot* disabled for the
   OTG controller. Both stacks drive the same controller. See
   [Migrating from the Arduino-ESP32 USB API](migrating-from-arduino-esp32-usb.md).
4. **`Serial` is not on the OTG port.** With the settings above it comes out on
   the UART (or on USB Serial/JTAG with hwcdc). On a board whose only connector
   is the OTG one, logs and the device share one plug; use a board with two
   connectors or a UART adapter while developing.
5. **`#error "Remove TinyUSB build flags..."`.** v3 has no build-time size
   flags. Every `CFG_TUD_*` buffer size and the v2 `ESP_USB_DEVICE_*` flags are
   a compile error; remove them from `build_opt.h` and set the sizes with each
   Function's `configure()`. `ESP_USB_DEVICE_CCID_BUFFER_SIZE` likewise points
   to `EspUsbCcidConfig::messageBytes`.
6. **`undefined reference to EspUsbDeviceVendor::...` while the headers were
   found.** If `sketch.yaml` points `libraries: - dir:` at a symlink,
   arduino-cli may find the headers through it and still not compile the
   `.cpp` files behind it. Use the real path or the Library Manager.

## 5. Host-OS specifics

v3 has been checked with ESP32 hosts (EspUsbHost) and with a Linux PC. Windows
and macOS have not been checked with v3; the Windows notes below come from v2,
whose Microsoft OS 2.0 layout v3 keeps unchanged.

### Linux

```sh
sudo dmesg -w          # start before plugging in; names the rejection reason
lsusb -v -d 303a:      # descriptors as the host parsed them (use your VID)
```

- `dmesg` states why a descriptor was rejected and which driver bound.
- libusb/PyUSB tools that fail with a permission error need a udev rule for
  your VID/PID.
- `usbmon` with Wireshark shows the control transfers when `dmesg` is not
  enough.

### Windows (not checked with v3)

- USB Device Tree Viewer shows raw descriptors plus the error. In Device
  Manager, "Device Descriptor Request Failed" is the descriptor stage; Code 10 /
  Code 43 is driver binding.
- Remember the [descriptor cache](#the-host-still-shows-the-old-device-after-a-change).
- **WinUSB binds without an INF.** The library emits a Microsoft OS 2.0
  descriptor set for every Vendor and DFU interface automatically.
  `config.webusbEnabled` only adds the WebUSB capability; it is not needed for
  WinUSB.
- **Code 28 (`CM_PROB_FAILED_INSTALL`) on a vendor interface, with nothing in
  `setupapi.dev.log`.** The compatible ID never reached the device node. A
  function subset is resolved only through usbccgp.sys, which Windows loads
  only for composite devices, so a single-interface device needs the compatible
  ID directly under the set header. The default `ESP_USB_DEVICE_MS_OS_20_AUTO`
  picks the right shape by interface count; check with
  `device.microsoftOs20UsesSubsets()`. To see what the host asked for, install
  `device.onAnyControlRequest()`; it also sees the requests the library answers
  itself.
- **Two identical COM ports.** Windows names a composite child from the
  function's `iFunction` string. Name each Function:
  `EspUsbDeviceCdcSerial serial(device, "Console");`.
- **COM port numbers move between USB ports.** Windows keys them on
  VID/PID/serial. Give each board a unique `config.serialNumber`.
- **The host application finds unrelated devices.** Give your product its own
  `config.deviceInterfaceGuid`.

### macOS (not checked with v3)

- System Information → USB for the tree; `ioreg -p IOUSB -l -w 0` for details.

## 6. Class-specific issues

### HID (all HID Functions)

- All HID Functions on one device merge into **one HID interface** with report
  IDs, using one IN endpoint.
- Sends (`sendReport()`, `move()`, `tapKey()`, ...) wait up to `timeoutMs`
  (100 ms by default) for the IN endpoint. `0` polls once. A busy endpoint
  gives `false` with `ESP_ERR_TIMEOUT`; an unconfigured device gives
  `ESP_ERR_INVALID_STATE`. Success means the report was copied and queued, not
  that the host read it.
- **Inside a USB callback, pass `timeoutMs = 0`.** A non-zero timeout on the
  USB worker fails at once with `ESP_ERR_INVALID_STATE`, because waiting there
  would stop the worker that has to free the endpoint.

### HID keyboard

- **Wrong characters arrive.** The host decodes key positions with its own
  layout. Match it with `keyboard.setLayout()`.
- **`write()` stops partway.** It returns `false` with
  `ESP_ERR_NOT_SUPPORTED` at the first character the layout cannot type, and
  does not send the rest. `pressKey()`, `tapKey()` and `releaseKey()` fail the
  same way.
- **The seventh key fails.** `pressUsage()`/`pressKey()` add to the held keys
  and modifiers. Without NKRO the report holds six non-modifier keys (HID 1.11
  Appendix B.1); a seventh returns `false` with `ESP_ERR_INVALID_SIZE` and
  changes nothing. Call `enableNkro()` before `begin()` to hold more. With NKRO
  a usage the bitmap cannot carry (above 0xDF and not a modifier) fails with
  `ESP_ERR_INVALID_ARG`.
- **A key held before a tap stays held.** `tapUsage()`/`tapKey()` release only
  the key and modifiers the tap added. Use `releaseUsage()`, `releaseKey()` or
  `releaseAll()` to let go of the others.
- **Does not work in BIOS/UEFI.** The host switched to boot protocol. NKRO
  folds down to 6 keys there. Watch `onProtocol()` or `protocol()`.
- **Lock LEDs.** `ledState()` is updated even without an `onOutputReport()`
  callback, and is cleared on bus reset.

### HID mouse

- `move(x, y)` keeps the buttons held by `press()`, so `press()` then `move()`
  drags. The fourth argument defaults to `EspUsbDeviceHidMouse::KeepButtons`
  (-1); a value 0-255 replaces the held buttons. Anything else fails with
  `ESP_ERR_INVALID_ARG`.

### HID consumer and system control

- A Consumer usage above 0x3FF (the report's Logical Maximum) returns `false`
  with `ESP_ERR_INVALID_ARG` and sends nothing.
- System Control accepts 0x81, 0x82, 0x83, or 0 to release. Other values fail
  the same way.

### CDC ACM

- **`print()` blocks for a second.** When the TX ring is full (the host is not
  reading, or no terminal is open), `write()`/`print()` wait up to the TX
  timeout (1000 ms by default). `setTxTimeoutMs(0)` makes them return a partial
  count at once.
- `connected()` is `true` only while the host has the port open with DTR set.
- The CDC port is a data port for your sketch. It is not `Serial`.

### Vendor bulk

- **The host read waits forever.** A stream write does not end with a short
  packet by default, and a write that is a multiple of the MPS gets no ZLP.
  Read fixed lengths on the host, or set
  `streamTermination = EspUsbStreamTermination::EndWithShortPacket`.
- `writeDirect()`/`readDirect()` buffers must be 64-byte aligned internal
  DMA-capable RAM. A PSRAM buffer is refused with `NotDmaCapable`. Rings set
  with `rxBuffer`/`txBuffer` may be in PSRAM.

### USB MIDI

- The **channel comes first and is 0-based** (`noteOn(channel, note, velocity,
  cable)`). The cable number is last and must be below `inCableCount()`.
- **A message is not sent.** A channel above 15, a data byte above 127 or a
  pitch bend above 16383 is not clamped: the call returns `false` with
  `ESP_ERR_INVALID_ARG` and sends nothing.
- Callback mode needs `onPacket()` registered before `begin()`.

### MSC

- **The drive never appears.** `EspUsbDeviceMscRamDisk` and
  `EspUsbDeviceMscFatRamDisk` start with the medium absent. Call
  `msc.mediaPresent(true)` before `device.begin()`.
  `EspUsbDeviceMscFirmwareDisk::attach()` sets the medium present and writable
  itself.
- Call `attach()` before `device.begin()`. Without it `begin()` fails with
  `ESP_ERR_INVALID_ARG`.
- **The first command after re-inserting fails.** `mediaPresent(true)` while the
  host is configured reports UNIT ATTENTION (sense 06/28/00) once, as SCSI
  requires. The next TEST UNIT READY succeeds.
- **Will not mount with media present.** Check the FAT image and block size
  against [`MSCFatRamDisk`](../examples/MSCFatRamDisk/), a known-good minimal
  image.
- **The host says the disk is write protected.** `msc.isWritable(false)`, or
  `EspUsbDeviceMscSdCard::readOnly(true)`, reports write protection: MODE SENSE
  sets WP, and a WRITE fails with DATA PROTECT (sense 07/27/00).
- **`EspUsbDeviceMscSdCard::attach()` returns `false`.** The card is not
  mounted, its sector size is not 512, a callback could not be registered, or
  the device is already running.

### USB Audio

- **`begin()` fails with `ESP_ERR_INVALID_ARG`.** The stream settings disagree;
  see the table in [1.2](#esp_err_invalid_arg-0x102-settings-that-contradict-each-other).
- **`begin()` fails with `ESP_ERR_INVALID_SIZE`.** The isochronous IN
  endpoints (capture, or UAC2 feedback) and the other IN endpoints do not fit
  the TX FIFO together.
- **Dropouts.** Read `playback.read()` / write `capture.write()` at a steady
  rate and watch `stats()`. When the playback ring overflows, the default
  `DropOldest` discards the oldest unread PCM.
- UAC1 is the default; select UAC2 in the `EspUsbAudioFunction` constructor.
  The UAC2 feedback endpoint uses 10.14 format in 3 bytes at FS and 16.16 in
  4 bytes at HS (USB 2.0 §5.12.4.2).

### USB Video

- Format, frame size, rate and maximum frame size are fixed at `begin()`;
  setters are refused afterwards.
- At FS an isochronous endpoint carries at most 1023 bytes per 1 ms frame
  (about 1 MB/s). Use MJPEG for anything larger than small test frames.

### CDC-NCM network

- **The host gets no address.** The DHCP server is off by default. Call
  `net.dhcpServer(true)` before `net.beginNetwork()`, and call `beginNetwork()`
  after `device.begin()`. The default address is `192.168.7.1/24`. The server
  advertises no gateway or DNS unless asked.
- Interoperation with the NCM drivers of PC operating systems has not been
  checked with v3.

### CCID

- `messageBytes = 0` (default) resolves to `max(271, Bulk-OUT MPS)`: 271 at FS,
  512 at HS. An explicit value below the MPS fails `begin()`.
- `removable = false` needs `insertCard()` before `begin()`.

### DFU

- `onDetach()` and `setDetachHandler()` are for Runtime mode only; in Download
  mode they return `ESP_ERR_INVALID_ARG`.
- Download failures are DFU status codes (`EspUsbDfuStatus`), reported through
  `onError()`. See [Firmware Update over USB](ota-over-usb.md).

## 7. Stability

- **`end()` returns `ESP_ERR_INVALID_STATE`.** It was called from a USB
  callback. Set a flag there and call `end()` from `loop()`.
- **`end()` returns `ESP_ERR_TIMEOUT`.** The USB worker did not finish its
  current callback within `config.stopTimeoutMs` (5000 ms by default). The stop
  stays requested and the resources are kept, because DMA may still touch the
  buffers. Call `end()` again once the callback returns. `stopTimeoutMs = 0`
  waits indefinitely. The destructor and a Function's `end()` always wait until
  the stop completes.
- **Changing settings at run time.** `end()` → `configure()` → `begin()`. The
  host sees an unplug and a new plug and must reopen the device. Descriptors
  cannot change while the device is connected.
- **State survives replugging.** Without VBUS sensing, an unplug is not seen
  until the next enumeration. Clear host-related state when `ready()` turns
  `true` again rather than when it turns `false`.
- **The bus is suspended.** `ready()` stays `true` while suspended; check
  `suspended()`. A suspended device does not lose its configuration.
  `remoteWakeup()` works only when `remoteWakeupSupported = true` and the host
  enabled it.
- **Heavy callbacks.** All class callbacks run on the USB worker, at the
  highest FreeRTOS priority by default. Long work there delays every other
  Function. Hand it to your own task. Raise `taskStackBytes` if callbacks print
  a lot.
- **The log dies right after `begin()`.** On a one-connector board the log port
  and the device port are the same plug.

## 8. Per-chip notes

| Chip | USB controllers | Notes |
| --- | --- | --- |
| ESP32-S2 | FS | Same endpoint budget as the S3. **Compile-only so far**; not run on hardware with v3 |
| ESP32-S3 | FS | Main hardware-tested target. One PHY on GPIO19/20, shared with USB Serial/JTAG |
| ESP32-P4 (v1.x) | HS (480 Mbps, own UTMI PHY) and FS | `Auto` picks HS. Build with `ChipVariant=prev3` |
| ESP32-P4X (v3.x) | HS and FS | Same USB budget as v1.x. Build with `ChipVariant=postv3`; the SDK supports v3.1 and later |
| ESP32-S31 | - | Not supported yet |
| ESP32, C3, C6, H2, ... | none | No USB OTG controller |

### Endpoint budget

| Controller | Endpoint numbers | Non-control IN endpoints | TX/RX FIFO |
| --- | --- | --- | --- |
| S2 / S3 / P4 FS | 1-6 | 4 | 256 words |
| P4 HS | 1-15 | 7 | 1024 words |

Non-control IN endpoints used per Function:

| Function | IN | Function | IN |
| --- | --- | --- | --- |
| HID (all HID Functions together) | 1 | NCM | 2 |
| CDC ACM | 2 | CCID (`removable = true`) | 2 |
| Vendor, MIDI, MSC, Video | 1 each | CCID (`removable = false`) | 1 |
| Audio capture | 1 | DFU | 0 |
| Audio UAC2 playback (feedback) | 1 | Audio playback (OUT) | 0 |

On S3, HID + CDC + MSC (1 + 2 + 1) already uses all four. DFU uses EP0 only and
can always be added.

### What changes at High Speed (P4)

| Item | FS | HS |
| --- | --- | --- |
| Bulk MPS | 8/16/32/64 | 512 |
| MSC `transferBytes` | multiple of 64 | multiple of 512 |
| CCID automatic `messageBytes` | 271 | 512 |
| Borrowed RX (`readDirect`) capacity | multiple of 64 | multiple of 512 |
| HID interrupt packet | up to 64 | up to 1024 |
| UAC2 feedback | 10.14, 3 bytes | 16.16, 4 bytes |

CDC, Vendor and MIDI accept `transferBytes` below the MPS. A configuration that
works at FS can fail `begin()` at HS, so check the return value. When `Auto` or
`HighSpeed` is selected on a P4, these HS limits apply even if the link falls
back to FS, because the HS descriptors must be valid too. `device.speed()`
reports the speed the host negotiated (`EspUsbBusSpeed::Full` or `High`, `None`
before the first bus reset).

### Hardware coverage

| Configuration | Status |
| --- | --- |
| S3 ↔ S3 (FS) | Main test bench; all 10 classes |
| P4 HS ↔ P4 FS loopback on one board | All 10 classes, both controllers |
| P4 ↔ P4X HS (both directions) | Checked |
| P4X → Linux PC (HS) | MSC and DFU checked at the wire level with pyusb |
| ESP32-S2 | Compile only |
| Windows, macOS | Not checked |

## 9. Still stuck

1. Read the host's log first: `dmesg -w`, USB Device Tree Viewer, or Console.
   What the host objected to is the most useful single piece of information.
2. Compare with a commercial device of the same class.
3. When reporting an issue, include: the library and core versions, the chip
   and revision, the BringUpCheck output, the DescriptorDump output,
   `esp_err_to_name(device.lastError())` and each Function's `startError()`,
   and the host log excerpt.
