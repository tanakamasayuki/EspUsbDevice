# Troubleshooting

> 日本語版: [troubleshooting.ja.md](troubleshooting.ja.md)

Symptom-first fixes for EspUsbDevice problems, collected in one linkable place.
Fundamentals and the full bring-up walkthrough are in the
[USB Device Development Guide](usb-device-guide.md); library internals are in
the [advanced guide](usb-device-advanced.md).

Two sketches answer most questions before any host is involved:

| Tool | What it tells you |
|------|-------------------|
| [`EspUsbDeviceBringUpCheck`](../examples/Info/EspUsbDeviceBringUpCheck/) | whether `begin()` succeeded, whether a host enumerated the device, and at which speed |
| [`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/) | every descriptor the library built, plus the endpoint budget - no host needed |

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

### `ESP_ERR_INVALID_SIZE`

The configuration exceeds the controller's endpoint budget or a descriptor
size limit. The check runs before the PHY starts, so the host never sees
anything.

1. Build [`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/)
   with the same class combination (the `DUMP_ENABLE_*` defines) and read the
   endpoint budget at the end. No host is needed.
2. Drop or swap a class. On ESP32-S2/S3 the binding limit is **four
   non-control IN endpoints**; HID + CDC + MSC (1+2+1) already fills it
   ([guide 3.3](usb-device-guide.md#33-the-endpoint-budget)).
3. On ESP32-P4, select the HS controller - its budget is much roomier
   ([guide 3.2](usb-device-guide.md#32-choosing-fs-or-hs-on-esp32-p4)).
4. If the endpoint budget fits, check the configuration descriptor is within
   704 bytes and each HID report descriptor within 256
   ([guide 3.4](usb-device-guide.md#34-other-library-side-limits)).

### `ESP_ERR_NOT_SUPPORTED`

1. Confirm the target is ESP32-S2, ESP32-S3, or ESP32-P4. The original ESP32,
   C3, C6, and the other targets have no USB-OTG device controller, and this
   library does not run there
   ([guide 3.1](usb-device-guide.md#31-supported-chips-and-controllers)).

### Any other `begin()` failure

1. Print `device.lastErrorName()` - every failure path names its reason.
2. Raise Core Debug Level to `Verbose` and read the ESP-IDF log
   ([guide 3.6](usb-device-guide.md#36-logs)).

## 2. begin() succeeds but the host sees nothing

Work through these in order. All are common, and the first two have nothing to
do with your code:

1. **Wrong connector.** Many boards also expose a UART or USB-Serial/JTAG
   connector that is not wired to the OTG controller. Check the schematic
   ([guide 1.2](usb-device-guide.md#12-which-connector-is-the-device-side)).
2. **Charge-only cable.** Swap in a known-good data cable.
3. **`USB Mode` built as "Hardware CDC and JTAG".** That routes D+/D- to the
   Serial/JTAG peripheral and the OTG controller never sees the bus. Build
   with "USB-OTG (TinyUSB)"
   ([guide 3.5](usb-device-guide.md#35-mutually-exclusive-with-the-stock-arduino-esp32-usb-stack)).
4. **ESP32-P4 only: the controller does not match the connector.** `Auto`
   resolves to HighSpeed on P4. If the cable is on the FS pins (GPIO26/27),
   set `config.controller = EspUsbController::FullSpeed`; if it is on the
   board's HS connector, use HighSpeed
   ([guide 3.2](usb-device-guide.md#32-choosing-fs-or-hs-on-esp32-p4)).
5. Then run BringUpCheck and read the
   [host's own log](#5-host-os-specifics). Past this point the remaining
   causes are visible only from the host side.

## 3. Enumeration starts but ends badly

### Windows shows "Unknown USB Device (Device Descriptor Request Failed)"

The device failed at the descriptor stage.

1. Open [USB Device Tree Viewer](https://www.uwe-sieber.de/usbtreeview_e.html)
   and read the raw descriptor bytes and the exact Windows error.
2. Compare those bytes with the DescriptorDump output; on Linux, cross-check
   with [`tests/manual/device_inspect`](../tests/manual/device_inspect/)
   (`--json` makes the diff easy).

### The host still shows the old device after a change

**Windows caches descriptors per VID/PID.** This is the number one cause of
"my change had no effect" during device development.

1. Change the PID whenever descriptors change during development (the Info
   sketches in this repository use separate PIDs for exactly this reason).
2. Or change `config.serialNumber`.
3. Or delete the device in Device Manager and replug.

### `SET_CONFIGURATION` completes but no driver binds

The descriptors are structurally valid, but the class / subclass / protocol
combination is not one the host OS maps to a driver.

1. Put a commercial device of the same class side by side and diff the
   descriptors
   ([guide 5.5](usb-device-guide.md#55-compare-against-something-that-works)).
   Interface order, IAD presence, `bInterfaceProtocol`, and endpoint intervals
   are the usual suspects.

### A class you registered never appears in the descriptor

1. Only four classes fit; the fifth `addClass()` fails **silently**. Confirm
   the interface list in DescriptorDump
   ([guide 3.4](usb-device-guide.md#34-other-library-side-limits)).

## 4. Build and upload

1. **Use arduino-esp32 core 3.3.9 or newer.** Per-version build results are
   published as [`COMPATIBILITY.<version>.md`](.) in this directory.
2. **Do not call `USB.begin()`**, and do not instantiate `USBHIDKeyboard`,
   `USBHIDMouse`, or `USBCDC` alongside this library - the two stacks fight
   over the one controller
   ([guide 3.5](usb-device-guide.md#35-mutually-exclusive-with-the-stock-arduino-esp32-usb-stack)).
3. **Leave `USB CDC On Boot` disabled**, or the core brings up a second CDC of
   its own.
4. In this configuration `Serial` comes out on the UART. If your board's only
   connector is the OTG one, logs and the device role share a plug - use a
   two-connector board or a UART adapter while developing
   ([guide 2.3](usb-device-guide.md#23-connector-layout-while-developing)).

## 5. Host-OS specifics

The full observation guide is
[guide section 5](usb-device-guide.md#5-observing-yourself-from-the-host-os).
The short version:

### Linux

```sh
sudo dmesg -w          # start before plugging in; names the rejection reason
lsusb -v -d 303a:      # descriptors as the host parsed them
```

- `dmesg` states why a descriptor was rejected and which driver bound.
- libusb / PyUSB tools failing with permission errors need a udev rule (or a
  one-off `sudo chmod a+rw /dev/bus/usb/<bus>/<dev>`); see
  [tests/manual/README.md](../tests/manual/README.md).

### Windows

- USB Device Tree Viewer shows raw descriptors plus the error.
- Device Manager: "Device Descriptor Request Failed" = descriptor stage;
  Code 10 / Code 43 (the "!" badge) = driver binding.
- Remember the [descriptor cache](#the-host-still-shows-the-old-device-after-a-change).
- A vendor interface is only usable once WinUSB binds: set
  `config.webusbEnabled = true` so the MS OS 2.0 descriptor is served, or
  bind manually with Zadig.

### macOS

- System Information → USB for the tree; `ioreg -p IOUSB -l -w 0` for detail.

## 6. Class-specific issues

### HID keyboard

- **Wrong characters arrive** - the host decodes scancodes with its own
  layout. Match it with `keyboard.setLayout()`.
- **Nothing arrives** - check `device.ready()` before sending; reports sent
  while unmounted are dropped.
- **Does not work in BIOS / UEFI** - the host switched to boot protocol.
  NKRO folds down to 6 keys there; watch `HOST_HID_PROTOCOL` in
  [`EspUsbDeviceConsole`](../examples/Info/EspUsbDeviceConsole/).
- **Lock LEDs** - read them with `ledState()` even when no
  `onOutputReport()` callback is installed.

### USB MIDI

- With EspUsbHost as the host, keep cable counts at five or fewer per
  direction: beyond that the configuration descriptor exceeds the host's
  256-byte control transfer limit and enumeration fails on the host side.

### MSC

- **Will not mount** - the FAT image or block size is off. Compare against
  [`MSCFatRamDisk`](../examples/MSCFatRamDisk/), which is a known-good
  minimal image.

### USB Audio

- **Dropouts** - poll `playback.read()` / `capture.write()` at a steady
  cadence and watch the stream stats; the FIFOs are bounded.
- UAC1 is the default; UAC2 must be selected explicitly in the
  `EspUsbAudioFunction` constructor. One sample rate is declared per
  direction.

### CDC-NCM network

- **The host gets no network** - check whether the host OS bound its NCM
  driver at all with [`tests/manual/usb_ncm`](../tests/manual/usb_ncm/). The
  device side serves DHCP on `192.168.7.0/24` (device = `192.168.7.1`) in the
  [`UsbNetwork`](../examples/UsbNetwork/) example.
- **Throughput dies until reboot on 2.0.x** - fixed in 2.1.0 (the DWC2 driver
  now runs in DMA mode); update the library.

## 7. Stability

- **Stops working after repeated replugging** - some state is surviving
  re-enumeration. Reproduce with
  [`tests/manual/enumeration_soak`](../tests/manual/enumeration_soak/), and
  drop state in `onBusDetached()` / `onBusAttached()`.
- **The log dies right after `begin()`** - on a single-connector board the
  log port and the device port are the same plug
  ([guide 2.3](usb-device-guide.md#23-connector-layout-while-developing)).

## 8. Per-chip notes

| Chip | Notes |
|------|-------|
| ESP32-S2 | One FS controller. Same endpoint budget class as the S3 (max EP number 5, four non-control IN) |
| ESP32-S3 | One FS controller; the main hardware-verified target. The four-IN budget is the practical composite ceiling |
| ESP32-P4 | FS (rhport 0) **and** HS (rhport 1); `Auto` picks HS. The FS PHY shares pins with USB-Serial/JTAG - `usb_wrap_ll_phy_select()` swaps it to GPIO24/25, after which Serial/JTAG moves to GPIO26/27. Which physical connector reaches which PHY differs per board |
| ESP32 / C3 / C6 / ... | No USB-OTG device controller; not supported |

Independent of the controller, at most **four classes** register per device,
and that limit fails silently ([guide 3.4](usb-device-guide.md#34-other-library-side-limits)).

## 9. Still stuck

1. Read the host's log first - `dmesg -w`, USB Device Tree Viewer, or
   Console.app. What the host objected to is the single best piece of
   information ([guide section 5](usb-device-guide.md#5-observing-yourself-from-the-host-os)).
2. Diff against a commercial device of the same class
   ([guide 5.5](usb-device-guide.md#55-compare-against-something-that-works)).
3. When reporting an issue, include: the library and core versions, the chip,
   the BringUpCheck output, the DescriptorDump output, and the host OS log
   excerpt. Those five reproduce most problems on sight.
