# USB Device Development Guide

> 日本語版: [usb-device-guide.ja.md](usb-device-guide.ja.md)

How to turn an ESP32 into a USB peripheral that a PC - or any other host -
recognises. The first half covers USB device fundamentals, the second half the
ESP32-specific constraints and a hands-on bring-up procedure.

It is written to be readable without prior USB experience, but the goal is that
you can diagnose "why won't the host recognise this?" yourself, so the
explanations are not skipped. If you are in a hurry, start at
[Bring-up procedure](#4-bring-up-procedure) and come back to the first half when
a term is unfamiliar.

Once it works and you want to know why it behaves that way and where the limits
are, continue to the [USB Device Development Guide (Advanced)](usb-device-advanced.md).
It covers the relationship with TinyUSB, descriptors byte by byte, callback
context, and implementing your own class.

If instead you want to **plug USB devices into** an ESP32 (the ESP32 being the
host), the counterpart is [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost)
and its [USB Host development guide](https://github.com/tanakamasayuki/EspUsbHost/blob/main/docs/usb-host-guide.md).

## Contents

1. [USB device fundamentals](#1-usb-device-fundamentals)
2. [Power and connectors](#2-power-and-connectors)
3. [ESP32-specific notes](#3-esp32-specific-notes)
4. [Bring-up procedure](#4-bring-up-procedure)
5. [Observing yourself from the host OS](#5-observing-yourself-from-the-host-os)
6. [Troubleshooting](#6-troubleshooting)
7. [Tool list](#7-tool-list)
8. [References](#8-references)

---

## 1. USB device fundamentals

### 1.1 Host and device

USB is not a peer-to-peer bus. **One host, with devices hanging off it**, and
every transfer starts at the host. A device cannot push data on its own; it
answers when the host comes asking.

With this library the ESP32 is the **device**. That means:

- **You cannot start a conversation.** `keyboard.write()` reaches the bus at the
  moment the host polls that endpoint. If the host never looks, the send waits
  or is dropped.
- **Everything you are is self-declared.** Whether you look like a keyboard or a
  serial port is decided entirely by the descriptors you return.
- **The host decides when you stop.** Suspend, bus reset, and deconfiguration
  can happen at any time, and each resets your state.

This is the big difference from the host-side library. If the host's job is to
*read and understand* someone else's descriptors, the device's job is to
**write descriptors the host will understand**.

### 1.2 Which connector is the device side

The USB Type-C connectors on an ESP32 board look alike but do different jobs.

- **The USB-UART bridge connector** (wired to a CP2102, CH340, …; used for
  flashing and the serial monitor)
- **The ESP32 USB OTG connector** (wired straight to the SoC's D+/D-; **this is
  the one this library uses**)
- **The USB Serial/JTAG connector** (an on-SoC peripheral on S3/P4; also usable
  for flashing and logs)

Silkscreen labels reading `USB` / `OTG` / `UART` are not reliable. **Check the
schematic.**

During development, strongly prefer a **board with two USB connectors**, so the
log port and the port under test are separate. On a single-connector board the
serial monitor disappears the moment you enumerate as a USB device, which makes
diagnosis extremely hard.

### 1.3 Speed

| Speed | Short | Rate | Notes |
|-------|-------|------|-------|
| Low Speed | LS | 1.5 Mbps | Not used by this library |
| Full Speed | FS | 12 Mbps | ESP32-S2/S3, and the ESP32-P4 FS controller |
| High Speed | HS | 480 Mbps | ESP32-P4 HS controller only |
| SuperSpeed | SS | 5 Gbps+ | Not available on ESP32 |

**The device decides the speed**, by how it pulls up D+/D- and chirps. But you
can only pick from what the board's controller has; it is not a free software
choice ([3.2](#32-choosing-fs-or-hs-on-esp32-p4)).

When running at high speed the host also asks what you would do at full speed.
The **Device Qualifier** and **Other Speed Configuration** descriptors answer
that, and this library generates both. They are needed because a bulk endpoint's
max packet size is 512 at HS and 64 at FS, so one class set needs two
descriptors.

### 1.4 Enumeration, from the answering side

The steps between plugging in and being usable. **Where it stops** is the
starting point for every diagnosis.

1. **Attach detection** - you pull up D+ to announce "an FS/HS device is here"
2. **Bus reset** - the host resets the bus
3. **Address assignment** - you answer `SET_ADDRESS`
4. **Device descriptor request** - you return 18 bytes for `GET_DESCRIPTOR(DEVICE)`
5. **Configuration descriptor request** - you return the whole tree; this fails
   most often
6. **String descriptor requests** - manufacturer, product, serial number
7. **`SET_CONFIGURATION`** - only now do the class endpoints open
8. **Host driver binding** - the OS reads the class codes and loads a driver

`device.ready()` becomes true after step 7 (`tud_mounted()` internally).

Reading the failure:

- **Stops at 1-3** (nothing at all on the host) → electrical: cable, connector,
  build settings.
- **Stops at 4-6** (host reports a failed device descriptor request) → the
  descriptors.
- **Reaches 7 but no driver** (shows up as an unknown device) → class codes, or
  the host-side driver.
- **Reaches 8 but does not work** → the class-specific protocol.

### 1.5 You are the one writing the descriptors

A device declares its structure as a sequence of **descriptors**:

```
Device (VID/PID, USB version, EP0 size)
└── Configuration (current draw, bus- or self-powered)
    ├── (IAD: groups several interfaces into one function)
    ├── Interface 0 (class / subclass / protocol <- "what this is" lives here)
    │   ├── class-specific descriptors (HID descriptor, CDC functional, …)
    │   ├── Endpoint 0x81 (IN, interrupt, max packet 8, interval 10)
    │   └── Endpoint 0x01 (OUT, ...)
    └── Interface 1
        └── ...
```

With `EspUsbDevice` you never write these by hand - they are assembled inside
`begin()` from the classes you registered. But **always check what was
assembled**. [`examples/Info/EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/)
prints all of it.

Points worth internalising:

- **The class belongs to the interface, not the device.** This library always
  sets the device descriptor's class to `0x00` (decided per interface), so
  **"what this is" is only visible on the interfaces**. The IAD
  (`bDescriptorType=0x0b`), which groups several interfaces into one function,
  appears inside the configuration descriptor on configurations that include
  CDC.
- **Interface and endpoint numbers are assigned by the library**, in one pass.
  They are not yours to pick; read the actual numbers from DescriptorDump.
- **The HID report descriptor is a separate thing.** The configuration
  descriptor only says how many bytes it is; the host fetches the contents with
  a separate request. That descriptor is what gives your HID data its meaning.
- **Composite HID collapses into one interface.** Keyboard + mouse + gamepad do
  not add interfaces; they share one HID interface and are told apart by report
  ID (keyboard=1, mouse=2, gamepad=3, consumer=4, system=5, vendor=6).

### 1.6 Endpoints and transfer types

An endpoint is a communication slot: an address (`0x81` - bit 7 is direction,
low 4 bits the number), a direction, a transfer type, a max packet size, and a
polling interval.

| Transfer type | Character | Used here for |
|---------------|-----------|---------------|
| **Control** | On EP0, request/response. Every device has one | Enumeration, class requests, HID SET_REPORT, vendor control |
| **Interrupt** | Host polls at a fixed interval; small and low-latency | HID, CDC notifications, CCID status |
| **Bulk** | Bulk data, no bandwidth guarantee, retried on error | CDC data, MSC, NCM, USBVendor |
| **Isochronous** | Reserved bandwidth, no retries | USB Audio |

Direction is **from the host's point of view**: "IN" means data goes from you to
the host.

On the host side, endpoints consumed host channels. On the device side,
**endpoints consume the slots this SoC's USB controller physically has**. That
is the first wall you hit as a device ([3.3](#33-the-endpoint-budget)).

### 1.7 Choosing what to look like

On the host side, "how does this device appear" was someone else's decision.
On the device side **you choose**, and it is the most consequential design
decision in the project.

| Appearance | Class | Good for | Host driver |
|------------|-------|----------|-------------|
| HID keyboard / mouse / gamepad | `0x03` | Input devices, macro pads, automation | Built into every OS. **No driver** |
| HID with vendor-defined reports | `0x03` | Bidirectional custom data, still driverless on Windows | Built in (reachable via HIDAPI etc.) |
| CDC ACM | `0x02` | Look like a serial port; existing serial apps work | Built in (inbox on Windows 10+) |
| USB MIDI | `0x01`/MIDI | DAWs, synths, controllers | Built in. **No driver** |
| Mass Storage | `0x08` | Look like a drive; file handoff, config files | Built in |
| USB Audio | `0x01` | Speaker, microphone | Built in |
| CDC-NCM | `0x02`/NCM | Look like a network adapter; a web UI over USB | Recent OSes support it |
| CCID | `0x0b` | Smart card reader | Built in (PC/SC) |
| Vendor Specific | `0xff` | Custom protocols, fastest bulk transfer | **Windows needs a driver binding** (WinUSB / WebUSB) |

The decision rules are simple:

1. **If the host must not install a driver, use a standard class.** HID, CDC,
   MIDI and MSC work everywhere as-is.
2. **If you need custom data but still no driver, use HID with vendor-defined
   reports.** This is a common commercial pattern. It is not fast, but it is
   bidirectional with no driver and no privileges.
3. **If you need bandwidth, use Vendor Specific bulk.** On Windows that needs a
   Microsoft OS 2.0 descriptor to bind WinUSB; this library emits one when
   WebUSB is enabled.
4. **If an existing application must consume it, use the class that application
   expects.** MIDI for a DAW, CDC for a terminal program.

You can combine several into a composite device, but endpoint budget and host-OS
behaviour both come into play, so get one function working first
([Step 7](#step-7-grow-into-a-composite-device)).

---

## 2. Power and connectors

### 2.1 The device receives power

On the host side the first hurdle was whether the board can *supply* VBUS. As a
device it is the opposite - you are **fed** by the host. That removes most power
problems, but not these:

- **Boards that are not powered from the OTG connector's VBUS.** If that VBUS is
  not wired to the SoC's supply, plugging in only that connector will not even
  boot the board. Power it from the log connector as well.
- **Declaring self-powered operation.** If an external supply runs the board, set
  `config.selfPowered = true`. It is only a declaration in the descriptor, but
  the host's power management reads it, so keep it consistent with the wiring.
- **Without VBUS sensing you cannot notice unplugging.** Most ESP32 boards do
  not wire VBUS sensing, so an unplug never reaches `onBusDetached()` and the
  state is only refreshed by the bus reset on the next plug-in. That is why the
  library also clears state in `onBusAttached()` (`SET_CONFIGURATION`).

### 2.2 bMaxPower

`config.maxPowerMilliamps` (100 mA by default) is the current you declare you
will draw. It does not have to match reality, but raising it to 500 mA is the
honest choice for a power-hungry configuration such as one using Wi-Fi. Some
hosts cut the port when consumption exceeds the declaration.

### 2.3 Connector layout while developing

The recommended arrangement:

| Purpose | Where it goes |
|---------|---------------|
| Flashing and the serial log | The USB-UART bridge connector, or USB Serial/JTAG |
| The host under test | The ESP32 USB OTG connector |

Shipping a single-connector board (an AtomS3, say) is fine, but **develop on a
two-connector board**. If the log dies the instant you enumerate, you cannot
even read `MOUNTED`.

The other option is to use a second ESP32 running
[EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) as the host. That is
what [`tests/peer`](../tests/peer/) in this repository does, and it lets you read
both sides' logs from the PC at once.

---

## 3. ESP32-specific notes

### 3.1 Supported chips and controllers

| Chip | USB device capability | What `EspUsbController::Auto` resolves to |
|------|----------------------|------------------------------------------|
| ESP32-S2 | One FS device controller | FullSpeed |
| ESP32-S3 | One FS device controller; the main target | FullSpeed |
| ESP32-P4 | Two: FS (rhport 0) and HS (rhport 1) | **HighSpeed** |
| ESP32 (original), C3, C6, … | No USB OTG. **This library does not run** | — |

The required Arduino-ESP32 core is **3.3.9 or newer**. Per-version build results
are published in [`docs/`](.) as `COMPATIBILITY.<version>.md`.

### 3.2 Choosing FS or HS on ESP32-P4

Select with `config.controller`. The default `Auto` resolves to **HighSpeed** on
P4.

| | FullSpeed (rhport 0) | HighSpeed (rhport 1) |
|---|---|---|
| PHY | On-SoC | External UTMI PHY |
| Default pins | GPIO26=D-, GPIO27=D+ | Board's UTMI wiring |
| Speed | 12 Mbps | 480 Mbps |
| Endpoint budget | Tight ([3.3](#33-the-endpoint-budget)) | Roomy |

Which connector is wired to which PHY **differs per board**; check the
schematic. Typically the HS connector is a third one - neither the USB
Serial/JTAG connector nor the GPIO26/27 FS pair.

The P4 FS PHY shares pins with USB Serial/JTAG, and
`usb_wrap_ll_phy_select(&USB_WRAP, 0)` routes it to GPIO24/25 instead (shown
commented out in [`examples/P4FullSpeedDevice`](../examples/P4FullSpeedDevice/)).
After the swap USB Serial/JTAG moves to GPIO26/27, so a serial monitor on the
old pins disconnects.

### 3.3 The endpoint budget

**This is the first hard limit you hit as a device.** A USB controller has a
bounded number of endpoints and endpoint numbers, and a configuration that
exceeds them is rejected by `begin()` with `ESP_ERR_INVALID_SIZE`, before the
PHY is started.

| Controller | Max endpoint number | Non-control IN | Non-control OUT |
|---|---|---|---|
| ESP32-S2 / S3 | 5 | 4 | 5 |
| ESP32-P4 rhport 0 (FS) | 6 | 4 | 6 |
| ESP32-P4 rhport 1 (HS) | 15 | 7 | 15 |

Roughly what each class costs:

| Class | Endpoints used |
|-------|----------------|
| HID (one interface for the whole composite HID) | 1 IN |
| CDC ACM | 1 notification IN + 1 data IN + 1 data OUT |
| USB MIDI | 1 IN + 1 OUT |
| MSC | 1 IN + 1 OUT |
| USBVendor | 1 IN + 1 OUT |
| USB Audio | 1 isochronous endpoint per direction |
| CDC-NCM | 1 notification IN + 1 data IN + 1 data OUT |
| CCID | 1 IN + 1 OUT + 1 notification IN |

In practice, the binding limit on ESP32-S3 is **four IN endpoints**. HID + CDC +
MSC already uses 1+2+1 = 4. One more class does not fit.

Checking is easy: set the `DUMP_ENABLE_*` defines in
[`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/) to
the combination you want, build, and read the endpoint budget at the end. You do
not even need to attach a host.

### 3.4 Other library-side limits

| Item | Limit | What happens past it |
|------|-------|----------------------|
| Registered classes | 4 | The fifth object's constructor calls `addClass()`, which fails and **registers nothing, silently** |
| Configuration descriptor | 704 bytes | `begin()` fails |
| HID report descriptor | 256 bytes | Same |
| String descriptors | 63 characters | Truncated |

The class limit fails quietly, so when building a composite always confirm the
interface list in DescriptorDump.

### 3.5 Mutually exclusive with the stock Arduino-ESP32 USB stack

This library builds its own TinyUSB and initialises the ESP-IDF PHY/controller
directly. Therefore:

- **Do not call `USB.begin()`.** `USBHIDKeyboard`, `USBHIDMouse` and `USBCDC`
  cannot be used alongside it either.
- **Build with USB Mode set to "USB-OTG (TinyUSB)".** Setting the ESP32-S3/P4
  board menu's `USB Mode` to `Hardware CDC and JTAG` routes D+/D- to the USB
  Serial/JTAG peripheral, and the OTG controller becomes unusable. With
  `arduino-cli`, `esp32:esp32:esp32s3:USBMode=default` *is* "USB-OTG (TinyUSB)"
  (every `sketch.yaml` in this repository specifies it).
- **Leave `USB CDC On Boot` disabled**, or Arduino tries to bring up a second
  USB CDC of its own.

In this configuration `Serial` comes out on the UART, which is why you need a
separate port for logs ([2.3](#23-connector-layout-while-developing)).

### 3.6 Logs

The reason enumeration failed is often only visible in the ESP-IDF log. Set Core
Debug Level to `Verbose` in the Arduino IDE while investigating.

And always read the **host OS log too**
([section 5](#5-observing-yourself-from-the-host-os)). When developing a device,
what the host received and what it objected to is your best source of
information.

---

## 4. Bring-up procedure

The path to a working USB device. **Go top to bottom without skipping.** Most
dead ends come from debugging a later step while an earlier problem (connector,
build setting, endpoint budget) is still there.

### Step 0. Bench checks, before writing code

- From the schematic, confirm **which connector goes straight to the ESP32 USB
  OTG pins**
- Confirm you can keep the log port and the host port separate
- Confirm the cable carries data (not a charge-only cable)
- Confirm the board menu's `USB Mode` is "USB-OTG (TinyUSB)"

### Step 1. Confirm it starts and enumerates

Flash [`examples/Info/EspUsbDeviceBringUpCheck`](../examples/Info/EspUsbDeviceBringUpCheck/).
It enumerates as a HID keyboard but never sends a key, so it is safe to leave
plugged into a PC.

- Does `BEGIN ok` appear? If not, the problem is on the board (target, core
  version, endpoint budget)
- Connect to the host - does `MOUNTED` appear? If not, it is electrical or the
  descriptors
- Note the speed (Full / High)
- Press CapsLock on the host - does `HOST_OUTPUT_REPORT` appear? That confirms
  both directions

Once this passes, the board and the build settings are known good, and every
later problem is specific to your own configuration.

### Step 2. Decide what to look like

Pick a class from [the table in 1.7](#17-choosing-what-to-look-like). This
decision shapes the whole project, so make it first. When in doubt:

- Host must not install a driver → a standard class
- Custom data with no driver → HID with vendor-defined reports
- Bandwidth matters → Vendor Specific bulk
- An existing application must consume it → that application's class

### Step 3. Get the minimum working with the class API

Flash the matching example as-is.

| Class | Example |
|-------|---------|
| HID keyboard / mouse | [`Keyboard`](../examples/Keyboard/) / [`Mouse`](../examples/Mouse/) |
| HID gamepad / media keys | [`Gamepad`](../examples/Gamepad/) / [`MediaKeys`](../examples/MediaKeys/) |
| HID custom reports | [`CustomHID`](../examples/CustomHID/) / [`VendorHID`](../examples/VendorHID/) |
| CDC ACM | [`Serial`](../examples/Serial/) |
| USB MIDI | [`MIDI`](../examples/MIDI/) |
| MSC | [`MSC`](../examples/MSC/) / [`MSCFatRamDisk`](../examples/MSCFatRamDisk/) / [`MSCSdCard`](../examples/MSCSdCard/) |
| USB Audio | [`AudioSpeaker`](../examples/AudioSpeaker/) / [`AudioMicrophone`](../examples/AudioMicrophone/) / [`AudioHeadset`](../examples/AudioHeadset/) |
| CDC-NCM | [`UsbNetwork`](../examples/UsbNetwork/) |
| CCID | [`SmartCardReader`](../examples/SmartCardReader/) |
| Vendor bulk | [`USBVendor`](../examples/USBVendor/) |

**Get the example working before writing your own code.** When something breaks
later, you can tell a library problem from a configuration problem.

### Step 4. Check what you are declaring

Set the `DUMP_ENABLE_*` defines in
[`examples/Info/EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/)
to match your configuration, build, and read the serial output. No host needed.

What to check:

- How many interfaces there are, and each one's class / subclass / protocol
- Endpoint types, directions, MPS, interval
- How large the HID report descriptor is
- **Whether it fits the endpoint budget**

### Step 5. Compare against what the host received

Step 4 is what you *meant* to send. Now confirm the host got the same bytes.

```sh
# Linux
lsusb -v -d 303a:4051

# Any OS, via PyUSB
cd tests
uv run --with pyusb python manual/device_inspect/device_inspect.py --pid 0x4051
```

If they match, the descriptor layer is fine. If they differ - or if the host
shows nothing at all - the problem is further down (enumeration, electrical,
controller selection). Section 5 covers how to look.

### Step 6. Nail it down by hand

When the class works but "the host's software does not react the way I expect",
[`examples/Info/EspUsbDeviceConsole`](../examples/Info/EspUsbDeviceConsole/) is
the tool. One transfer at a time, no rebuild, so the experiment loop is fast.

```
> state
STATE mounted=1 speed=full leds=0x00 hid_protocol=report vendor_mounted=0
> key 0x04                            # does the host react to this usage?
> report 1 00 00 05 00 00 00 00 00    # put a raw report on the wire
> vendor 01 02 03 04                  # raw bytes on the vendor bulk IN endpoint
```

At the same time everything coming *down* from the host - HID output reports,
protocol switches, vendor control requests, bulk OUT - is printed with a `HOST_`
prefix. **Watching that while the host's own software runs is how you learn the
initialization sequence it expects.**

### Step 7. Grow into a composite device

Once a single function works, combine them. Only two constraints matter here:

- **The endpoint budget** ([3.3](#33-the-endpoint-budget)) - check it in
  DescriptorDump first
- **At most four classes** ([3.4](#34-other-library-side-limits))

Composite HID does not add interfaces; it adds report IDs on one HID interface.
[`CompositeDevice`](../examples/CompositeDevice/),
[`CompositeHidCdcMsc`](../examples/CompositeHidCdcMsc/) and
[`KeyboardMouse`](../examples/KeyboardMouse/) are the references.

Some host OSes behave differently depending on interface order and whether an
IAD is present. After rearranging, re-verify on the OSes you actually target.

### Step 8. Check that it holds up

Enumerating once is not the same as staying usable.

```sh
cd tests
uv run --with pyusb python manual/enumeration_soak/enumeration_soak.py --cycles 50
```

That repeats re-enumeration and configuration changes, checking that the
descriptors never drift and that the device keeps answering. On top of that:

- Suspend and resume the host
- Unplug and replug many times
- Reboot the host and check it is recognised at boot (BIOS/UEFI switches HID
  devices to boot protocol)

### Step 9. Write down the result

- Put the smallest working sketch under `examples/` (with README.md,
  README.ja.md and sketch.yaml if contributing here)
- Add anything that needs a human to `tests/manual/`
- Add anything automatable on two boards to `tests/peer/`
- **State the host OS and version you verified on.** On the device side this is
  the equivalent of the host side's VID:PID - the single most important fact.

---

## 5. Observing yourself from the host OS

Host-side development meant capturing and analysing someone else's device. On
the device side you observe **how you look to the host**. The tools differ per
OS.

### 5.1 Linux

```sh
# What happened (start this before plugging in)
sudo dmesg -w
udevadm monitor --udev

# All descriptors
lsusb -v -d 303a:4050

# Tree and speed (12M / 480M)
lsusb -t

# HID report descriptor
sudo usbhid-dump -d 303a:4050

# What is actually arriving as keyboard/mouse input
sudo evtest

# Capture your own transfers
sudo modprobe usbmon
sudo wireshark   # pick usbmonX
```

`dmesg` matters most: it states the reason when a descriptor is invalid, and
shows which driver bound.

### 5.2 Windows

- **Device Manager** - "Unknown USB Device (Device Descriptor Request Failed)"
  means it failed at the descriptor stage; a Code 10 / Code 43 warning means
  driver binding failed.
- **[USB Device Tree Viewer](https://www.uwe-sieber.de/usbtreeview_e.html)** -
  the most useful tool on Windows. It shows both the raw descriptor bytes and
  the error Windows produced.
- **Event Viewer** - details of a failed driver bind.
- **`pnputil /enum-devices /connected`** - which driver bound.

**Windows caches descriptors per VID/PID.** This is the number one cause of "it
still shows the old name" and "my change had no effect" in device development.
Fix it by one of:

- Changing the PID whenever you change descriptors during development (that is
  why the Info sketches here use separate PIDs)
- Changing `config.serialNumber`
- Deleting the device in Device Manager and replugging

### 5.3 macOS

- **System Information → USB** - the tree and a descriptor summary
- `ioreg -p IOUSB -l -w 0` - more detail
- Wireshark on the `XHC20` interface for captures

### 5.4 Using another ESP32 as the host

If the real host is an embedded system rather than a PC, an ESP32 running
[EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) can play host. Both
sides' logs are then readable from the PC at once, which speeds up diagnosis.
[`tests/peer`](../tests/peer/) in this repository is exactly that setup.

### 5.5 Compare against something that works

When a host dislikes your device, the fastest route is to **put a commercial
device of the same class side by side**.

```sh
lsusb -v -d <the commercial device's vid:pid> > known-good.txt
lsusb -v -d 303a:4051                         > mine.txt
diff -u known-good.txt mine.txt
```

Interface ordering, presence of an IAD, `bInterfaceProtocol`, endpoint intervals
- the fields that are technically optional but that real host drivers assume -
are what show up in the diff.

---

## 6. Troubleshooting

| Symptom | Likely cause | Check / fix |
|---------|--------------|-------------|
| `begin()` fails with `ESP_ERR_INVALID_SIZE` | Endpoint budget exceeded, or descriptor too large | Read the budget in DescriptorDump. Drop a class. On P4, use HS ([3.3](#33-the-endpoint-budget)) |
| `begin()` fails with `ESP_ERR_NOT_SUPPORTED` | Target has no USB device controller | Confirm it is an S2 / S3 / P4 |
| `begin()` succeeds but the host shows nothing | Wrong connector (plugged into the UART / Serial-JTAG side) | Check the schematic ([1.2](#12-which-connector-is-the-device-side)) |
| Same | `USB Mode` built as Hardware CDC and JTAG | Set it to "USB-OTG (TinyUSB)" ([3.5](#35-mutually-exclusive-with-the-stock-arduino-esp32-usb-stack)) |
| Same | Charge-only cable | Use a data cable |
| Same, on P4 | Controller does not match the connector | Set `config.controller` to match the connector (FS/HS) you used ([3.2](#32-choosing-fs-or-hs-on-esp32-p4)) |
| Windows: "Device Descriptor Request Failed" | Invalid descriptor, or none returned | Read the raw bytes in USB Device Tree Viewer and compare with DescriptorDump |
| Host still shows the old device after a change | **Windows descriptor cache** | Change the PID or serial number; or delete the device in Device Manager and replug ([5.2](#52-windows)) |
| A fifth class never appears in the descriptor | Only four classes fit; the fifth is dropped silently | Check the interface list in DescriptorDump ([3.4](#34-other-library-side-limits)) |
| Reaches `SET_CONFIGURATION` but no driver binds | The class / subclass / protocol combination | Diff against a commercial device of the same class ([5.5](#55-compare-against-something-that-works)) |
| Cannot open the vendor interface on Windows | Not bound to WinUSB | Set `config.webusbEnabled = true` so the MS OS 2.0 descriptor is returned; failing that, bind with Zadig |
| HID keystrokes produce the wrong characters | Host keyboard layout mismatch | Set `keyboard.setLayout()` to the host's layout |
| Keyboard does not work in BIOS / UEFI | The host switched to boot protocol | Watch `HOST_HID_PROTOCOL` in Console. NKRO folds to 6 keys under boot protocol |
| Sends never arrive at the host | Sending before mount | Check `device.ready()` first; sends while unmounted are dropped |
| No log, or the log dies right after `begin()` | Single-connector board sharing the log and device port | Use a two-connector board, or a peer-board setup ([2.3](#23-connector-layout-while-developing)) |
| Stops working after repeated replugging | State surviving re-enumeration | Reproduce with `enumeration_soak`, and drop state in `onBusDetached()` / `onBusAttached()` |
| MSC will not mount | FAT image or block size | Compare against `MSCFatRamDisk` |
| Audio drops out | FIFO read/write cadence | Poll the API at a steady interval; watch the stream stats |
| No idea at all | You have not read the host's log | Start with `dmesg -w` (Linux) or USB Device Tree Viewer (Windows) ([section 5](#5-observing-yourself-from-the-host-os)) |

---

## 7. Tool list

### For users (examples/Info/)

| Tool | Purpose |
|------|---------|
| [`EspUsbDeviceBringUpCheck`](../examples/Info/EspUsbDeviceBringUpCheck/) | **Run this first.** Whether begin() succeeded, whether the host enumerates, the speed, and host->device traffic. Includes a checklist for when nothing enumerates |
| [`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/) | **Run this next.** Every descriptor the library built, plus the endpoint budget. No host needed |
| [`EspUsbDeviceConsole`](../examples/Info/EspUsbDeviceConsole/) | **For nailing things down.** Type HID reports and vendor transfers into a serial terminal, and see every request the host makes |

The per-feature examples are listed in [`examples/README.md`](../examples/README.md).
How the library works internally is covered in the
[USB Device Development Guide (Advanced)](usb-device-advanced.md).

### For developers (tests/manual/)

PyUSB scripts you run on the PC. See [tests/manual/README.md](../tests/manual/README.md)
for how to run them.

| Tool | Purpose |
|------|---------|
| [`device_inspect`](../tests/manual/device_inspect/) | Every descriptor as the host received it - the cross-check for DescriptorDump. `--json` for diffing |
| [`enumeration_soak`](../tests/manual/enumeration_soak/) | Repeated re-enumeration and configuration changes, checking the descriptors never drift |
| [`p4_hs_bulk`](../tests/manual/p4_hs_bulk/) | ESP32-P4 High-Speed operation and effective bulk throughput |
| [`usb_ncm`](../tests/manual/usb_ncm/) | Whether the host OS binds its NCM driver to the network device |

The automated tests live in [`tests/peer`](../tests/peer/) (two boards) and
[`tests/unit`](../tests/unit/) (host-side descriptor checks). See
[tests/TEST_PLAN.md](../tests/TEST_PLAN.md) for the structure.

---

## 8. References

- [USB 2.0 Specification](https://www.usb.org/document-library/usb-20-specification) - the standard itself
- [USB Class Codes](https://www.usb.org/defined-class-codes) - the class code list
- [USB Device Class Documents](https://www.usb.org/documents) - HID, CDC, MSC, CCID, UAC and the rest
- [HID Usage Tables](https://usb.org/document-library/hid-usage-tables-16) - required reading when writing a report descriptor
- [USB Made Simple](https://www.usbmadesimple.co.uk/) - a beginner-friendly explanation
- [USB Device Tree Viewer](https://www.uwe-sieber.de/usbtreeview_e.html) - observation on Windows
- [TinyUSB](https://docs.tinyusb.org/) - the device stack this library vendors
- [pid.codes](https://pid.codes/) - VID/PID for open source projects
- This repository's [README.md](../README.md) - API reference and per-class status
- [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) - the library for making an ESP32 the host, with its [USB Host development guide](https://github.com/tanakamasayuki/EspUsbHost/blob/main/docs/usb-host-guide.md)
