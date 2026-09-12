# USB Device Development Guide (Advanced)

> 日本語版: [usb-device-advanced.ja.md](usb-device-advanced.ja.md)

A continuation of the [USB Device Development Guide](usb-device-guide.md). Where
the introduction covers "getting the host to recognise it", this covers **why it
behaves that way, where the limits are, and what to measure when you hit one**.

It is written for readers who already have at least one device working and are
facing one of these:

- Needing to write or read descriptors byte by byte
- Not fitting the endpoint budget, not getting the throughput, or transfers
  stalling
- Wanting to know exactly what is safe to do inside a callback
- Adding a class the library does not support
- Wanting to change the TinyUSB configuration, or to know why you cannot

## Contents

1. [Architecture and task model](#1-architecture-and-task-model)
2. [The relationship with TinyUSB](#2-the-relationship-with-tinyusb)
3. [Writing descriptors byte by byte](#3-writing-descriptors-byte-by-byte)
4. [Control transfers, from the answering side](#4-control-transfers-from-the-answering-side)
5. [Endpoint resources: numbering and buffers](#5-endpoint-resources-numbering-and-buffers)
6. [Transfer timing and bandwidth](#6-transfer-timing-and-bandwidth)
7. [Errors and recovery](#7-errors-and-recovery)
8. [Callback context](#8-callback-context)
9. [Implementing a new class](#9-implementing-a-new-class)
10. [Measurement and debugging](#10-measurement-and-debugging)

---

## 1. Architecture and task model

### 1.1 The layers

```
Sketch (loop / setup)
  ↕ callback registration, send APIs
EspUsbDevice + classes       … descriptor generation, class state, buffers
  ↕ tud_* API / tud_*_cb() implementations
TinyUSB device stack (usbd)  … standard requests, class drivers, EP0
  ↕ dcd_*
dcd_dwc2                     … endpoints, FIFOs, DMA descriptors
  ↕ esp_private/usb_phy
USB OTG controller + PHY
```

This is where the device side differs most from the host side. On the host side
the bottom two layers (the ESP-IDF USB Host Library and the HCD) are prebuilt
binaries shipped by Arduino-ESP32, and **their configuration cannot be changed**.
On the device side, **this library builds TinyUSB itself**. The lower layers are
therefore ours, and the `CFG_TUD_*` values are ones we chose
([section 2](#2-the-relationship-with-tinyusb)).

What is still used from Arduino-ESP32 is the SoC definitions, PHY setup
(`usb_new_phy()`), FreeRTOS and board support. What was cut is the Arduino USB
Device integration, not the core.

### 1.2 One task

`begin()` creates exactly **one** FreeRTOS task - in contrast to the host side's
two (daemon and client).

| Property | Value |
|----------|-------|
| Task name | `espusb-device` |
| Stack | 4096 bytes |
| Priority | `configMAX_PRIORITIES - 1` (**the highest**) |
| Core | Not pinned |

The loop body is only two calls:

```cpp
while (true) {
  tud_task_ext(1, false);      // process USB events, bounded at 1 ms
  espUsbDeviceNetDrainTx();    // hand queued NCM frames to TinyUSB
}
```

It uses `tud_task_ext(1, false)` rather than `tud_task()` because **this task has
to keep turning while the bus is quiet**. `tud_task()` blocks until the next USB
event, and network frames queued in the meantime would never reach TinyUSB. A
bounded 1 ms wait keeps the task cheap when idle while giving the drain a floor
on how often it runs.

The stack size and priority are not currently settable from
`EspUsbDeviceConfig`. The priority is the maximum so that USB timing - answering
the host's polling - is not lost to other work. **Heavy work in the sketch will
not stall this task; but heavy work inside this task stalls the whole system**
([section 8](#8-callback-context)).

### 1.3 TinyUSB APIs are called from the usbd task

TinyUSB requires its device API to be called from the same context as
`tud_task()`. **This is not an internal style rule - breaking it actually breaks
things.**

The failure was measured in this library. When `tud_network_xmit()` was called
from lwIP's tcpip task, transfer completions overlapped the call (measured:
**about 70% of completions landed inside such a call**), and the IN endpoint
ended up armed with packets to send but an empty FIFO. No completion interrupt
ever arrived, the NTB was never returned to the pool, and
`tud_network_can_xmit()` stayed false forever - **the USB network was dead until
reboot**.

So today producers only **copy the frame into a pooled buffer and hand it to a
queue**; every TinyUSB call is made by `espUsbDeviceNetDrainTx()` on the usbd
task.

| Constant | Value | Why |
|----------|-------|-----|
| `NET_TX_FRAME_MAX` | 1600 | One Ethernet frame |
| `NET_TX_SLOTS` | 4 (~6.4 KB) | Absorbs the burst lwIP hands over between two usbd turns, without so much buffering that TCP loses its feedback signal |
| `NET_TX_ACQUIRE_TIMEOUT` | 20 ms | If no slot frees up, **drop the frame**. TCP retransmits, whereas blocking the tcpip task stalls every other interface with it |

This shape - producers copy and queue, one task owns TinyUSB - is the pattern to
reuse when adding your own class ([section 9](#9-implementing-a-new-class)).

### 1.4 Start and stop ordering

The order inside `begin()` is deliberate:

1. `buildDescriptors()` - assemble the descriptors and **validate the endpoint
   budget**. A failure here means the PHY never starts
2. Each class's `begin()` - on failure, `end()` the already-started classes in
   reverse
3. `startTinyUsbRuntime()` - create the PHY, `tusb_init(rhport)`, create the task
4. Each class's `afterDeviceStarted()`

So **an invalid configuration produces no electrical activity at all**. From the
host it looks like "plugged in, nothing happened", and the cause is entirely on
the board. That is why BringUpCheck reports `BEGIN` before anything else.

Setting `config.startTinyUsb = false` **stops after step 2 and only builds the
descriptors**. No hardware and no host are needed, which is how
`tests/single/descriptor` and `tests/single/composite_constraints` verify
descriptors. It is equally useful for automated tests of your own configuration.

Teardown, via `stopTinyUsbRuntime()`, runs in reverse: delete the task,
`tusb_deinit()`, `usb_del_phy()`. After `end()` the same object can `begin()`
again.

---

## 2. The relationship with TinyUSB

The most important structural fact on the device side is that **this library
vendors TinyUSB's sources and builds them itself**. Arduino-ESP32's prebuilt
`libarduino_tinyusb` is not used.

### 2.1 What is vendored

From a pinned TinyUSB commit, **only the 43 files needed (12 sources, 31
headers)** are copied mechanically under `src/`. There are no patches to
upstream files.

| Area | Files |
|------|-------|
| Core | `tusb.c`, `common/tusb_fifo.c`, `device/usbd.c` |
| Classes | `hid`, `cdc`, `midi`, `msc`, `vendor`, `net/ncm`, `audio` - each `*_device.c` |
| Controller | `portable/synopsys/dwc2/dcd_dwc2.c`, `dwc2_common.c` |

Host, Type-C, DFU, Video, Printer, MTP, MIDI 2.0, ECM/RNDIS, non-FreeRTOS OSAL
and non-ESP32 portable files are **not copied**. The list is the minimum derived
from compiler dependencies on clean S2/S3/P4 builds.

Provenance (repository, full commit SHA, TinyUSB version, selection rationale)
is in [`third_party/tinyusb/UPSTREAM.json`](../third_party/tinyusb/UPSTREAM.json),
with the reasoning and verification method in
[PROVENANCE.md](../third_party/tinyusb/PROVENANCE.md). The verification script
fetches the pinned tarball, extracts only the 43 manifest files, and compares
them **byte for byte**. A normal build downloads nothing.

```sh
python tools/verify_tinyusb_vendor.py
```

Only `src/tusb_config.h` and `src/internal/EspUsbTinyUsbConfig.h` are
EspUsbDevice's own integration files.

### 2.2 What owning tusb_config.h bought

`src/tusb_config.h` only includes `internal/EspUsbTinyUsbConfig.h`, where the
real values live:

| Setting | Value | Meaning |
|---------|-------|---------|
| `CFG_TUSB_MCU` | S2 / S3 / P4 | Any other target is an `#error` |
| `CFG_TUSB_OS` | `OPT_OS_FREERTOS` | |
| `CFG_TUD_MAX_SPEED` | P4: `HIGH_SPEED`, else `FULL_SPEED` | |
| `CFG_TUD_ENDPOINT0_SIZE` | 64 | |
| `CFG_TUD_CDC/MSC/HID/MIDI/AUDIO/VENDOR/NCM` | all 1 | **Every class is always compiled** |
| `CFG_TUD_CDC_RX/TX_BUFSIZE` | 512 / 512 | |
| `CFG_TUD_MSC_EP_BUFSIZE` | 4096 | SCSI read/write unit |
| `CFG_TUD_HID_EP_BUFSIZE` | 64 | Upper bound on HID endpoint MPS |
| `CFG_TUD_MIDI_RX/TX_BUFSIZE` | 512 / 512 | |
| `CFG_TUD_VENDOR_RX/TX_BUFSIZE` | 512 / 512 | |
| `CFG_TUD_NCM_IN_NTB_N` | 3 | Upstream defaults to 1, which leaves one transmit NTB, so every frame waits for the previous transfer. Upstream measures up to 50% more throughput at 2 and no "request blocked" at 3. Three 3200-byte NTBs cost ~9.6 KB |
| `CFG_TUD_NCM_OUT_NTB_N` | 2 | |

**Every class being compiled at 1** is the point worth internalising. Which
classes appear in a device is decided by **the descriptor graph**, not by
Arduino-ESP32 Kconfig. The linker drops the code for unused classes.

Audio is configured as **compile-time capacity**, not as a fixed Audio Card
topology:

| Setting | Value |
|---------|-------|
| `CFG_TUD_AUDIO_MAX_N_CHANNELS` | 2 |
| `CFG_TUD_AUDIO_MAX_N_BYTES_PER_SAMPLE` | 4 |
| Max sample rate | P4: 192000, else 96000 |
| Software buffer packets | P4: 8, else 4 |

Endpoint buffers are sized from the larger of the FS and HS maximum packets,
because **P4's HS controller may negotiate Full Speed**. Compile-time storage
covers both, and descriptor validation plus runtime checks decide which rates are
legal on the current connection.

### 2.3 DMA mode versus slave mode

DWC2 has two transfer modes, and this library uses **DMA**:

```c
#define CFG_TUD_DWC2_DMA_ENABLE 1
#define CFG_TUD_DWC2_SLAVE_ENABLE 0
```

Slave mode has the CPU push each packet into the controller's TxFIFO, refilling
from a FIFO-empty interrupt that is disarmed as soon as the last byte is
written. A sustained bulk IN stream can therefore reach **"endpoint enabled,
packets still outstanding, FIFO empty, interrupt already cleared"** - a transfer
nothing can feed again. That is what killed CDC-NCM device-to-host traffic
within seconds. DMA mode does not use that path.

**The two modes are mutually exclusive by construction, not by preference.**
`tusb_option.h` derives `CFG_TUD_EDPT_DEDICATED_HWFIFO` from
`CFG_TUD_DWC2_SLAVE_ENABLE`, and that flag decides whether the shared
`tu_edpt_stream` layer (CDC, MIDI, Vendor) hands the driver a real buffer or a
`tu_fifo`. Leaving slave mode on while the controller actually runs DMA makes
those classes call `usbd_edpt_xfer_fifo()`, whose `xfer->buffer` is NULL, so
**the endpoint DMAs from address 0 and the host receives garbage**. Enable
exactly one.

Cache coherency on P4 is upstream's problem and upstream solves it. P4 is the
only one of the three targets that reaches internal SRAM through an L1 data
cache, and `tusb_mcu.h` enables dcache maintenance **precisely when DMA is on**
(line size 64, matching `CONFIG_CACHE_L1_CACHE_LINE_SIZE`). `TUD_EPBUF_TYPE_DEF`
then aligns and pads every endpoint buffer to a whole cache line, so no DMA
buffer shares a line with anything else. S2/S3 have no such cache and need none
of it.

### 2.4 Choosing the rhport at runtime

`CFG_TUSB_RHPORT0_MODE` / `CFG_TUSB_RHPORT1_MODE` are **deliberately not
defined**. Defining one fixes `TUD_OPT_RHPORT` and would make **the P4
controller choice a build-time decision**.

Instead it is chosen at runtime:

```cpp
// EspUsbTinyUsbRuntime.cpp
g_rhport = highSpeed ? 1 : 0;
const tusb_rhport_init_t init = {
    .role  = TUSB_ROLE_DEVICE,
    .speed = highSpeed ? TUSB_SPEED_HIGH : TUSB_SPEED_FULL,
};
tusb_init(g_rhport, &init);
```

The PHY follows the same branch:

| Controller | `usb_phy_config_t.target` | `otg_speed` | rhport |
|---|---|---|---|
| FullSpeed | `USB_PHY_TARGET_INT` (internal PHY) | `USB_PHY_SPEED_FULL` | 0 |
| HighSpeed | `USB_PHY_TARGET_UTMI` (external UTMI PHY) | `USB_PHY_SPEED_HIGH` | 1 |

Requesting `HighSpeed` on anything but P4 returns `ESP_ERR_NOT_SUPPORTED` before
the PHY is created.

### 2.5 Adding a class TinyUSB does not have

The vendored TinyUSB is upstream-verbatim, so **a class TinyUSB does not
implement cannot be added to its driver table**. CCID is that case.

TinyUSB has a weak `usbd_app_driver_get_cb()` hook that `tusb_init()` reads
once. The library overrides it strongly and returns whatever table was
registered:

```cpp
// EspUsbDeviceAppDriver.cpp
extern "C" usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count)
{
  *driver_count = g_appDriverCount;
  return g_appDrivers;
}
```

**The indirection exists for footprint.** `usbd_app_driver_get_cb` is reachable
from `usbd.c` and is therefore always linked. If it named the CCID driver
directly, **every sketch would link the whole CCID driver**, used or not. Reading
a pointer instead means the driver is reachable only from the class that
registers it, and the linker drops it when no sketch instantiates that class.

Registration happens from the class's `begin()`, **before the stack starts**:

```cpp
espUsbDeviceRegisterAppDrivers(drivers, count);  // drivers must outlive the stack
```

---

## 3. Writing descriptors byte by byte

A reading guide for the raw bytes
[`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/)
prints. All multi-byte USB values are **little-endian**. Unlike the host-side
guide, the question here is **which fields you decide and which the library
decides**.

### 3.1 Device descriptor (18 bytes)

| Offset | Size | Field | Who decides |
|--------|------|-------|-------------|
| 0 | 1 | bLength | 18, fixed |
| 1 | 1 | bDescriptorType | 0x01 |
| 2 | 2 | bcdUSB | Library (0x0201 with WebUSB, otherwise 0x0200) |
| 4 | 1 | bDeviceClass | 0xef when an IAD is present, otherwise 0x00 |
| 5 | 1 | bDeviceSubClass | 0x02 when an IAD is present, otherwise 0x00 |
| 6 | 1 | bDeviceProtocol | 0x01 when an IAD is present, otherwise 0x00 |
| 7 | 1 | bMaxPacketSize0 | 64 (`CFG_TUD_ENDPOINT0_SIZE`) |
| 8 | 2 | idVendor | **`config.vid`** |
| 10 | 2 | idProduct | **`config.pid`** |
| 12 | 2 | bcdDevice | Library |
| 14–16 | 1 each | iManufacturer / iProduct / iSerialNumber | Presence of **`config.manufacturer` / `product` / `serialNumber`** |
| 17 | 1 | bNumConfigurations | 1 |

Two things are worth knowing.

**`bcdUSB` is only raised when WebUSB is enabled.** Declaring a BOS descriptor
requires USB 2.01 or newer, and adding a BOS without raising this means the host
never asks for it. The library returns 0x0201. (The WebUSB specification itself
asks for 0x0210, but hosts gate BOS retrieval on "2.01 or newer", so they do come
asking.)

**bDeviceClass follows whether an IAD was emitted.** "What this is" mostly lives
on the interfaces, and grouping several interfaces into one function is the job
of the IAD (type 0x0b) inside the configuration descriptor. CDC, NCM and Audio
emit one as part of their own descriptor, so any device containing them has one.

A device that uses IADs is required by the IAD ECN to also declare
**0xef / 0x02 / 0x01** (Miscellaneous / Common Class / Interface Association) at
device level. `buildDescriptors()` scans the configuration descriptor it just
assembled and sets those three bytes if it finds any association. **This is what
matters on Windows:** it makes usbccgp.sys load as the parent, create a child
device per association, and bind a driver per function. Without it, a device
with two CDC functions can have a single driver claim all four interfaces and
surface one port instead of two - if it works at all.

Configurations that emit no IAD (HID, MSC, MIDI, bulk Vendor) keep 0x00, because
there the class lives entirely in the interface descriptors and the device level
has nothing to add.

### 3.2 Configuration descriptor (9 bytes + what follows)

| Offset | Field | Who decides |
|--------|-------|-------------|
| 2–3 | wTotalLength | Library (704-byte ceiling) |
| 4 | bNumInterfaces | Computed from the registered classes |
| 5 | bConfigurationValue | 1 |
| 7 | bmAttributes | bit7=1 fixed, bit6=**`config.selfPowered`** |
| 8 | bMaxPower | **`config.maxPowerMilliamps` / 2** |

Interface, endpoint and class-specific descriptors are concatenated after those
9 bytes. Walking them is just "first byte is the length, second byte is the
type", repeatedly.

### 3.3 Interfaces and endpoints

Interface and endpoint numbers are **assigned in one pass, in registration
order** ([5.1](#51-numbering-rules)). They are not yours to pick.

Three fields in the 7-byte endpoint descriptor carry your design:

| Offset | Field | Notes |
|--------|-------|-------|
| 2 | bEndpointAddress | bit7=direction, bit3:0=number. Assigned by the library |
| 3 | bmAttributes | Transfer type; the class decides |
| 4 | wMaxPacketSize | Determined by speed and class ([6.2](#62-maximum-packet-size)) |
| 6 | bInterval | A **request** for a polling interval, not a guarantee ([6.1](#61-binterval-is-a-request-not-a-guarantee)) |

### 3.4 HID descriptor and report descriptor

The HID descriptor inside the configuration (type 0x21, 9 bytes) **only carries
the length of the report descriptor**. The contents are fetched separately by the
host with `GET_DESCRIPTOR(type=0x22)`.

| Offset | Field |
|--------|-------|
| 2–3 | bcdHID |
| 5 | bNumDescriptors |
| 6 | bDescriptorType (0x22) |
| 7–8 | **wDescriptorLength** (report descriptor size in bytes) |

That is where DescriptorDump reads the report descriptor length from.

### 3.5 Composite HID report-ID merging

On a composite HID (keyboard + mouse, say) the classes' report descriptors are
**merged into one**, like this:

```
[first 6 bytes of class A] [0x85 reportId_A] [rest of class A]
[first 6 bytes of class B] [0x85 reportId_B] [rest of class B]
...
```

The first six bytes are the Usage Page / Usage / Collection, and a `0x85`
(Report ID) item is inserted right after. The IDs are fixed:

| Class | Report ID |
|-------|-----------|
| Keyboard | 1 |
| Mouse | 2 |
| Gamepad | 3 |
| Consumer Control | 4 |
| System Control | 5 |
| Vendor | 6 |

So a composite HID **adds report IDs on one HID interface rather than adding
interfaces**. The endpoint is a single duplex pair on EP1 (OUT=0x01 / IN=0x81).

The ceiling is `MAX_HID_REPORT_DESCRIPTOR` = 256 bytes; past it `begin()` fails.

### 3.6 String descriptors

`index=0` is the language ID list, and this library returns exactly one, 0x0409
(en-US). `index=1,2,3` are manufacturer / product / serialNumber, and `index=4`
is the MAC address string when a network function exists.

The wire format is UTF-16LE, but **the library simply widens ASCII one byte at a
time**. The ceiling is 63 characters, past which it truncates. Non-ASCII
characters in a product name will not come out correctly as-is.

### 3.7 BOS and Microsoft OS 2.0

Generated only when `config.webusbEnabled = true`.

| Descriptor | Size | Contents |
|------------|------|----------|
| BOS | up to 57 bytes | WebUSB platform capability (landing URL) and Microsoft OS 2.0 platform capability |
| MS OS 2.0 | 162 or 178 bytes | WinUSB compatible ID and device interface GUID for the vendor interface actually allocated |

**The MS OS 2.0 descriptor is what lets Windows open the vendor interface.**
Without it a `0xff` interface sits there with no driver. APIs to replace the
vendor code, GUID or contents are not implemented.

**The two sizes are two shapes, and picking the wrong one costs you the
driver.** A descriptor set may carry the compatible ID directly under its
header, or wrap it in a configuration subset and a function subset:

```
Set header (10)                      Set header (10)
  Compatible ID (20)                   Configuration subset (8)
  Registry property (132)                Function subset (8, bFirstInterface)
= 162, "flat"                                Compatible ID (20)
                                             Registry property (132)
                                     = 178, "subsets"
```

The subsets exist to name **one function of a composite device**, and Windows
resolves them only through **usbccgp.sys** - the parent driver it loads for a
composite device and nothing else. Put a function subset on a
single-interface device and the compatible ID has no function to attach to:
`USB\MS_COMP_WINUSB` never reaches the device node, no driver matches, and
Device Manager reports `CM_PROB_FAILED_INSTALL` (code 28) **with not a line
written to `setupapi.dev.log`** - the install never starts, so there is
nothing to log. The device answers the vendor request correctly the whole
time, which is what makes this one hard to see from the Linux side, where no
driver is needed at all.

The layout therefore follows `bNumInterfaces` by default, and
`config.msOs20Layout` overrides it:

| `msOs20Layout` | Result |
|----------------|--------|
| `ESP_USB_DEVICE_MS_OS_20_AUTO` (default) | subsets when the configuration has more than one interface, flat when it has exactly one |
| `ESP_USB_DEVICE_MS_OS_20_FLAT` | always flat |
| `ESP_USB_DEVICE_MS_OS_20_SUBSETS` | always subsets |

`EspUsbDevice::microsoftOs20UsesSubsets()` reports what was actually built,
and `examples/Info/EspUsbDeviceDescriptorDump` prints the bytes.

### 3.8 Presenting a multi-function device to Windows

Linux and macOS bind drivers per function straight from the IADs in the
configuration descriptor. **Windows also reads the device descriptor**, in this
order:

1. `bDeviceClass` of **0xef / 0x02 / 0x01** makes **usbccgp.sys** (the USB
   Generic Parent Driver) load as the parent
2. usbccgp creates a child device (PDO) per IAD-delimited function
3. Each child binds the driver for its own class. For CDC ACM that is
   **usbser.sys**, one COM port per function

The library does step 1 automatically whenever it emits an IAD
([3.1](#31-device-descriptor-18-bytes)), so a device with two CDC functions
shows up as two COM ports.

The rest is design-side work:

| Do this | Why |
|---|---|
| **Name every port** (`EspUsbDeviceCdcSerial(device, "Console")`) | Windows uses the IAD's `iFunction` as the child device name. Without it you get identically named ports and no way to tell which is which |
| **Set `config.serialNumber`, unique per board** | Windows remembers COM port numbers per VID/PID/serial. With no serial, the number is reassigned whenever the device moves to a different USB port, and two identical boards collide |
| **Change the PID whenever the descriptor changes (during development)** | Windows caches driver binding per VID/PID. Keeping the same pair across a descriptor change leaves the stale binding in place, which typically looks like "only one port appears". `pnputil /enum-devices /connected` plus Device Manager's "Show hidden devices" is how you clear it |

**No INF file is needed.** Windows 10 and newer bind usbser.sys to CDC ACM
(class 0x02 / subclass 0x02) automatically. Windows 7 needed a `.inf`; treat
that as out of scope.

**Alongside a bulk Vendor interface**, that one interface is a separate story:
attaching WinUSB to it needs the MS OS 2.0 descriptor, which is emitted when
`config.webusbEnabled = true` ([3.7](#37-bos-and-microsoft-os-20)). It works
independently of the CDC ports.

The quickest check is `tests/manual/device_inspect/device_inspect.py`: it prints
the descriptors a host actually received, so `bDeviceClass=0xef`, the number of
IADs and the `iFunction` strings are all visible at once.

---

## 4. Control transfers, from the answering side

### 4.1 The 8-byte setup packet

The same table the host-side guide reads as a sender, read here as a receiver.

| Byte | Field | Contents |
|------|-------|----------|
| 0 | bmRequestType | bit7=direction (1=IN) / bit6:5=type (0:standard 1:class 2:vendor) / bit4:0=recipient (0:device 1:interface 2:endpoint) |
| 1 | bRequest | Request number |
| 2–3 | wValue | Request-specific |
| 4–5 | wIndex | Often an interface number or endpoint address |
| 6–7 | wLength | Data stage length |

### 4.2 Which request reaches which callback

Standard requests (`SET_ADDRESS`, `GET_DESCRIPTOR`, `SET_CONFIGURATION`, …) are
handled by TinyUSB's `usbd.c`, which asks the library only for the content it
needs.

| Host request | What runs |
|--------------|-----------|
| `GET_DESCRIPTOR(DEVICE)` | `tud_descriptor_device_cb()` |
| `GET_DESCRIPTOR(CONFIGURATION)` | `tud_descriptor_configuration_cb(index)` |
| `GET_DESCRIPTOR(STRING)` | `tud_descriptor_string_cb(index, langid)` |
| `GET_DESCRIPTOR(BOS)` | `tud_descriptor_bos_cb()` |
| `GET_DESCRIPTOR(DEVICE_QUALIFIER)` | `tud_descriptor_device_qualifier_cb()` |
| `GET_DESCRIPTOR(OTHER_SPEED_CONFIG)` | `tud_descriptor_other_speed_configuration_cb(index)` |
| `GET_DESCRIPTOR(HID REPORT)` | `tud_hid_descriptor_report_cb(instance)` |
| `SET_CONFIGURATION n` | `tud_mount_cb()` → each class's `onBusAttached()` |
| `SET_CONFIGURATION 0` / detach | `tud_umount_cb()` → each class's `onBusDetached()` |
| HID `SET_REPORT` | `tud_hid_set_report_cb()` → class `onHidSetReport()` |
| HID `GET_REPORT` | `tud_hid_get_report_cb()` |
| HID `SET_PROTOCOL` | `tud_hid_set_protocol_cb()` → `onHidSetProtocol()` |
| CDC `SET_LINE_CODING` | `tud_cdc_line_coding_cb()` |
| CDC `SET_CONTROL_LINE_STATE` | `tud_cdc_line_state_cb()` |
| vendor / WebUSB class and vendor requests | `tud_vendor_control_xfer_cb()` → `onControlRequest()` |
| MSC SCSI commands | the `tud_msc_*_cb()` family |

In other words: **you never write the EP0 handling, but you own every answer.**

#### Watching the requests the library answers itself

`EspUsbDeviceVendor::onControlRequest()` only ever sees what is left over. The
library takes the WebUSB and Microsoft OS 2.0 vendor codes, and the whole
standard descriptor path, before that callback runs - which is exactly the
traffic you need when a host refuses to bind a driver and you cannot tell
whether it asked at all.

`EspUsbDevice::onAnyControlRequest()` watches all of it. It is an observer:
the return value cannot change what the device answers.

```cpp
device.onAnyControlRequest([](const EspUsbDeviceControlRequestInfo &r) {
  Serial.printf("%s type=0x%02x req=0x%02x val=0x%04x idx=0x%04x len=%u -> %u%s\n",
                r.stage == ESP_USB_DEVICE_CONTROL_STAGE_SETUP ? "SETUP" : "ACK",
                r.bmRequestType, r.bRequest, r.wValue, r.wIndex, r.wLength,
                r.responseLength, r.handled ? "" : " (unhandled)");
});
```

| Field | Meaning |
|-------|---------|
| `stage` | `SETUP` for every vendor request, `ACK` for every control transfer that completed |
| `responseLength` | bytes the library handed to the stack, before it clamps them to `wLength`. Non-zero only for descriptors, the WebUSB URL and the MS OS 2.0 set |
| `handled` | at `SETUP`, the library recognised the request; at `ACK`, the transfer completed |

Vendor requests are reported at `SETUP` rather than at `ACK` on purpose: a
request the device **stalls** never reaches a status stage, and "the host asked
and we said no" is a different diagnosis from "the host never asked". So to
answer *"is Windows sending the MS OS 2.0 request?"*, look for
`type=0xC0 req=<bMS_VendorCode> idx=0x0007` in the log.

The callback runs on the usbd task, so keep it to counters or a queue - see
[1.3](#13-tinyusb-apis-are-called-from-the-usbd-task).

### 4.3 Three stages, and STALL

A control transfer is Setup → (Data) → Status. A request you cannot serve is
answered with **STALL**.

**STALL is not a fault - it is the legitimate answer for "I do not support that
request".** Hosts are written expecting it for anything beyond the standard
requests. Returning `false` from `onControlRequest()` produces a STALL.

`EspUsbDeviceVendor::onControlRequest()` receives the `stage`, so you can act per
stage. For an IN request, `sendControlResponse(request, data, length)` supplies
the data stage; for an OUT request, `sendControlResponse(request)` completes the
status stage.

```cpp
vendor.onControlRequest([](const EspUsbDeviceVendorControlRequest &r) {
  if ((r.bmRequestType & 0x80) && r.bRequest == 0x01) {
    return vendor.sendControlResponse(r, info, min<size_t>(r.wLength, sizeof(info) - 1));
  }
  return false;   // -> STALL
});
```

Never return more than `wLength`, and remember that **returning fewer bytes than
requested is legitimate** - a short packet ends the data stage.

---

## 5. Endpoint resources: numbering and buffers

### 5.1 Numbering rules

`buildDescriptors()` assigns from interface number 0 and endpoint number 1, **in
registration order**:

1. **HID first.** A composite HID gets one interface and one duplex pair on EP1
   (`0x01` / `0x81`). Separate HID classes get one interface and one IN endpoint
   each
2. Then the non-HID classes in registration order, advancing by each class's
   `interfaceCount()` and `endpointCount()`

How a class turns its assigned number into addresses varies. CDC, for example:

```cpp
epNotification = 0x80 | n;       // notification IN
epOut          = n + 1;          // data OUT
epIn           = 0x80 | (n + 1)  // data IN
```

So **CDC consumes two endpoint numbers** (notification and data are separate).
NCM has the same shape. Vendor uses one number for both directions (`n` and
`0x80 | n`).

**Always confirm the assigned numbers in DescriptorDump.** Changing the
registration order changes the numbers, which breaks any host-side script with
hard-coded endpoint addresses.

### 5.2 The controller limits in practice

`validateControllerEndpoints()` **walks the assembled descriptor** and counts
the endpoints actually emitted rather than trusting each class's declaration,
so nothing slips past.

| Controller | Endpoint number | Non-control IN | Non-control OUT |
|---|---|---|---|
| ESP32-S2 / S3 | 5 | 4 | 5 |
| ESP32-P4 rhport 0 (FS) | 6 | 4 | 6 |
| ESP32-P4 rhport 1 (HS) | 15 | 7 | 15 |

Per the note in the source, P4's rhport 0 has 7 endpoint numbers and 5 IN
endpoints including EP0; rhport 1 has 16 and 8 including EP0. The table above is
those minus EP0.

Exceeding them makes `begin()` return `ESP_ERR_INVALID_SIZE`. It fails **before
the PHY starts**, so nothing at all happens on the host.

### 5.3 Why the IN direction runs out first

On DWC2, **each IN endpoint needs its own dedicated TxFIFO**, while OUT endpoints
share the RxFIFO. The IN count is therefore tied directly to a hardware
resource, and on S2/S3 it tops out at four non-control IN endpoints.

That is why "HID + CDC + MSC" lands exactly on the limit: IN is HID 1 + CDC 2
(notification + data) + MSC 1 = 4. Nothing else fits.

Ways out, in order:

1. **Drop a class.** CDC spends an IN endpoint on notifications, so if you only
   need a byte stream, Vendor (1 IN) is cheaper
2. **Merge into a composite HID.** Keyboard + mouse + gamepad cost one IN
   endpoint total
3. **Use the ESP32-P4 HS controller.** Seven IN endpoints

### 5.3.1 Several CDC ports

`EspUsbDeviceCdcSerial` is the one class you can register more than once. The
host sees several serial ports, each with its own buffers, line coding and DTR
state.

```cpp
EspUsbDevice device;
EspUsbDeviceCdcSerial Console(device, "Console");
EspUsbDeviceCdcSerial DataLink(device, "Data Link");
```

**How many fit is decided by IN endpoints, not by a class count.** Each port
costs 2 IN:

| controller | CDC alone | Alongside HID + Vendor |
|---|---|---|
| ESP32-S2 / S3 | 2 ports (4 IN, the whole budget) | **1 port** (HID 1 + Vendor 1 + CDC 2 = 4) |
| ESP32-P4 rhport 0 (FS) | 2 ports | 1 port |
| ESP32-P4 rhport 1 (HS) | 3 ports (6 IN) | **2 ports** (1 + 1 + 4 = 6) |

`CFG_TUD_CDC` is fixed per target to exactly that ceiling (2 on S2/S3, 3 on P4):
compiling a port that could never be enumerated buys nothing. Registering more
than fits makes `begin()` fail with `ESP_ERR_INVALID_SIZE`, before the PHY is
started, so the host sees nothing.

The instance array is always allocated, so a sketch using a single port still
pays for the capacity (measured: +1416 bytes on S3, +4600 bytes on P4, where the
HS bulk endpoint buffers are 512 bytes). Define `CFG_TUD_CDC` before the library
is built to override it.

**The port index is descriptor order.** What `port()` returns is also the
TinyUSB instance the object drives. Changing registration order changes both the
port numbers and the endpoint addresses, which matters if a host-side script
hardcodes them.

**Always name the ports.** Two ACM functions are byte-for-byte the same shape,
so without names a host has no way to tell them apart. The constructor's second
argument fills in both the IAD's `iFunction` and the control interface's
`iInterface` (and, per [3.1](#31-device-descriptor-18-bytes), a configuration
with associations declares 0xef/0x02/0x01 automatically).

### 5.4 Buffer sizes

There is no device-side equivalent of the host's FIFO partitioning - `dcd_dwc2`
allocates that. What matters instead is `CFG_TUD_*_BUFSIZE`
([2.2](#22-what-owning-tusb_configh-bought)).

| Symptom | Where to look |
|---------|---------------|
| CDC dropping input | `CFG_TUD_CDC_RX_BUFSIZE` (512) |
| MSC is slow | `CFG_TUD_MSC_EP_BUFSIZE` (4096), the SCSI read/write unit |
| NCM throughput is low | `CFG_TUD_NCM_IN_NTB_N` (3) and `NET_TX_SLOTS` (4) |
| A HID report does not fit | `CFG_TUD_HID_EP_BUFSIZE` caps the HID endpoint MPS and the report size |
| `vendor.write()` keeps returning 0 | `CFG_TUD_VENDOR_TX_BUFSIZE` is the whole transmit FIFO |

**These are not fixed by the library.** Every one is behind `#ifndef`, so a
sketch raises it from its own `build_opt.h` and the flag reaches the library's
translation units too:

```c
// build_opt.h, next to the .ino
-DCFG_TUD_VENDOR_TX_BUFSIZE=16384
```

That is the thing owning `tusb_config.h` bought
([2.2](#22-what-owning-tusb_configh-bought)): the core's prebuilt TinyUSB bakes
these into a shipped `sdkconfig`, where no sketch can reach them.

The defaults already differ per target, because full speed and high speed do
not have the same problem:

| | ESP32-S2 / S3 | ESP32-P4 |
|---|---|---|
| `CFG_TUD_VENDOR_TX_BUFSIZE` | 512 | **4096** |
| `CFG_TUD_VENDOR_TX_EPSIZE` | TinyUSB's (one packet) | **4096** |
| `CFG_TUD_HID_EP_BUFSIZE` | 64 | **512** |

**The middle row is the one nobody reaches for, and it is the one that
matters.** The FIFO is how much a sketch may queue; `CFG_TUD_VENDOR_TX_EPSIZE`
is how much of that FIFO **one armed transfer carries**, and TinyUSB defaults
it to a single bulk packet. The vendor class submits one transfer per endpoint
and re-arms from the completion callback, so at one packet per transfer every
512 bytes costs a completion interrupt, an event-queue hop and a usbd task turn
- about 52 us of turnaround against 46 us of wire time. DWC2 sends several
packets per transfer perfectly happily; only that default stopped it.

Measured on ESP32-P4 rev 1.3 over usbip, 4 MiB per run, median of 9, pattern
verified on the host:

| FIFO | transfer | MB/s | global RAM |
|-----:|---------:|-----:|-----------:|
| 512 | 512 | 9.83 | 74,008 |
| 8192 | 512 | 10.76 | 81,688 |
| 8192 | 2048 | 18.64 | 83,224 |
| **4096** | **4096** | **21.12** | **81,176** |
| 8192 | 8192 | 22.81 | 89,368 |
| 8192 | 16384 | 22.78 | 97,560 |
| 32768 | 8192 | 23.34 | 113,936 |

It saturates around 8192, and 4096/4096 gets 93% of that ceiling while using
**less** RAM than raising the FIFO alone did. Raising the host's URB depth past
2 adds nothing, so ~23 MB/s is the device's ceiling on this path rather than
the host's. Together with `CFG_TUD_HID_EP_BUFSIZE` the P4 defaults cost 7168
bytes of RAM over a full-speed build, whether or not a sketch uses those
classes, because TinyUSB defines the buffers statically.

**Nothing `tu_fifo` backs may exceed 32768 bytes**, and the reason is not RAM:
`tu_edpt_stream_init()` takes the size as `uint16_t` and `tu_fifo` runs its
indices over `[0, 2*depth)`. 65536 arrives as a depth of 0 - the device
reports ready and never mounts, with nothing pointing at the flag that caused
it. The header turns that into a build error instead.

---

## 6. Transfer timing and bandwidth

### 6.1 bInterval is a request, not a guarantee

A device can ask to be polled at a given interval, but **the host decides the
actual one**.

| Speed / type | How bInterval reads |
|--------------|---------------------|
| FS interrupt | Milliseconds directly (1–255) |
| FS iso | `2^(bInterval-1)` frames |
| HS interrupt / iso | `2^(bInterval-1)` microframes (bInterval=4 → 8×125 µs = 1 ms) |

When HID feels slow, check your own bInterval first, then **measure on the
host**: `evtest` timestamps on Linux, or a `usbmon` capture, show the real
interval. No amount of asking on the device side beats host scheduling and bus
contention.

### 6.2 Maximum packet size

| Transfer | FS | HS |
|----------|----|----|
| Control | 8/16/32/64 (this library: 64) | 64 |
| Bulk | 8/16/32/64 (this library: 64) | **512, fixed** |
| Interrupt | ≤64 | ≤1024 |
| Isochronous | ≤1023 | ≤1024 |

The HS configuration descriptor is built by copying the FS one and **rewriting
every bulk endpoint's MPS to 512**, plus the interrupt endpoints of any HID
function that asked for a larger high-speed packet (`hidHighSpeedEndpointSize()`,
today only `EspUsbDeviceHidVendor`). Audio is computed separately from direction
and rate.

Per-function rather than per-descriptor-type, because the two are not the same
question: a keyboard beside a vendor HID function must keep its 8-byte endpoint
while the other goes to 512. An interrupt endpoint **reserves** its bandwidth,
so inflating one that sends 8 bytes takes the reservation from everything else
on the bus.

The HID MPS depends on the configuration:

| Configuration | HID endpoint MPS |
|---------------|------------------|
| Single HID | 8 |
| Composite HID | 16 |
| Composite HID including an NKRO keyboard | Whatever that class asks for (`CFG_TUD_HID_EP_BUFSIZE` is the ceiling) |
| `EspUsbDeviceHidVendor` | `reportSize + 1`, capped at 64 in the FS configuration and at `CFG_TUD_HID_EP_BUFSIZE` in the HS one |

An NKRO keyboard's bitmap report would be split across packets otherwise, so it
requests a larger MPS via `hidInEndpointSize()`. A composite HID's shared
endpoint takes **the maximum request among the classes it contains**.

**HID is not "the low-bandwidth class" at high speed.** That reputation is a
full-speed fact: 64 bytes every millisecond is 64 KB/s. A high-speed interrupt
endpoint carries up to 1024 bytes every 125 us, so a 511-byte report at 8,000
reports/s is about 4 MB/s - and unlike bulk, that bandwidth is reserved rather
than whatever is left over. Measured on P4: 4.03 MB/s at 7,866 reports/s with
no gaps in the sequence, which is 98% of one report per microframe. That rate
needs the *host* to keep several URBs in flight, though - one synchronous read
at a time gives about 1,100 reports/s whatever the device does. It is also the only class that needs no driver on
any host OS, which is what makes it worth the endpoint budget when Windows
driver binding is the problem you are trying to avoid.

### 6.3 Measured throughput

| | Conditions | Measured |
|---|---|---|
| ESP32-P4 HS bulk | `tests/manual/p4_hs_bulk`, 512-byte synchronous echo | the script prints MiB/s |

What `p4_hs_bulk` reports is **a health-check figure that includes a synchronous
echo per packet, not a peak-bandwidth benchmark**. The bus idles between each
send and receive, so it reads lower than a real streaming workload. To size a
design, measure again with your own transfer pattern.

Note also that flushing a transfer of exactly 512 bytes makes TinyUSB send a
terminating ZLP. The checker counts and skips those legitimate zero-byte packets
because that is **protocol-correct behaviour** ([7.2](#72-zlp)).

For a one-way stream the numbers that move are the transmit FIFO depth and the
transfer length, not the send loop: on P4 the same 4 MiB transfer runs at 9.83
MB/s with the FIFO and transfer TinyUSB defaults to, and 21.5 with the 4096/4096
this library sets there ([5.4](#54-buffer-sizes)). A sketch that streams while
it also produces the data should use `EspUsbDeviceVendor::waitWritable()` rather
than spinning on `write()` returning 0: measured at the same 21 MB/s with the
spin count going from ~25,000 per 4 MiB to zero, one block per transfer. The
gain there is not bandwidth but the CPU the spin was taking from whatever
produces the data - a two-channel capture streaming out of a P4 lost 7-16% of
its USB throughput to the spin loop and loses none to the blocking wait.

**Write in whole packets if you can.** `write()` accepts only what the FIFO has
room for, so a caller that offers the rest of its buffer whenever a little room
appears queues odd lengths - 4032 bytes, say - and TinyUSB arms a transfer as
soon as one packet's worth is present. A transfer that is not a multiple of the
packet size ends in a short packet, which completes the host's in-flight URB
early and costs it a resubmission. Waiting for room for a whole multiple, and
writing exactly that, keeps every transfer full:

```cpp
while (sent < total) {
  const size_t chunk = min(EspUsbDeviceVendor::writeCapacity(), total - sent);
  if (!vendor.waitWritable(chunk, 1000)) { break; }   // false when unplugged
  sent += vendor.write(data + sent, chunk);
}
```

It is worth doing when a producer keeps the FIFO partly full; a sketch that has
the data ready and nothing else to do will find the FIFO empty at each turn
anyway and see no short transfers either way.

---

## 7. Errors and recovery

### 7.1 Returning NAK and STALL

- **NAK** … "nothing ready right now". The host retries, so it is not an error.
  An IN endpoint with no data queued NAKs automatically.
- **STALL** … "I cannot handle that request or transfer". On control transfers it
  is a legitimate answer ([4.3](#43-three-stages-and-stall)). On a bulk or
  interrupt endpoint it halts the endpoint until the host sends
  `CLEAR_FEATURE(ENDPOINT_HALT)`.

### 7.2 ZLP

Bulk transfers end on a **short packet** (smaller than MPS). When the length is
an exact multiple of MPS, the receiver reads it as "more to come", so some
protocols need a **ZLP** (zero-length packet) as the terminator.

TinyUSB sends one when you `flush()` a transfer that is an exact multiple of MPS.
**Host-side scripts must expect that zero-byte packet.** "It only hangs at
certain sizes" and "sometimes a read returns nothing" both come from here.

### 7.3 Bus reset, suspend, deconfiguration

Device-side state is invalidated by three events:

| Event | What runs |
|-------|-----------|
| `SET_CONFIGURATION 0` | `tud_umount_cb()` → `onBusDetached()` |
| Detach (only on boards with VBUS sensing) | Same |
| `SET_CONFIGURATION n` | `tud_mount_cb()` → `onBusAttached()` |

**Why there are two hooks** matters. A bare bus reset does not reach
`onBusDetached()`. On boards without VBUS sensing - most ESP32 boards - neither
does an unplug. So **a replug goes straight to re-enumeration undetected**. The
hook that always fires is `onBusAttached()` (`SET_CONFIGURATION n`), and it
closes that gap. At mount the host believes nothing is held and no LEDs are set,
so clearing there is correct either way.

When writing your own class, **drop anything the host believes it knows in both
hooks**.

### 7.4 Sending while unmounted

Sends made while `device.ready()` (= `tud_mounted()`) is false are **dropped, not
queued**, and the send API returns `false`.

This is the most common cause of "I am sending but nothing arrives". The
introduction's Console checks `ready()` before sending and prints an explicit
error precisely because a bare `false` does not tell you why.

---

## 8. Callback context

### 8.1 Everything runs on the usbd task

**Every callback the library invokes runs on the `espusb-device` (usbd) task.**
That is a different task from `loop()`, and it runs at **the highest priority**.

| Callback | Trigger |
|----------|---------|
| `keyboard.onOutputReport()` | Host LED report |
| `keyboard.onProtocol()` | boot / report protocol switch |
| `cdc.onRx()` / `onLineCoding()` / `onLineState()` | CDC receive and control |
| `vendor.onRx()` / `onControlRequest()` | vendor bulk OUT, control requests |
| `hidVendor.onOutputReport()` / `onFeatureReport()` | HID OUT / FEATURE reports |
| MSC SCSI callbacks | Host reads and writes |
| Audio events, network frame receive | Per class |

The rules are clear:

| Safe | Not safe |
|------|----------|
| Copy data, set a flag, push to a queue | `delay()`, long loops, waiting for completion |
| Short log output | Large heap allocation, file I/O, networking |
| Update state variables | Waiting on another task's lock |

**Do not forget this task runs at the highest priority.** Waiting 100 ms here
does not just stall USB for 100 ms - no lower-priority task runs at all.

The correct shape is the same as on the host side:

```cpp
volatile bool workRequested = false;

vendor.onRx([](size_t available) {
  vendor.read(rxBuffer, min(available, sizeof(rxBuffer)));  // copy only
  workRequested = true;                                      // do the work in loop()
});

void loop() {
  if (workRequested) {
    workRequested = false;
    doSomethingExpensive();
  }
}
```

**Pointer lifetime** works the same way: the `data` handed to `onOutputReport()`
and friends points into a library buffer that is reused once you return. Copy it
if you need it later.

### 8.2 Why high-rate I/O is a polling API

The bulk data paths for Audio and Network are **polling APIs, not callbacks**.

```cpp
// Audio: read on your own schedule, not from a callback
int n = playback.available();
size_t got = playback.read(buffer, sizeof(buffer));

// Network: raw frames via sendFrame() / onFrame()
```

The reason is the flip side of 8.1. Audio at 48 kHz / 2 ch / 16-bit delivers 192
bytes every millisecond. Handing that over in a callback would run **your DSP and
I2S writes inside the usbd task**, competing with USB timing. Instead the
boundary is a FIFO, and the application reads and writes on its own schedule.

The cost is that **falling behind causes overruns and underruns**. Those are
counted, not hidden:

```cpp
EspUsbAudioStreamStats s = playback.stats();
// s.transferredBytes / s.overrunCount / s.overrunBytes
//                    / s.underrunCount / s.underrunBytes
```

A rising `overrunCount` means you are reading too slowly; a rising
`underrunCount` means you are not writing fast enough. **When audio breaks up,
read these numbers first** - they turn "it crackles sometimes" into "this many
times and this many bytes per second".

The network transmit path follows the same philosophy: when the queue is full it
drops the frame and lets TCP retransmit
([1.3](#13-tinyusb-apis-are-called-from-the-usbd-task)).

---

## 9. Implementing a new class

### 9.1 Deriving from EspUsbDeviceClass

Every class in the library derives from `EspUsbDeviceClass`. The constructor
calls `device.addClass(this)`, so **constructing the object registers it** (up to
six).

What to implement:

| Member | Role |
|--------|------|
| `configurationDescriptor(dst, interfaceNumber, endpointNumber, endpointSize)` | **Required.** Write your descriptors using the assigned numbers, return the byte count |
| `interfaceCount()` / `endpointCount()` | **Required.** How far numbering advances |
| `isHid()` / `isCdc()` / `isMsc()` / `isVendor()` / `isAudio()` / `isNet()` / `isMidi()` | Kind. `isHid()` is true by default |
| `begin()` / `end()` | Setup before the stack starts, and teardown |
| `afterDeviceStarted()` | Work that needs the stack running |
| `configurationDescriptorForSpeed(dst, capacity, …, highSpeed)` | When content varies by speed. The default calls `configurationDescriptor()` with MPS 64 or 512 |
| `hidReportDescriptor()` / `hidReportDescriptorLength()` / `hidReportId()` / `hidInEndpointSize()` | For HID classes |
| `functionName()` / `assignFunctionIds(instance, stringIndex)` | For a class that can appear more than once: the name to publish, and the instance index and string index the builder assigns in descriptor order |
| `onBusAttached()` / `onBusDetached()` | Drop state the host no longer knows ([7.3](#73-bus-reset-suspend-deconfiguration)) |

**`configurationDescriptor()` must use the numbers it was given.** Picking your
own breaks the numbering and slips past `validateControllerEndpoints()`.

### 9.2 If TinyUSB already has the class

CDC, HID, MIDI, MSC, Vendor, NCM and Audio are **already compiled in**
([2.2](#22-what-owning-tusb_configh-bought)). So a new capability is usually just
"a protocol on top of an existing class":

- Custom data with no driver → `EspUsbDeviceHidVendor` (reports up to
  `CFG_TUD_HID_EP_BUFSIZE - 1`: 63 on S2/S3, 511 on P4) or
  `EspUsbDeviceHidCustom` (your own report descriptor)
- A custom protocol that needs bandwidth → `EspUsbDeviceVendor` (bulk IN/OUT plus
  control)
- Look like a serial port → `EspUsbDeviceCdcSerial`

### 9.3 If TinyUSB does not have the class

CCID is the worked example:

1. Implement a `usbd_class_driver_t` (`init` / `reset` / `open` /
   `control_xfer_cb` / `xfer_cb` / `sof`)
2. Call `espUsbDeviceRegisterAppDrivers(drivers, count)` from the class's
   `begin()`, **before the stack starts**
3. Emit the class-specific descriptors from `configurationDescriptor()`
4. Keep the driver table valid **for as long as the stack runs** (make it static)

Do not name `usbd_app_driver_get_cb` directly
([2.5](#25-adding-a-class-tinyusb-does-not-have)). Breaking the footprint
indirection links your driver into sketches that never use the class.

### 9.4 Implementation order

1. **Build descriptors only, with `config.startTinyUsb = false`.** No hardware
   needed. Settle the byte layout first, the way `tests/single/descriptor` does
2. **Put it in DescriptorDump and check the endpoint budget**
3. **Enumerate on real hardware and cross-check with `device_inspect`**
4. **Verify one data direction at a time** (device→host first, then host→device)
5. **Run `enumeration_soak`** to see whether re-enumeration corrupts state
6. **Exercise the error paths** (sending while unmounted, unplugging, host
   suspend)

### 9.5 Implementations to read

| Goal | Reference |
|------|-----------|
| The smallest class | `EspUsbDeviceHidMouse` (one interface, one IN) |
| Multiple interfaces plus class-specific descriptors | `EspUsbDeviceCdcSerial` (IAD plus functional descriptors) |
| Bidirectional bulk plus control | `EspUsbDeviceVendor` |
| A class driver TinyUSB does not have | `EspUsbDeviceCcid` with `internal/EspUsbDeviceAppDriver.*` |
| Descriptors that vary by speed | `EspUsbAudioFunction` |
| Handing work to the usbd task | `EspUsbDeviceNet`'s TX queue |

---

## 10. Measurement and debugging

### 10.1 What to measure

| Question | Tool |
|----------|------|
| What am I declaring? | [`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/) |
| Does it fit the endpoint budget? | The same (no host needed) |
| What did the host receive? | [`device_inspect`](../tests/manual/device_inspect/) and `lsusb -v` |
| Try an arbitrary transfer | [`EspUsbDeviceConsole`](../examples/Info/EspUsbDeviceConsole/) |
| Does it survive re-enumeration? | [`enumeration_soak`](../tests/manual/enumeration_soak/) |
| Effective HS throughput | [`p4_hs_bulk`](../tests/manual/p4_hs_bulk/) |
| Audio dropouts | `stats()` overrun / underrun counters |
| Automated descriptor checks | `config.startTinyUsb = false` plus [`tests/unit/`](../tests/unit/) |
| Automated two-board tests | [`tests/peer/`](../tests/peer/) |
| Identifying a P4 port | [`tests/probe/`](../tests/probe/) |

`tests/probe/` is not a regression suite - it is **the place for throwaway
sketches used during bring-up and diagnosis**. Adding a new investigation there
is the existing convention.

### 10.2 Reading the logs

Core Debug Level `Verbose` brings out the ESP-IDF side (PHY, controller).
TinyUSB's own logging is off by default (`CFG_TUSB_DEBUG 0`).

To get TinyUSB's logs, raise `CFG_TUSB_DEBUG` to 1–3 and rebuild. **It is
verbose enough to affect USB timing itself.** For enumeration failures the host's
log (`dmesg -w`, USB Device Tree Viewer) almost always carries more information.

### 10.3 Diagnostic principles

1. **Check whether `begin()` succeeded first.** If it failed, the host is not
   involved at all
2. **Read DescriptorDump without a host attached.** If the descriptors are not
   what you meant, nothing further is worth investigating
3. **Cross-check what you emit against what the host received.** If they match,
   the problem is either below or above the descriptor layer
4. **Remove one class at a time.** Composite problems usually vanish when reduced
   to a single function - and if they do, the cause is the budget or the ordering
5. **Try another host OS.** Windows-only and macOS-only problems are common; the
   per-OS quirks are in
   [introduction section 5](usb-device-guide.md#5-observing-yourself-from-the-host-os)
6. **Change one thing at a time.** Class set, MPS and buffer sizes are separate
   experiments

---

## Related documents

- [USB Device Development Guide (introduction)](usb-device-guide.md) - fundamentals, connectors, bring-up, observing from the host
- [README.md](../README.md) - API reference and per-class status
- [third_party/tinyusb/PROVENANCE.md](../third_party/tinyusb/PROVENANCE.md) - where the vendored TinyUSB comes from and how it is verified
- [docs/DESIGN_NOTES.ja.md](DESIGN_NOTES.ja.md) - design background (Japanese)
- [docs/V2_ARCHITECTURE.ja.md](V2_ARCHITECTURE.ja.md) - v2 architecture (Japanese)
- [tests/manual/README.md](../tests/manual/README.md) - the manual test catalogue
- [tests/TEST_PLAN.md](../tests/TEST_PLAN.md) - test strategy
- [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) - the host side, with its [USB Host Development Guide (Advanced)](https://github.com/tanakamasayuki/EspUsbHost/blob/main/docs/usb-host-advanced.md)
