# USB Device Development Guide (Advanced)

> 日本語版: [usb-device-advanced.ja.md](usb-device-advanced.ja.md)

This continues the [USB Device Development Guide](usb-device-guide.md).
The guide covers getting the host to recognise the device. This document covers the mechanisms shared by every class:
why things behave the way they do, where the limits are, and what to look at when you hit one.

It is written for readers who:

- want to read or check descriptors byte by byte
- do not fit the endpoint or FIFO budget, see transfers stop, or get less throughput than expected
- need to know exactly what is allowed inside a callback
- want zero-copy borrowed transfers (`readDirect`/`writeDirect`)
- want to understand PSRAM, the cache, and the difference between HS and FS

Per-class usage is left to the class documents ([HID](classes/hid.md), [CDC](classes/cdc.md),
[Vendor](classes/vendor.md), [MIDI](classes/midi.md), [MSC](classes/msc.md), [Audio](classes/audio.md),
[Video](classes/video.md), [NCM](classes/ncm.md), [CCID](classes/ccid.md), [DFU](classes/dfu.md)).

This document covers v3. Differences from v2 are summarised in [section 14](#14-differences-from-v2).

## Contents

1. [Layers and dependencies](#1-layers-and-dependencies)
2. [Task model and callback context](#2-task-model-and-callback-context)
3. [Start and stop](#3-start-and-stop)
4. [Descriptor generation](#4-descriptor-generation)
5. [Control transfers](#5-control-transfers)
6. [Endpoint numbers and FIFOs](#6-endpoint-numbers-and-fifos)
7. [HS and FS](#7-hs-and-fs)
8. [Cache and DMA](#8-cache-and-dma)
9. [Copied and borrowed transfers](#9-copied-and-borrowed-transfers)
10. [Using PSRAM](#10-using-psram)
11. [Suspend and remote wakeup](#11-suspend-and-remote-wakeup)
12. [Adding your own Function](#12-adding-your-own-function)
13. [Measurement and debugging](#13-measurement-and-debugging)
14. [Differences from v2](#14-differences-from-v2)
15. [What has been verified](#15-what-has-been-verified)

Terms:

| Term | Meaning |
| --- | --- |
| Device | `EspUsbDevice`, the whole USB device. Only one can run at a time |
| Function | A class object such as `EspUsbDeviceCdcSerial`, registered with a Device |
| MPS | wMaxPacketSize, the largest packet on an endpoint |
| FS / HS | Full Speed (12 Mbps) / High Speed (480 Mbps) |
| ZLP | Zero Length Packet |
| USB worker | The FreeRTOS task the library creates. It handles USB events and class processing |
| Borrowed transfer | A transfer that hands the application's buffer to DMA without copying it |

---

## 1. Layers and dependencies

### 1.1 Layers

```text
sketch (setup / loop / your own tasks)
  ↓ configure(), begin(), transfer APIs, callback registration
EspUsbDevice and Functions          … configuration, descriptor generation, per-class rings and state
  ↓ class driver table (only the selected classes)
device core (EspUsbDeviceCore)      … EP0, standard requests, event queue, generations
  ↓ controller boundary (EspUsbController.h)
DWC2 driver (EspUsbDwc2*)           … registers, FIFOs, DMA, interrupts
  ↓ ESP-IDF: usb_phy, esp_intr, esp_cache, heap_caps, FreeRTOS
USB OTG controller and PHY
```

| Layer | Main files | Role |
| --- | --- | --- |
| Device / Function | `src/EspUsbDevice.h`, `src/*.cpp` | Public API. Configuration checks, descriptor generation, class processing |
| runtime | `src/internal/EspUsbRuntime.*` | Acquires the PHY, selects the controller, starts and stops the USB worker |
| device core | `src/internal/EspUsbDeviceCore.*` | EP0, standard requests, event queue, bus and configuration generations |
| controller boundary | `src/internal/EspUsbController.h` | Typed entry points between the core and DWC2 |
| DWC2 | `src/internal/EspUsbDwc2Device.c`, `EspUsbDwc2Phy.c`, `EspUsbDwc2Registers.h`, `EspUsbDwc2Hardware.h` | Endpoints, FIFOs, DMA, interrupts |
| platform | `src/internal/EspUsbDwc2Platform.*` | Per-SoC port table, IRQ registration, cache synchronisation |
| wire types | `src/internal/EspUsbWire.h` | Layout of SETUP (8 bytes), interface (9 bytes) and endpoint (7 bytes) |

`src/internal/` is not a user API. Names and contents can change without notice.

### 1.2 Relationship with TinyUSB

v3 uses neither TinyUSB's device stack (`usbd.c`) nor its class drivers.
EP0, standard requests and class processing are handled by this library's core and Functions.

Only the DWC2 driver is derived from TinyUSB: it is a maintained fork of TinyUSB's DWC2 driver.
It is not a clean-room rewrite.
This library owns the source and fixes it. Upstream changes are reviewed and then carried over by hand.
Nothing replaces the snapshot automatically.
Origin, licences and the maintenance method are in [PROVENANCE.md](../third_party/tinyusb/PROVENANCE.md).

| Item | State in v3 |
| --- | --- |
| TinyUSB device stack / class drivers | Not used and not built |
| DWC2 driver | Maintained fork of TinyUSB's (MIT; register definitions derived from ST, BSD-3-Clause) |
| `tusb_config.h`, `CFG_TUD_*`, `CFG_TUSB_*` | Gone. Defining an old flag is a compile error (`EspUsbLegacyFlags.h`), even with the value 0 |
| TinyUSB bundled with Arduino-ESP32 | Not used |
| Code that depends on TinyUSB internals (`tud_*` and so on) | Does not work with v3 |

This command checks that the maintained files are present and that the old vendored paths are gone.
It is a file check; it does not prove USB correctness.

```sh
python3 tools/verify_usb_backend.py
```

### 1.3 What comes from ESP-IDF

| Used | For |
| --- | --- |
| `esp_private/usb_phy.h` (`usb_new_phy`) | Acquiring and releasing the PHY |
| `esp_intr_alloc` | Registering the controller interrupt |
| `esp_cache_msync` | Cache synchronisation on P4 |
| `heap_caps_aligned_alloc` | Allocating DMA-capable internal RAM |
| FreeRTOS | USB worker, event queue, critical sections |

The Arduino-ESP32 USB integration (`USB.h`, `USBCDC` and so on) is not used. One controller cannot run two USB stacks at once.

---

## 2. Task model and callback context

### 2.1 The USB worker

When `begin()` starts USB, it creates one FreeRTOS task: the USB worker.

| Item | Default | Setting |
| --- | --- | --- |
| Task name | `espusb-device` | Fixed |
| Stack | 4096 bytes | `EspUsbDeviceConfig::taskStackBytes` |
| Priority | `configMAX_PRIORITIES - 1` (highest) | `taskPriority` (-1 for the default) |
| Core | Not pinned | `taskCoreId` (-1 for the default) |
| Event queue depth | 16 | `eventQueueDepth` |

The worker repeats these steps:

1. Wait up to 1 ms (at least 1 tick) on the event queue.
2. Handle up to `eventQueueDepth` events. While a bus reset is being processed, keep going until it is done.
3. Once reset handling is complete, run the internal jobs that Functions posted (NCM transmission, for example).

No polling call such as `device.task()` is needed; v3 does not have one.

### 2.2 What the ISR does

The controller interrupt handler (ISR) does as little as possible:

- Reads registers, builds events and queues them.
- At the start of a bus reset (USBRST), advances the core's generation and invalidates the configuration.
- Updates the suspend/resume state.
- When an ISO OUT misses its target frame, re-arms it with the opposite parity on the same buffer ([8.4](#84-missed-iso-out-frames)).

Class processing and user callbacks never run in the ISR.
None of the current classes uses SOF processing, which would run in the ISR.

### 2.3 Where callbacks run

Every user callback the library calls runs on the USB worker.

| Example callbacks | Trigger |
| --- | --- |
| `onDirectTxComplete` / `onDirectRxComplete` | Completion, cancellation, reset or stop of a borrowed transfer |
| `onRx` / `onRxData` / `onTxComplete` | Bulk reception, completion of copied transmission |
| `onControlRequest` (Vendor) | SETUP/DATA/ACK of a vendor request |
| `onLineCoding` / `onLineState` / `onBreak` (CDC) | CDC control requests |
| HID `onOutputReport` and similar | OUT/FEATURE reports and protocol changes from the host |
| MSC `onRead` / `onWrite` and similar | SCSI commands |
| `onAnyControlRequest` (Device) | Observation of every control request |
| Audio, Video, NCM, CCID and DFU callbacks | Class-specific triggers |

The worker runs at the highest priority. Time spent in a callback stops USB responses for that long, and lower-priority tasks do not run.

| Fine inside a callback | Not allowed inside a callback |
| --- | --- |
| Copying data, setting flags, queueing, notifying another task | `delay()`, long loops, waiting for completion or a timeout |
| Submitting the next prepared buffer (`writeDirect`/`readDirect`) | `device.end()`, `begin()`, `configure()`, registering callbacks |
| Short logging | Destroying a Function, large heap allocations, file I/O |

- Pointers passed to a callback (`onRxData` data, HID reports and so on) are valid only during the callback. Copy what you keep.
- The library never calls a user callback while holding an internal lock.
- `end()` called from the worker is rejected with `ESP_ERR_INVALID_STATE`.
- If a callback keeps blocking, `end()` returns `ESP_ERR_TIMEOUT` after `stopTimeoutMs` ([3.2](#32-the-order-inside-end)).

### 2.4 What may be called from where

| Caller | What it may call |
| --- | --- |
| An ordinary task (`setup()`, `loop()`, your own task) | Every public API. `begin()`/`end()`/`configure()`/callback registration belong here |
| USB worker (inside a callback) | Transfer APIs without waiting. Submitting and cancelling borrowed transfers, `remoteWakeup()`. CDC `write()` returns at once with what it accepted. HID `sendHidReport` with `timeoutMs = 0` |
| ISR | No public API. Hand over to a task, for example with a task notification |

- Calls from an ISR are not supported. `remoteWakeup()` returns `ESP_ERR_INVALID_STATE`, `remoteWakeupEnabled()` returns false and CDC `write()` returns 0. Other APIs give no guarantee.
- Keep configuration and lifecycle calls on one Device (`configure()`, `begin()`, `end()`, callback registration) on a single task. The library does not serialise them against each other.
- A submitting API never calls the completion callback itself. But when the worker runs on another core, the completion callback can run before the submitting call returns.
  Prepare whatever the callback uses before submitting, and do not rely on the returned `id` having been stored yet.

### 2.5 The event queue and overflow

Events built by the ISR reach the worker through a queue of `eventQueueDepth` entries.
One extra entry is reserved for hardware events on top of that.

If the worker stalls for long, the queue overflows. The library does not silently drop events,
because dropping a completion event would strand a borrowed buffer forever. It recovers like this:

1. Stop interrupts, disconnect from the host and stop DMA on every endpoint.
2. Reset every class and return borrowed buffers (`BusReset`). `onBusDetached` is called as well.
3. Discard the old events left in the queue.
4. Wait about 20 ms and reconnect. The host enumerates again.

To the application this looks like an unplug followed by a replug.
If it happens, a callback is too heavy or the worker priority is too low. Raising `eventQueueDepth` is the last resort.

### 2.6 Worker settings

```cpp
EspUsbDeviceConfig config;
config.taskStackBytes = 8192;   // raise it if callbacks use printf or large locals
config.taskPriority = -1;       // default: highest
config.taskCoreId = 0;          // default: -1 (not pinned)
config.eventQueueDepth = 16;    // default: 16; 0 is rejected
config.stopTimeoutMs = 5000;    // default: 5000; how long end() waits for the worker; 0 waits without a limit
if (!device.begin(config)) Serial.println(device.lastErrorName());
```

| Setting | Constraint | On violation |
| --- | --- | --- |
| `taskStackBytes` | In bytes. At least FreeRTOS's minimum | `ESP_ERR_INVALID_SIZE` |
| `taskPriority` | -1, or 0 to `configMAX_PRIORITIES - 1` | `ESP_ERR_INVALID_ARG` |
| `taskCoreId` | -1, or an existing core (only 0 on a single-core chip) | `ESP_ERR_INVALID_ARG` |
| `eventQueueDepth` | 1 or more (`uint16_t`) | `ESP_ERR_INVALID_SIZE`; too large gives `ESP_ERR_NO_MEM` |

- The checks run before the PHY is acquired.
- User callbacks run on the worker's stack. If you shrink the stack or lower the priority, account for the cost of your callbacks.
- With a lower priority, other tasks delay USB responses. That causes queue overflow and re-enumeration ([2.5](#25-the-event-queue-and-overflow)).
- Pinning to a core can help when the worker competes with a heavy task on the same core. The effect depends on the setup; measure before deciding.
- The settings are read at `begin()`. To change them, pass new values to the next `begin()` after `end()`.
- `DescriptorsOnly` creates no worker, so these values are not used (nor range-checked).

---

## 3. Start and stop

### 3.1 The order inside begin()

`begin()` finishes every check and allocation before it finally connects to the host.
If any step fails, everything acquired so far is undone in reverse order and `begin()` returns `false`. `lastError()` gives the reason.

| Step | What happens | Typical failures |
| --- | --- | --- |
| 1. State checks | Not already started; no other Device running; no Function left with a rejected registration; at most 251 Functions; valid `startMode` | `INVALID_STATE`, `INVALID_SIZE`, `INVALID_ARG` |
| 2. Setting checks | Format of `deviceInterfaceGuid` | `INVALID_ARG` |
| 3. Function table | Allocates the registration-order pointer table at its exact size | `NO_MEM` |
| 4. Descriptor generation | Per-Function setting checks, number assignment, configuration (FS, HS), strings, BOS/MS OS 2.0, endpoint limits ([section 4](#4-descriptor-generation)) | `INVALID_SIZE`, `INVALID_ARG`, `NO_MEM` |
| 5. FIFO budget | Bulk IN double buffering ([6.4](#64-bulk-in-double-buffering)); the transmit FIFO budget for every IN endpoint ([6.5](#65-transmit-fifo-budget)) | `INVALID_SIZE` |
| 6. Function start | Each Function's start step in registration order: rings, DMA buffers, class driver registration | Per-Function reasons |
| 7. USB start | Check worker settings → acquire the PHY → configure the controller → start the core → create the worker | `INVALID_ARG`, `INVALID_SIZE`, `NOT_SUPPORTED`, `NO_MEM`, `FAIL` |
| 8. After start | Each Function's after-start step | `FAIL` |

A Function's start and stop steps (`begin()`, `afterDeviceStarted()`, `prepareToStop()`) are protected. Only `device.begin()` / `device.end()` call them. Calling one from a sketch, such as `console.begin()`, is a compile error.

"Start the core" in step 7 proceeds as follows:

1. Count interfaces, endpoint numbers and directions in the selected configuration.
2. Allocate the core state, endpoint table, interface table, event queue, 64 bytes for EP0 and the DWC2 transfer records as **one internal DMA region**.
3. Synchronise the cache for the EP0 SETUP area (P4). On failure, `ESP_FAIL`.
4. Initialise the class drivers, then the controller.
5. Register and enable the interrupt. If registration fails, `ESP_FAIL`.

If steps 1 to 6 fail, the PHY, interrupt and worker never run, so nothing at all happens on the host side.
When "plugging it in does nothing", look at the return value of `begin()` and `lastErrorName()` first.

A successful `begin()` does not mean enumeration succeeded. Use `ready()` to see whether the host has configured the device.

### 3.2 The order inside end()

`end()` closes new submissions, stops DMA, returns buffers and releases resources.

1. If called from the worker, reject with `ESP_ERR_INVALID_STATE`.
2. Run each Function's stop preparation, which stops accepting new submissions.
3. Tell the worker to stop and wait up to `config.stopTimeoutMs` (default 5000 ms; 0 waits without a limit) for it to exit. Before exiting, the worker:
   1. stops the interrupt and disconnects from the host;
   2. stops the controller and any DMA in progress;
   3. shuts down each class driver, returning borrowed buffers with `Stopped` (those callbacks also run on the worker);
   4. calls `onBusDetached` if the device was configured;
   5. releases the interrupt registration and the core region.
4. Release the PHY.
5. Call each Function's `end()` in reverse registration order, which frees rings and so on.
6. Release the descriptors and the Function table.

Once `end()` returns `ESP_OK`, no callback or DMA touches any buffer again. Buffers and Functions may then be destroyed.

- If a callback blocks and the worker does not exit in time, `end()` returns `ESP_ERR_TIMEOUT` and skips steps 4 onward. The stop request stays in place, so calling `end()` again after the callback returns completes the stop.
- The Device and Function destructors wait for the stop to complete without a limit, so the worker never keeps using memory that has been destroyed.
- After `end()` the same objects can be reconfigured with `configure()` and started again with `begin()`.
- Destroying a Function while running makes its destructor stop the Device. Normally the order is `end()` → destroy Functions → destroy the Device.

### 3.3 Resource allocation and release

| Resource | Allocated | Released | Where |
| --- | --- | --- | --- |
| Function table | `begin()` (or a descriptor query while stopped) | `end()`, failed start | Heap |
| Descriptors (configuration, HID report, BOS, MS OS 2.0, strings, WebUSB URL) | `begin()`; only what is used, at its real length | `end()`, failed start | Heap |
| Class rings | Each Function's start (inside `device.begin()`); a buffer you pass as `rxBuffer` and so on is borrowed instead | `end()` | Heap when automatic; user buffers may be in PSRAM |
| Class DMA buffers, copied-TX staging | Each Function's start (inside `device.begin()`) | `end()` | Internal RAM (DMA-capable, 64-byte aligned) |
| Core region (queue, EP0, DWC2 records) | USB start | `end()` | Internal RAM (DMA-capable, one region) |
| PHY, interrupt, worker | USB start | `end()` | ESP-IDF |

- Nothing is allocated from the heap during a transfer, a cancellation or queue-overflow recovery.
- DWC2 transfer records are created only for the endpoint directions that appear in any alternate setting, plus the two EP0 directions.
- A `begin()` with no registered Function does not start the controller or allocate the core region. `started()` is then true and `ready()` false.

### 3.4 State queries

| Function | True when |
| --- | --- |
| `started()` | From a successful `begin()` until `end()`. Resources are held. Also true while waiting for a connection or during a reset |
| `ready()` | The host has completed SET_CONFIGURATION |
| `suspended()` | The bus is suspended. `ready()` stays true while suspended |

`speed()` returns the speed the host chose at its last bus reset (`EspUsbBusSpeed::Full` / `High`; USB 2.0 §7.1.7.5). It is `None` before USB starts, in `DescriptorsOnly`, after `end()`, and before the first bus reset. An HS controller reports `Full` behind an FS host or hub.

The bus can change between a query and your next call. `ready() && !suspended()` does not promise that the next transfer will succeed.
Judge transfers by each API's return value and completion notification.

### 3.5 Checking a configuration with DescriptorsOnly

With `startMode = DescriptorsOnly`, `begin()` runs without the PHY, interrupt or worker.
No host or cable is needed, which makes it suitable for automated tests of your configuration.

```cpp
EspUsbDeviceConfig config;
config.startMode = EspUsbDeviceStartMode::DescriptorsOnly;
if (!device.begin(config)) {
  Serial.println(device.lastErrorName());   // configuration mistakes show up here
  return;
}
const uint8_t *fs = device.configurationDescriptorForSpeed(0, false);
const uint8_t *hs = device.configurationDescriptorForSpeed(0, true);
// Read or copy the bytes here. The pointers stay valid until end()
device.end();
```

| Checked | Not checked |
| --- | --- |
| Function settings, descriptor lengths and contents, strings and URL | PHY, interrupts, DMA, cache behaviour |
| Endpoint numbers and the number of non-control IN endpoints | Enumeration with a host, standard request handling |
| Bulk IN double buffering and the transmit FIFO budget | Worker settings (stack, priority, core, queue) |
| Allocation of each Function's rings/DMA buffers | Actual transfers |

- On P4 (`controller = Auto`) the check uses the HS controller's budget.
- On S2/S3 the HS descriptors can be queried as well (`configurationDescriptorForSpeed(0, true)`). The budget stays that of the physical FS controller.
- While USB is running on an FS-only chip, an HS query returns `nullptr` and `lastError()` is `ESP_ERR_NOT_SUPPORTED`.
- A descriptor query while stopped may rebuild the descriptors. Do not keep a pointer across another query or a configuration change.

[`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/) prints every descriptor this way.

---

## 4. Descriptor generation

Every multi-byte USB value is little-endian. This section shows which fields you decide and which the library decides.
The reference is USB 2.0 §9.6 (Standard USB Descriptor Definitions).

### 4.1 Function registration and number assignment

A Function joins the Device's registration list in its constructor. Registration uses no heap.

- Registration order is preserved. Up to 251 Functions are allowed (string indices 5 to 255).
- Registration while running is rejected with `ESP_ERR_INVALID_STATE`. While a rejected Function remains, the next `begin()` is also rejected.
  Register it again with `device.addClass(&function)` after stopping, or recreate it.
- The Device does not own its Functions, and must outlive them. Neither Devices nor Functions can be copied or moved.

`begin()` assigns interface and endpoint numbers by these rules. You cannot choose them by hand.

1. Interface numbers start at 0, endpoint numbers at 1.
2. HID comes first. Two or more HID Functions are merged into one HID interface (interface 0, IN and OUT on endpoint 1) ([4.5](#45-merged-hid-reports)).
3. The other Functions follow in registration order.
4. Each Function receives `interfaceCount()` interface numbers and `endpointCount()` consecutive endpoint numbers. IN and OUT may share a number.

| Function | Interfaces | Endpoint numbers | Non-control IN |
| --- | --- | --- | --- |
| HID (one) | 1 | 1 | 1 |
| HID (two or more, merged) | 1 | 1 | 1 |
| CDC ACM | 2 | 2 (notification IN, data IN/OUT) | 2 |
| Vendor | 1 | 1 (IN/OUT) | 1 |
| MIDI | 2 | 1 (IN/OUT) | 1 |
| MSC | 1 | 1 (IN/OUT) | 1 |
| NCM | 2 | 2 (notification IN, data IN/OUT) | 2 |
| CCID | 1 | 1 (IN/OUT); 2 when `removable` (adds a notification IN) | 1 or 2 |
| Video | 2 | 1 | 1 |
| DFU | 1 | 0 | 0 |
| Audio | Depends on the configuration | Depends | Depends |

Changing the registration order changes the numbers. Host programs should read endpoint addresses from the descriptors rather than hard-code them.

### 4.2 Device descriptor (18 bytes)

| Offset | Field | Value / decided by |
| --- | --- | --- |
| 0 | bLength | 18 |
| 1 | bDescriptorType | 0x01 |
| 2 | bcdUSB | 0x0200; 0x0201 when a BOS is present |
| 4 | bDeviceClass | 0xEF with an IAD, otherwise 0x00 |
| 5 | bDeviceSubClass | 0x02 with an IAD, otherwise 0x00 |
| 6 | bDeviceProtocol | 0x01 with an IAD, otherwise 0x00 |
| 7 | bMaxPacketSize0 | 64 (FS and HS) |
| 8 | idVendor | `config.vid` (default 0x303A) |
| 10 | idProduct | `config.pid` (default 0x4000) |
| 12 | bcdDevice | `config.deviceVersion` (default 0x0100) |
| 14 | iManufacturer | 1 if `config.manufacturer` is set |
| 15 | iProduct | 2 if `config.product` is set |
| 16 | iSerialNumber | 3 if `config.serialNumber` is set |
| 17 | bNumConfigurations | 1 |

- **bcdUSB becomes 0x0201 only when a BOS is present.** A host requests the BOS only when bcdUSB is 0x0201 or higher.
  0x0210 would claim USB 2.1 features that are not implemented, so it is not used.
- **bDeviceClass follows the presence of an IAD.** If the configuration contains any IAD (type 0x0B), the device declares 0xEF/0x02/0x01.
  That is required by the USB-IF Interface Association Descriptors ECN.
  CDC, NCM, Audio and Video emit IADs. A configuration of only HID, MSC, Vendor, MIDI, CCID and DFU keeps 0x00.
- The default VID/PID are for prototypes. Use your own for a product (see the [guide](usb-device-guide.md)).

### 4.3 Configuration descriptor (9 bytes plus the rest)

| Offset | Field | Value / decided by |
| --- | --- | --- |
| 2 | wTotalLength | The real length (limited only by USB's 16 bits) |
| 4 | bNumInterfaces | Number of assigned interfaces |
| 5 | bConfigurationValue | 1 |
| 6 | iConfiguration | 0 |
| 7 | bmAttributes | bit 7 = 1, bit 6 = `config.selfPowered`, bit 5 = `config.remoteWakeupSupported` |
| 8 | bMaxPower | `config.maxPowerMilliamps` rounded up to 2 mA units, at most 250 (500 mA) |

IADs, interfaces, endpoints and class-specific descriptors follow.
Walking them is just "first byte is the length, second byte is the type", repeated.

v3 measures the length first, allocates exactly that much and then writes. There is no fixed limit (v2 had 704 bytes).
Measuring and writing are driven by the same configuration; if they disagree, `begin()` rejects it with `ESP_ERR_INVALID_SIZE`.

### 4.4 Interfaces and endpoints

The fields of the endpoint descriptor (7 bytes) that matter for a design:

| Offset | Field | Notes |
| --- | --- | --- |
| 2 | bEndpointAddress | bit 7 = direction (1 = IN), bits 3..0 = number. Assigned by the library |
| 3 | bmAttributes | Transfer type. Decided by the class |
| 4 | wMaxPacketSize | Decided by speed and class ([7.1](#71-maximum-packet-sizes)) |
| 6 | bInterval | A requested polling interval, not a guarantee |

bInterval follows USB 2.0 §9.6.6.

| Speed / type | Meaning of bInterval | Accepted range |
| --- | --- | --- |
| FS interrupt | Milliseconds as is | 1–255 |
| FS isochronous | 2^(bInterval-1) frames | 1–16 |
| HS interrupt / isochronous | 2^(bInterval-1) microframes (125 µs) | 1–16 |
| Bulk | Unused | Ignored |

Out-of-range values are rejected before the endpoint is opened (before any FIFO is allocated).
The host decides the actual polling interval. A smaller value cannot beat the host's scheduling or a busy bus.

### 4.5 Merged HID reports

With two or more HID Functions, they are merged into one HID interface.
The number of interfaces does not grow; the number of report IDs does. There is one IN and one OUT endpoint (endpoint 1).

The report descriptor concatenates each Function's descriptor like this:

```text
[Push] [Function items … Report ID (0x85, id) right after the first Collection … the rest] [Pop]
```

- Push/Pop keep one Function's global items (units, ranges, report sizes) from leaking into the next.
- Report IDs are assigned by first reserving each Function's preferred ID (`hidReportId()`), then giving free IDs to duplicates and to Functions without a preference.
  They are not fixed per type as in v2. `compositeReportId()` returns the assigned ID.
- The merged descriptor has no fixed length limit (up to USB's 16 bits).
- The merged HID endpoint's MPS defaults to 16 bytes and takes the largest size any Function asks for. The `inputReportSize` of `EspUsbDeviceHidCustom` and the report length of `EspUsbDeviceHidVendor` count too (up to 64 bytes at FS, 1024 at HS).
- Each HID Function's `inputPacketSize()` reads the MPS that applies. It reflects the speed the host chose, and is 0 before the bus reset and after `end()`.

The HID descriptor (type 0x21, 9 bytes) carries only the report descriptor's length (wDescriptorLength, offsets 7–8).
The host reads the contents separately with `GET_DESCRIPTOR(0x22)`. See [HID](classes/hid.md) for details.

### 4.6 String descriptors

| Index | Contents |
| --- | --- |
| 0 | Language ID: 0x0409 only |
| 1–3 | `config.manufacturer` / `product` / `serialNumber` |
| 4 | Reserved |
| 5– | Function names, one per Function in registration order |
| After those | Function-specific auxiliary strings (such as the NCM MAC address) |

- Input is NUL-terminated UTF-8. `begin()` converts it to UTF-16LE and copies it. After success, changing the source array does not change the response.
- One string may hold up to 126 UTF-16 code units (254 bytes with the header). Longer gives `ESP_ERR_INVALID_SIZE`.
- Invalid UTF-8 (overlong forms, surrogates, values above U+10FFFF, truncated sequences) gives `ESP_ERR_INVALID_ARG`. Nothing is replaced or truncated.
- A `nullptr` item is omitted (index 0). An empty string becomes a zero-length string descriptor. An empty Function name is omitted.
- If every string is omitted, no heap is used for strings.
- Requests for a LANGID other than 0x0409 are answered with STALL.

### 4.7 BOS, WebUSB and Microsoft OS 2.0

A BOS is emitted only for WebUSB or Microsoft OS 2.0. Without either, the pointer and length are 0.

| Condition | BOS contents |
| --- | --- |
| `config.webusbEnabled = true` | WebUSB platform capability (24 bytes) |
| A Vendor or DFU Function is present | Microsoft OS 2.0 platform capability (28 bytes) |

The BOS is 5 bytes plus those (29, 33 or 57 bytes).

The **Microsoft OS 2.0 descriptor set** lets Windows bind WinUSB without an .inf file.
It is built when there is a Vendor or DFU interface.

| Element | Contents |
| --- | --- |
| Compatible ID | `WINUSB`, one per Vendor/DFU interface |
| Registry property | `DeviceInterfaceGUIDs`, on Vendor interfaces only. The value is `config.deviceInterfaceGuid` |
| Vendor revision | `config.msOs20VendorRevision`; 0 (default) derives it from a CRC over the whole set |
| CCGP device | With `config.msOs20CcgpDevice = true`, Windows treats the device as composite |

The set has two shapes:

```text
flat (one interface)                subsets (two or more interfaces)
Set header (10)                    Set header (10)
  Vendor revision (6)                Configuration subset (8)
  Compatible ID (20)                   Function subset (8, bFirstInterface)
  Registry property (132)                Vendor revision (6)
                                         Compatible ID (20)
                                         Registry property (132)
                                       Function subset (8, bFirstInterface)
                                         …
```

| `msOs20Layout` | Result |
| --- | --- |
| `ESP_USB_DEVICE_MS_OS_20_AUTO` (default) | Subsets with two or more interfaces or with `msOs20CcgpDevice`; flat otherwise |
| `ESP_USB_DEVICE_MS_OS_20_FLAT` | Always flat. With two or more WinUSB targets, `begin()` fails with `ESP_ERR_INVALID_ARG` |
| `ESP_USB_DEVICE_MS_OS_20_SUBSETS` | Always subsets |

The wrong shape means no driver binds. The reasons:

- A function subset names one function of a composite device. Windows resolves it only through `usbccgp.sys`, which it loads only for composite devices.
- On a single-interface device a subset leaves the compatible ID with nothing to attach to. No driver matches and Device Manager shows code 28.
- On a composite device, interfaces the set does not name stay without a driver.

**`deviceInterfaceGuid` is the GUID a host application uses to find the device.** Which driver binds is decided by the compatible ID alone.
The default is shared by every Vendor interface built with this library, so give your product its own.
The format is `{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}` (38 characters including the braces). Anything else makes `begin()` fail with `ESP_ERR_INVALID_ARG`.

**The vendor revision is what makes Windows re-read the set.** Windows remembers the registry properties from the first enumeration per VID/PID/serial and re-reads them only when this value changes.
By default it is derived from the set itself, so changing the GUID or the Functions changes it automatically. `microsoftOs20VendorRevision()` returns the value in use.

**WebUSB** behaviour:

| `webusbUrl` | Behaviour |
| --- | --- |
| `nullptr` | No landing page (iLandingPage = 0) |
| `https://…` | Scheme 1; stored without the prefix |
| `http://…` | Scheme 0; stored without the prefix |
| Anything else | Scheme 255; stored in full |

Up to 252 bytes of UTF-8 after the prefix. Empty, prefix-only or invalid UTF-8 gives `ESP_ERR_INVALID_ARG`; too long gives `ESP_ERR_INVALID_SIZE`.

**Vendor codes the library uses:**

| bRequest | Purpose | Note |
| --- | --- | --- |
| 0x01 | WebUSB (bVendorCode) | With `webusbEnabled`, the library takes only GET_URL (bmRequestType 0xC0, wIndex 2). A wValue other than URL index 1 stalls |
| 0x02 | Microsoft OS 2.0 (bMS_VendorCode) | When a set exists, the library answers bmRequestType 0xC0, wValue 0, wIndex 7 (MS_OS_20_DESCRIPTOR_INDEX) |

Requests that do not match these combinations reach Vendor `onControlRequest`, even with the same bRequest. The combinations themselves never reach it.

### 4.8 Device Qualifier and Other Speed Configuration

This follows USB 2.0 §9.6.2 and §9.6.4.

| Chip / setting | Device Qualifier | Other Speed Configuration |
| --- | --- | --- |
| S2 / S3 (FS only) | Not returned (STALL) | Not returned (STALL) |
| P4, `controller = FullSpeed` | Not returned (STALL) | Not returned (STALL) |
| P4, `controller = Auto` / `HighSpeed` | 10 bytes | The configuration for the speed not in use, with its type changed to 0x07 |

- The specification requires a full-speed-only device to answer a Device Qualifier request with a request error (STALL) (§9.6.2).
- On an HS-capable controller, `begin()` builds both the FS and HS configurations. It also allocates the Other Speed buffer (the larger of the two) up front.
- The Device Qualifier matches the device descriptor's bcdUSB, class codes, bMaxPacketSize0 (64) and bNumConfigurations (1).

### 4.9 How Windows sees a multi-function device

Linux and macOS bind drivers per function by looking at the IADs in the configuration. Windows also looks at the device descriptor.

1. If bDeviceClass is 0xEF/0x02/0x01, `usbccgp.sys` loads as the parent.
2. usbccgp creates a child device for each IAD.
3. Each child gets its class driver. For CDC ACM that is `usbser.sys`, one COM port per function.

The library does step 1 automatically whenever it emits an IAD ([4.2](#42-device-descriptor-18-bytes)). The rest is up to your design.

| Do this | Why |
| --- | --- |
| Name each Function (`EspUsbDeviceCdcSerial cdc(device, "Console")`) | Windows uses the IAD's iFunction as the child's name; without it identical names appear |
| Give `config.serialNumber` a value unique to each board | Windows remembers COM port numbers by VID/PID/serial; without one the number changes with every USB port |
| Change the PID while you are still changing descriptors | Windows caches driver binding per VID/PID; stale results show up as "only one port appears" and similar |

The Windows behaviour in this section was measured with v2. v3 produces descriptors of the same structure, but v3 has not been checked on Windows.

---

## 5. Control transfers

### 5.1 The SETUP packet (8 bytes)

| Byte | Field | Contents |
| --- | --- | --- |
| 0 | bmRequestType | bit 7 = direction (1 = IN), bits 6..5 = type (0 standard, 1 class, 2 vendor), bits 4..0 = recipient (0 device, 1 interface, 2 endpoint) |
| 1 | bRequest | Request number |
| 2–3 | wValue | Request-specific |
| 4–5 | wIndex | Interface number or endpoint address |
| 6–7 | wLength | Number of bytes in the data stage |

### 5.2 How EP0 is handled

A control transfer has three stages: SETUP → (DATA) → STATUS (USB 2.0 §8.5.3). The core tracks them.

- EP0's MPS is 64 bytes. The data stage moves 64 bytes at a time through a buffer inside the core.
- A new SETUP discards the previous request. The SETUP sequence number and bus generation keep old data out of the new request.
- A request the device cannot answer is stalled. STALL is not a fault; it is the correct answer "this request is not supported".

### 5.3 Standard requests

The core handles standard requests. It checks direction, value, index and length and stalls anything that does not match.

| Request | Handling |
| --- | --- |
| GET_STATUS (device) | Returns the self-powered bit and the remote-wakeup permission bit |
| SET_FEATURE / CLEAR_FEATURE (DEVICE_REMOTE_WAKEUP) | Accepted when remote wakeup is advertised and the device is in the Address or Configured state (USB 2.0 §9.4.1, §9.4.9) |
| SET_FEATURE (TEST_MODE) | Accepted only while running at HS. The test selector (high byte of wIndex) must be 1-4 (Test_J, Test_K, Test_SE0_NAK, Test_Packet). Entered after the status stage; only a power cycle leaves it (USB 2.0 §7.1.20, §9.4.9). FS and other selectors stall |
| SET_ADDRESS | Rejected once configured |
| GET_DESCRIPTOR | Device, configuration, string, device qualifier, other speed, BOS ([section 4](#4-descriptor-generation)) |
| GET_CONFIGURATION / SET_CONFIGURATION | Value 0 or 1. SET_CONFIGURATION closes every endpoint and reinitialises the classes, even for the same value |
| GET_INTERFACE / SET_INTERFACE | Passed to the class. If the class does not handle it, only alternate setting 0 is accepted |
| GET_STATUS (endpoint) | Returns the halt bit |
| SET_FEATURE / CLEAR_FEATURE (ENDPOINT_HALT) | Rejected for EP0 and isochronous endpoints. Otherwise applied, then the class is told |

- Clearing or re-selecting the configuration with SET_CONFIGURATION stops DMA, returns borrowed buffers (`BusReset`) and reinitialises the classes.
  A request for an unsupported value is rejected without dropping the active configuration.
- What SET_INTERFACE and ENDPOINT_HALT do to transfers depends on the class ([9.5](#95-reset-standard-requests-and-end) and the class documents).

### 5.4 Where class and vendor requests go

| Request | Goes to |
| --- | --- |
| Class request to an interface | The Function owning the interface in wIndex's low byte |
| Class request to an endpoint | The Function owning that endpoint |
| Vendor request (any recipient) | The library first (WebUSB, MS OS 2.0), then each Function in turn |

A Vendor Function receives vendor requests through `onControlRequest`. The callback runs at the SETUP, DATA and ACK stages.

```cpp
vendor.onControlRequest([](const EspUsbDeviceVendorControlRequest &r) {
  if (r.bmRequestType == 0xC0 && r.bRequest == 0x10) {         // IN, vendor, to the device
    if (r.stage == ESP_USB_DEVICE_CONTROL_STAGE_SETUP)
      return vendor.sendControlResponse(r, info, sizeof(info)); // clamped to wLength
    return true;
  }
  return false;   // STALL
});
```

Register the callback while stopped. See [Vendor](classes/vendor.md) for details.

### 5.5 Response rules

| Situation | Behaviour |
| --- | --- |
| IN, response shorter than requested | Normal. The data stage ends with a short packet |
| IN, response longer than requested | Clamped to wLength |
| IN, wLength of 1 or more, zero-length response | A zero-length data stage (ZLP) is sent |
| wLength of 0 (no data) | Only the STATUS stage |
| OUT, receive buffer smaller than wLength | Rejected |

When an IN request with a non-zero wLength is answered with zero bytes, a ZLP is sent instead of jumping to STATUS, so the host does not wait for a data stage forever.

### 5.6 Observing requests

`EspUsbDevice::onAnyControlRequest()` observes requests, including the ones the library answers itself.
The callback only observes; it cannot change what the device answers.

```cpp
device.onAnyControlRequest([](const EspUsbDeviceControlRequestInfo &r) {
  // Runs on the USB worker. Keep it to a counter or a queue
});
```

| Stage | Requests reported |
| --- | --- |
| `SETUP` | Every vendor request, whether or not the library answers it |
| `ACK` | Every control transfer that completes its STATUS stage |

| Field | Meaning |
| --- | --- |
| `responseLength` | Length of the response the library prepared (before clamping to wLength). Non-zero only for descriptors, the WebUSB URL and the MS OS 2.0 set |
| `handled` | At SETUP: the library recognised and answered it. At ACK: the transfer completed |

A stalled request never reaches its STATUS stage. Vendor requests are reported at SETUP so that "asked and refused" can be told apart from "never asked".
For example, whether Windows asks for the MS OS 2.0 set shows up as `type=0xC0 req=0x02 idx=0x0007`.

Register it while stopped. `onAnyControlRequest()` returns `esp_err_t` and refuses with `ESP_ERR_INVALID_STATE` while running, because the worker reads it.

---

## 6. Endpoint numbers and FIFOs

### 6.1 Controller capabilities

The numbers come from `espusb_dwc2_ports` in `src/internal/EspUsbDwc2Platform.cpp`.
The descriptor check, bulk IN double buffering and the transmit FIFO budget all use this table.

| SoC / controller | Speed | Endpoint numbers (excluding EP0) | Non-control IN | Non-control OUT | FIFO (32-bit words) |
| --- | --- | --- | --- | --- | --- |
| ESP32-S2 / S3 | FS | 1–6 | 4 | 6 | 256 |
| ESP32-P4 FS controller | FS | 1–6 | 4 | 6 | 256 |
| ESP32-P4 HS controller | HS | 1–15 | 7 | 15 | 1024 |

Exceeding a limit makes `begin()` fail with `ESP_ERR_INVALID_SIZE`. The PHY, interrupt and worker do not start.
The same direction of one endpoint number cannot be used by two different interfaces.

### 6.2 IN endpoints run out first

On DWC2 every IN endpoint needs its own transmit FIFO, while OUT endpoints share one receive FIFO.
That is why, on S2/S3 and P4 FS, the limit of four non-control IN endpoints is reached first.

Examples (S3):

| Configuration | IN | Result |
| --- | --- | --- |
| HID + CDC + MSC | 1 + 2 + 1 = 4 | Fits |
| CDC + CDC | 2 + 2 = 4 | Fits |
| CDC + CDC + CDC | 6 | `INVALID_SIZE` |
| CDC + Vendor + HID + MSC | 2 + 1 + 1 + 1 = 5 | `INVALID_SIZE` |

Ways to use fewer:

1. If you only stream bytes, use Vendor (one IN) rather than CDC (two INs).
2. Merge HID. Several HID Functions share one interface and one IN endpoint.
3. Use the P4 HS controller (seven INs).

### 6.3 How the FIFO is divided

The DWC2 shares one small FIFO SPRAM among these uses:

| Use | Size (words) |
| --- | --- |
| DMA endpoint information | endpoints × 2 (14 on S2/S3/P4 FS, 32 on P4 HS) |
| Receive FIFO | 14 + 2 × (largest OUT MPS / 4 + 1) + endpoints × 2 |
| EP0 transmit FIFO | 16 (64 bytes) |
| Transmit FIFO per IN endpoint | ceil(MPS / 4); doubled for double-buffered bulk IN |

- A transmit FIFO's number is not the USB endpoint number but the endpoint's rank among the IN endpoints in use. EP0 is always FIFO 0.
  FIFOs are packed even when endpoint numbers have gaps.
- FIFOs are allocated as endpoints are opened at SET_CONFIGURATION. Registers read right after `begin()` show the state before allocation.

Example on S3 with a 64-byte largest OUT packet: 256 − 14 (DMA info) − 16 (EP0 IN) − 62 (receive FIFO) = 164 words (656 bytes) are left for IN endpoints.
That is why an FS isochronous IN endpoint cannot have the maximum 1023 bytes.

P4 HS example: up to five Vendors (512-byte bulk IN = 128 words) fit. Six fit the seven non-control IN endpoints, but together with the receive FIFO and EP0 they exceed 1024 words, so `begin()` refuses them with `ESP_ERR_INVALID_SIZE`.

### 6.4 Bulk IN double buffering

With one packet's worth of transmit FIFO, the next packet cannot be staged until the current one has left. With two, one is sent while the next is staged.
The cost is not RAM but the controller's fixed FIFO. If it runs out, an endpoint does not open and enumeration fails.

Choose with `config.bulkInBuffering`:

| Value | Behaviour |
| --- | --- |
| `Auto` (default) | Double **every** bulk IN endpoint if all of them fit; otherwise change nothing |
| `Single` | One packet each |
| `Double` | Double every one; if they do not fit, `begin()` fails with `ESP_ERR_INVALID_SIZE` |

It is all or nothing because a rule that doubles only some endpoints would make throughput depend on the Function registration order.

| Controller | Rule of thumb |
| --- | --- |
| S2 / S3 / P4 FS | Four 64-byte bulk IN endpoints can all be doubled |
| P4 HS | Two 512-byte bulk IN endpoints can be doubled; with three or more, Auto doubles none |

`device.bulkInDoubleBuffered()` returns the bitmap of endpoints that were doubled. It is the first thing to check when a stream is slower than expected.

### 6.5 Transmit FIFO budget

`begin()` computes the FIFO budget for every IN endpoint in the configuration: receive FIFO, EP0, each IN endpoint and double buffering (the formulas in [6.3](#63-how-the-fifo-is-divided)).
If it does not fit, `begin()` fails with `ESP_ERR_INVALID_SIZE`. This happens before the PHY is touched, so nothing happens on the host. `DescriptorsOnly` runs the same check.

- An endpoint number reused by alternate settings is counted once, at its largest packet.
- The check runs up front because a configuration that does not fit could not open its endpoints at SET_CONFIGURATION.
- Isochronous endpoints fail in a way that is hard to notice (the host binds a driver, the device reports data sent, and nothing arrives). This matters most for Audio capture and Video.

---

## 7. HS and FS

### 7.1 Maximum packet sizes

| Transfer | FS | HS |
| --- | --- | --- |
| Control | 64 (this library) | 64 |
| Bulk | 8 / 16 / 32 / 64 | 512 |
| Interrupt | 1–64 | 1–1024 |
| Isochronous | 1–1023 | 1–1024 |

The core checks these ranges when opening an endpoint. HS bulk endpoints other than 512 are rejected (USB 2.0 §5.8.3).

### 7.2 Values that change at HS

The P4 default (`controller = Auto`) uses the HS controller. At HS these change:

| Item | FS | HS |
| --- | --- | --- |
| Bulk MPS | 64 (Vendor also allows 8/16/32) | 512 |
| Borrowed RX (`readDirect`) capacity | Multiple of 64 | Multiple of 512 |
| Lengths that get a ZLP with `EndWithShortPacket` | Multiples of 64 | Multiples of 512 |
| MSC `transferBytes` | Multiple of 64 | Multiple of 512 (otherwise `begin()` gives `INVALID_SIZE`) |
| CCID `messageBytes` default (0 = automatic) | 271 | 512 |
| HID interrupt MPS | Up to 64 | Up to 1024 |
| Non-control IN | 4 | 7 |

CDC/Vendor/MIDI `transferBytes` may be smaller than the MPS.
Settings that worked at FS can make `begin()` fail at HS. Check the return value and `lastError()`.

### 7.3 Choosing the controller and falling back to FS

| `config.controller` | Behaviour |
| --- | --- |
| `Auto` (default) | HS when there is an HS controller (P4), otherwise FS (S2/S3) |
| `FullSpeed` | Uses the FS controller; on P4 that is the FS port |
| `HighSpeed` | Uses the HS controller; on chips without one, `begin()` gives `ESP_ERR_NOT_SUPPORTED` |

- An HS controller connected to an FS host or hub runs at FS. That is why `begin()` builds both FS and HS descriptors for an HS controller.
- FS values (bulk MPS 64 and so on) are decided only once connected. Checks that depend on the MPS, such as the borrowed RX capacity, use the actual speed.
- Unknown SoCs are never mapped onto an existing port. ESP32-S31 is not supported by the current SDK.

---

## 8. Cache and DMA

### 8.1 The DMA mode

Every chip uses DWC2 buffer DMA mode. Slave mode, where the CPU pushes each packet into the FIFO, is not used (a controller without internal DMA is refused at initialisation).

Every area DMA touches is in internal RAM:

| Area | Allocated by |
| --- | --- |
| Core region (EP0 SETUP/data, DWC2 records) | The library. Internal, DMA-capable, 64-byte aligned |
| Class DMA buffers, copied-TX staging | The library. Same |
| Borrowed-transfer buffers | You. DMA-capable internal RAM, start aligned to 64 bytes |

### 8.2 Cache synchronisation on P4

On P4 the CPU reaches internal RAM through the L1 data cache (64-byte lines). S2/S3 have no such cache, and the synchronisation calls do nothing there.
The library does the cache maintenance. You never need to call `esp_cache_msync` per SoC.

| When | What | Why |
| --- | --- | --- |
| Before IN DMA | Clean the buffer being sent (write the cache back to memory) | So DMA does not send stale data |
| Before OUT DMA | Clean the receive buffer too | A dirty line evicted during DMA would overwrite received data |
| After OUT DMA | Invalidate the received range | So the CPU does not read stale cache lines |
| At start | Clean, then invalidate, the SETUP area | Clears dirty lines left by initialisation before DMA. On failure `begin()` gives `ESP_FAIL` |

The clean before OUT fixes a real corruption seen with HS borrowed RX on P4X:
in a 65535-byte receive the latter part was never written, while completion reported success.

What you must do:

- Do not read or write a borrowed buffer from the CPU until its completion callback. On P4, writes during DMA can be lost or corrupt the received data.
- Make the borrowed RX capacity a multiple of 64 (the library checks). With a 64-byte aligned start, the buffer then shares no cache line with other variables.

### 8.3 Splitting DMA

How much one DMA programming can carry is set by the width of the controller's counters.
On S3, for example, the packet counter is 7 bits, so with MPS 64 one programming carries at most 8128 bytes.

Borrowed transfers hide this. The library splits them internally:

- Intermediate pieces are multiples of the MPS. No short packet or ZLP is inserted in the middle.
- Completion is reported once for the whole transfer.
- The length limit is `maxDirectTransfer()` (2147483647 bytes on 32-bit ESP chips). That is what can be expressed, not what can be allocated.

A single control or isochronous submission is still limited to 16 bits.

### 8.4 Missed ISO OUT frames

When DWC2 arms an ISO OUT endpoint, it is told whether the next frame (microframe at HS) is even or odd.
If the host skips an interval, the parity no longer matches and an incomplete-ISO-OUT interrupt fires.

The library re-arms the endpoint inside the ISR with the opposite parity and the same DMA buffer. Nothing was received, so no stale data can be delivered.
Re-arming through the worker would miss the 125 µs microframe at HS and lose the next packet as well.
This interrupt is enabled only while an ISO OUT endpoint is open.

---

## 9. Copied and borrowed transfers

CDC and Vendor have two paths. MIDI has only the copy path (its packet API).

| Path | API | Owner of the buffer | Use |
| --- | --- | --- | --- |
| Copy | `write()`, `read()`, `available()` | Back with you when the call returns | Ordinary transfers |
| Borrowed | `writeDirect()`, `readDirect()` | The library until the completion callback | Large data, avoiding copies |

### 9.1 The copy path

- `write()` copies into the ring and returns the number of bytes accepted. That does not mean the host received them. With too little ring space, only part is accepted.
- Received data goes into the ring. When the ring is full, bulk reception stops and the host is made to wait (backpressure). Nothing is overwritten silently.
- Bulk is a byte stream. One `write()` does not correspond to one host read. Carry message boundaries in your protocol.

Capacities and termination (ZLP) are per class. See [CDC](classes/cdc.md) and [Vendor](classes/vendor.md).

### 9.2 Conditions for borrowed transfers

| Condition | On violation (`EspUsbTransferError`) |
| --- | --- |
| The completion callback (`onDirectTxComplete`/`onDirectRxComplete`) was registered before `begin()` | `BadArgument` |
| The start is 64-byte aligned | `NotAligned` |
| The whole buffer is DMA-capable internal RAM (not PSRAM) | `NotDmaCapable` |
| Length is at least 1 and at most `maxDirectTransfer()` | `BadArgument` |
| RX capacity is a multiple of 64 and of the current MPS | `BadArgument` |
| RX only with `rxMode = Borrowed` | `NotSupported` |
| The host has configured the device and the endpoint is open | `NotMounted` |
| No transfer in flight in the same direction (for TX, no copied data pending either) | `Busy` |
| The controller accepted it | `TransferFailed` |

- `writeDirect`/`readDirect` return an `EspUsbTransferSubmission`. Check acceptance with `if (submission)`.
- A rejected submission borrows nothing and produces no callback.
- An accepted one has a non-zero `id`. IDs keep increasing across `begin`/`end`.
- One borrowed transfer per direction per Function can be in flight. `Busy` is ordinary backpressure, not an error.
- TX lengths need not be multiples of 64 or of the MPS.
- `writeDirect` ends either with `Continue` (no ZLP) or `EndWithShortPacket` (when the length is a multiple of the MPS, completion waits until a ZLP has been sent).

### 9.3 Return (the completion callback)

The completion callback runs on the USB worker exactly once per accepted transfer.
`EspUsbTransferResult` carries `buffer`, `requestedBytes`, `actualBytes`, `actualBytesKnown`, `status` and `id`.

| `status` | Meaning | `actualBytes` |
| --- | --- | --- |
| `Success` | Completed. It does not mean the host application processed it | Valid |
| `BusReset` | Dropped by a bus reset, a configuration clear/re-select, or queue-overflow recovery | Unknown (`actualBytesKnown=false`) |
| `Stopped` | Returned by `end()` after DMA stopped | Unknown |
| `TransferError` | A low-level transfer error, or ENDPOINT_HALT | Only what is known |
| `Cancelled` | Returned by a cancellation, SET_INTERFACE or CLEAR_FEATURE(HALT) | Unknown |

- RX completes when the buffer is full or a short packet / ZLP arrives. Call `readDirect()` again for the next receive.
  While nothing is submitted, OUT data is not accepted and the host waits.
- The next prepared buffer may be submitted from the callback. Filling the next buffer inside the callback leaves the endpoint idle meanwhile.
  Fill one while the other is in flight, and have the callback only submit.

### 9.4 Cancellation

```cpp
auto s = vendor.writeDirect(buffer, length);
// …
vendor.cancelDirectTx(s.id);   // id = 0 targets the current loan
// Do not touch the buffer until onDirectTxComplete returns it (Cancelled or another status)
```

- `ESP_OK` from `cancelDirectTx(id)`/`cancelDirectRx(id)` means the request was accepted. The buffer has not been returned yet.
- The library stops DMA, places a fence in the queue to retire old completion events, and then returns the buffer through the completion callback. It never returns it before DMA has stopped.
- If completion and cancellation race, there is exactly one result. It can be `Success`.
- With nothing to cancel, or a non-matching `id`, the result is `ESP_ERR_INVALID_STATE`. An old `id` never cancels a newer transfer.
- A second cancellation of the same transfer returns `ESP_OK`. Racing with a reset or `end()` can return it as `BusReset`/`Stopped`.
- Giving up on a wait (a timeout) does not return the buffer. Always wait for the completion callback.
- Cancellation sends no STALL to the host and does not change the DATA PID.

### 9.5 Reset, standard requests and end()

| Event | Borrowed transfer in flight | Copy ring |
| --- | --- | --- |
| Bus reset | `BusReset` | Discarded |
| SET_CONFIGURATION (clear / re-select) | `BusReset` | Discarded |
| SET_INTERFACE (alt 0, data interface) | `Cancelled`; both directions back to DATA0 | That direction discarded |
| SET_FEATURE(ENDPOINT_HALT) | `TransferError` on that endpoint only; stays halted | That direction discarded |
| CLEAR_FEATURE(ENDPOINT_HALT) | `Cancelled` on that endpoint only; back to DATA0 | That direction discarded |
| Queue-overflow recovery | `BusReset` | Discarded |
| `end()` | `Stopped` | Released |

- At the start of a bus reset (in the ISR) the core invalidates the configuration and refuses new submissions and DMA programming until the worker has finished the reset.
- Each time the configuration is rebuilt, a configuration generation advances. Completion events of an older generation left in the queue are discarded,
  so even when an endpoint number is reused, an old completion never returns a new loan.
- Data that `write()` accepted but has not yet sent can be lost on any of these events. If delivery must be guaranteed, confirm and retransmit at the application level.
- The table describes CDC/Vendor. See the class documents for other classes.

### 9.6 ZLPs and the end of a transfer

A bulk transfer ends with a packet shorter than the MPS (a short packet) (USB 2.0 §5.8.3).
If the length is an exact multiple of the MPS, the receiver assumes more is coming; a ZLP is needed to mark the end.

| Path | How the end is chosen |
| --- | --- |
| Borrowed TX | Per transfer with `EspUsbTransferTermination` (`Continue` / `EndWithShortPacket`) |
| Copy TX | `streamTermination` (Vendor defaults to `Continue`, CDC to `EndWithShortPacket`) |

"The host read never returns for one particular length" or "an empty read comes back now and then" usually come from here.
Host code should handle transfers both with and without a ZLP. Bulk ZLP rules do not apply to isochronous transfers.

---

## 10. Using PSRAM

PSRAM can be used as storage the CPU reads and writes. Anything DMA touches stays in internal RAM.

| Area | PSRAM |
| --- | --- |
| CDC/Vendor rings passed as `rxBuffer`/`txBuffer` | Allowed, with no alignment requirement |
| Automatically allocated rings | Allocated with `malloc`; where they land depends on the sdkconfig malloc settings |
| MSC storage (RAM disk, the target of the callbacks) | Allowed |
| Class DMA buffers, copied-TX staging | Not used (the library allocates internal RAM) |
| Borrowed-transfer buffers | Not allowed (`NotDmaCapable`) |
| User buffers handed to DMA, such as MSC `transferBuffer` | Not allowed (`configure()` returns `ESP_ERR_INVALID_ARG`) |

- The library does not free rings you pass in. Keep them alive from `begin()` until `end()`.
- To send PSRAM data with a borrowed transfer, copy it into an internal-RAM buffer yourself and submit that. The library never bounce-copies silently.

Verified: Vendor rings (8 KiB each) and MSC storage placed in PSRAM, both over the P4 FS loopback and over the P4/P4X HS direct link.

---

## 11. Suspend and remote wakeup

### 11.1 Suspend

When the bus has been idle for 3 ms, the device enters suspend (USB 2.0 §7.1.7.6).

- `suspended()` becomes true. The configuration is kept, and `ready()` stays true.
- Suspend is not a reset. Borrowed buffers are not returned automatically; they come back on completion, cancellation, reset or `end()`.
- Resume is detected from the controller's resume interrupt or from SOFs restarting.
- A suspend before the first SETUP is not treated as suspend.
- Keeping current within the suspend limit (§7.2.3) is up to the board and the application. The library does not control SoC sleep or peripheral circuits.

### 11.2 Remote wakeup

Remote wakeup lets the device wake a suspended host. Three things are kept separate:

| Stage | Decided by | API |
| --- | --- | --- |
| Advertising the capability | The application (before `begin()`) | `config.remoteWakeupSupported` (default false); becomes bit 5 of the configuration's bmAttributes |
| Permission to use it | The host (SET_FEATURE / CLEAR_FEATURE) | Query with `remoteWakeupEnabled()` |
| Requesting a wakeup | The application | `remoteWakeup()` |

```cpp
EspUsbDeviceConfig config;
config.remoteWakeupSupported = true;
device.begin(config);

// In a task such as loop(), when an external event occurs
if (device.suspended() && device.remoteWakeupEnabled()) {
  esp_err_t result = device.remoteWakeup();
  // ESP_OK means "accepted into the queue". Confirm the resume by suspended() turning false
}
```

| `remoteWakeup()` returns | When |
| --- | --- |
| `ESP_OK` | The request was accepted. The bus has not necessarily resumed yet |
| `ESP_ERR_INVALID_STATE` | Not started / stopping, `DescriptorsOnly`, not configured, not suspended, not permitted by the host, called from an ISR, or a request is already pending |
| `ESP_ERR_TIMEOUT` | The event queue is full |
| `ESP_ERR_NOT_SUPPORTED` | The chip has no USB OTG |

- The host's permission is cleared by a bus reset and by `end()` (§9.4.5). There is no API for the application to grant it.
- If a resume, CLEAR_FEATURE, reset, reconfiguration or stop happens after acceptance, the request is not carried out.
- The specification allows a resume request only after the bus has been idle for at least 5 ms (§7.1.7.7). The worker waits 5 ms plus one tick, re-checks the state, then signals.
- The resume signal must last 1 to 15 ms (§7.1.7.7). The library waits 2 ms inside the controller lock (`esp_rom_delay_us`),
  because `vTaskDelay(1 ms)` can be shorter than 1 ms when called partway through a tick. The cost is that interrupts on that core are delayed by about 2 ms while it signals.
- If unused, it adds no timer, task or wait.

---

## 12. Adding your own Function

### 12.1 Build on an existing Function first

Most new features can be built by putting your own protocol on an existing Function.

| Goal | Use |
| --- | --- |
| Driverless custom data | `EspUsbDeviceHidVendor` (fixed-size reports), `EspUsbDeviceHidCustom` (your own report descriptor) |
| A custom protocol that needs bandwidth | `EspUsbDeviceVendor` (bulk IN/OUT plus vendor control, WinUSB/WebUSB) |
| Appear as a serial port | `EspUsbDeviceCdcSerial` |
| Observe control requests | `device.onAnyControlRequest()` |

### 12.2 What EspUsbDeviceClass exposes

`EspUsbDeviceClass`, the base of every Function, is public. What it exposes, however, stops at **writing descriptors**.

| Exposed | Purpose |
| --- | --- |
| `configurationDescriptor(dst, itf, ep, packet)` | Required. Writes the descriptor with the assigned numbers and returns its length |
| `configurationDescriptorForSpeed(dst, capacity, itf, ep, highSpeed)` | When the contents depend on speed |
| `interfaceCount()` / `endpointCount()` | Required. Interfaces and endpoint numbers used |
| `functionName()`, `auxiliaryStringCount()`/`auxiliaryString()` | Strings |
| `descriptorConfigurationError()` | Reports a setting error to `begin()` |
| Type predicates such as `isHid()` | All default to false. `isHid()` is true only for HID Functions (`EspUsbHidFunction`) |

The writer contract:

- With `dst == nullptr`, return the length and write nothing.
- With a non-null `dst`, never write beyond `capacity`.
- Until the configuration changes, the measured length must equal the written length. Otherwise `begin()` rejects it with `ESP_ERR_INVALID_SIZE`.
- Use the numbers you are given. Never pick your own.

```cpp
class MyFunction : public EspUsbDeviceClass {
public:
  explicit MyFunction(EspUsbDevice &device) : EspUsbDeviceClass(device) {}
  uint8_t interfaceCount() const override { return 1; }
  uint8_t endpointCount() const override { return 0; }
  uint16_t configurationDescriptor(uint8_t *dst, uint8_t itf, uint8_t, uint16_t) override {
    const uint8_t bytes[] = {9, 4, itf, 0, 0, 0xff, 0, 0, 0};
    if (dst) memcpy(dst, bytes, sizeof(bytes));
    return sizeof(bytes);
  }
};
```

**Limitation:** registering a "class driver", which opens endpoints, runs transfers and answers class requests, is an internal API (`src/internal/`) and is not public in v3.
At SET_CONFIGURATION the core hands every interface to some class driver. If any interface has no driver, SET_CONFIGURATION fails.
So a Function that only derives from `EspUsbDeviceClass` cannot talk to a host.
It is limited to uses such as checking descriptors with `DescriptorsOnly`. Implementing a new USB class requires changing the library itself.

### 12.3 Steps when adding a class to the library

When adding a class to the library itself, this order makes failures easier to isolate:

1. Build only the descriptors with `DescriptorsOnly` and check the bytes and the endpoint budget.
2. Enumerate on hardware and compare with `lsusb -v` and similar on the host.
3. Check the data path one direction at a time.
4. Check that buffers come back exactly once on reset, configuration re-select, HALT, cancellation and `end()`.
5. Pass the standard tests in `validation/` (`--combo all`).

See [docs/development/architecture.ja.md](development/architecture.ja.md) (Japanese) for the internal design.

---

## 13. Measurement and debugging

### 13.1 Tools

| To find out | Use |
| --- | --- |
| Why `begin()` failed | `device.lastError()` / `lastErrorName()` |
| The descriptors you emit, the endpoint budget | [`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/) (no host needed) |
| How far enumeration gets | [`EspUsbDeviceBringUpCheck`](../examples/Info/EspUsbDeviceBringUpCheck/) |
| What the host received | `lsusb -v` (Linux), USB Device Tree Viewer (Windows) |
| Which control requests the host sent | `device.onAnyControlRequest()` |
| Bulk IN double buffering | `device.bulkInDoubleBuffered()` |
| The speed the host chose (FS / HS) | `device.speed()` |
| Automated two-board tests | `validation/` (`pytest --combo all`) |

### 13.2 Principles for isolating a problem

1. **Check whether `begin()` succeeded first.** If it failed, the host is irrelevant.
2. **Read the descriptors without a host.** If they are not what you intended, looking further is wasted effort.
3. **Compare your output with what the host received.** If they match, the problem is above or below the descriptors.
4. **Remove Functions one at a time.** If a composite problem disappears with a single Function, the cause is the budget or the order.
5. **Unexpected re-enumeration points to queue overflow.** Check for heavy callbacks or a lowered worker priority ([2.5](#25-the-event-queue-and-overflow)).
6. **Change one thing at a time.** Class mix, MPS and buffer sizes, one by one.

### 13.3 Throughput

Bandwidth depends heavily on how the host side is written:

- Increase the number of reads the host keeps in flight (URBs). With one at a time, the endpoint idles between URBs.
- In the host's completion callback, only resubmit buffers. Heavy work such as parsing inside the callback opens gaps in the stream.
- On the device side, bulk IN double buffering ([6.4](#64-bulk-in-double-buffering)), large transfers, and borrowed-transfer callbacks that only submit are what help.

Baseline throughput figures for v3 have not been measured yet.

---

## 14. Differences from v2

| v2 | v3 |
| --- | --- |
| Bundled and built TinyUSB's device stack and class drivers | Not used. The library's core and Functions do the work; only the DWC2 driver is a maintained fork of TinyUSB's |
| Capacities tuned with `CFG_TUD_*` in `tusb_config.h` and `build_opt.h` | Set per Function with `configure()` while stopped. Old flags are compile errors |
| `config.startTinyUsb = false` | `config.startMode = EspUsbDeviceStartMode::DescriptorsOnly` |
| `device.task()` | Removed. The USB worker does the work |
| Fixed worker stack/priority | `taskStackBytes`/`taskPriority`/`taskCoreId`/`eventQueueDepth` |
| Up to 6 Functions (classes) | Up to 251 (endpoint/FIFO budgets checked separately) |
| Configuration descriptor up to 704 bytes, merged HID report up to 256 bytes | Allocated at the real length; limited only by USB's 16 bits |
| Merged HID report IDs fixed per type (Keyboard = 1 and so on) | Assigned uniquely per configuration; read with `compositeReportId()` |
| Strings widened from ASCII to 16 bits and cut at 63 characters | UTF-8 converted to UTF-16, up to 126 code units; too long or invalid is an error |
| WebUSB URLs without a prefix treated as https, and truncated | Without a prefix, stored in full as scheme 255; too long is an error |
| `writeDirect` enabled by a build flag, up to 65535 bytes | Always available. Returns `EspUsbTransferSubmission`. No 16-bit length limit. `readDirect` and cancellation added |
| `end()` returned nothing | Returns `esp_err_t`. After success no DMA or callback touches any buffer. The wait for the worker is bounded by `stopTimeoutMs` (`ESP_ERR_TIMEOUT`) |
| Class drivers added through `usbd_app_driver_get_cb` | Internal API, not public ([12.2](#122-what-espusbdeviceclass-exposes)) |
| No remote wakeup | `remoteWakeupSupported`, `remoteWakeupEnabled()`, `remoteWakeup()` |
| No suspend query | `suspended()` |
| No speed query | `speed()` |
| VBUS sensing via `CFG_TUD_VBUS_DETECT_HW` | `config.vbusSensing` |

Migration steps are in [Migrating from v2](migrating-from-v2.md).

---

## 15. What has been verified

| Environment | Status |
| --- | --- |
| Two ESP32-S3 boards as FS peers | Main test environment; every class and the shared mechanisms |
| One ESP32-P4 as an HS↔FS loopback | Basic transfers of each class on both controllers |
| Old P4 (v1.x) and P4X (v3.x) connected directly at HS | Both directions |
| P4X connected directly to a Linux PC at HS | DFU/MSC on the wire |
| ESP32-S2 | Compile only; not tested on hardware |
| ESP32-S31 | Not supported |
| Windows / macOS | Not checked with v3 |

The standard tests are `pytest --combo all` in `validation/` (6 combinations, more than 200 cases), against Arduino-ESP32 3.3.12.

Not verified for this document:

- Remote wakeup on S2/P4 hardware and with PC operating systems (verified only S3 to S3).
- VBUS detection on a real cable unplug with `vbusSensing = true`.
- HS electrical compliance and compliance testing (the `SET_FEATURE(TEST_MODE)` test signals have not been measured with test equipment).
- Baseline throughput figures for v3.

---

## Related documents

- [USB Device Development Guide](usb-device-guide.md)
- [Example workflow](example-workflow.md)
- [Troubleshooting](troubleshooting.md)
- [Firmware update over USB](ota-over-usb.md)
- [Migrating from v2](migrating-from-v2.md)
- [Migrating from the Arduino-ESP32 USB stack](migrating-from-arduino-esp32-usb.md)
- Per class: [HID](classes/hid.md), [CDC](classes/cdc.md), [Vendor](classes/vendor.md), [MIDI](classes/midi.md), [MSC](classes/msc.md), [Audio](classes/audio.md), [Video](classes/video.md), [NCM](classes/ncm.md), [CCID](classes/ccid.md), [DFU](classes/dfu.md)
- [Origin and maintenance of the USB backend](../third_party/tinyusb/PROVENANCE.md)
- [Internal design](development/architecture.ja.md), [testing](development/testing.ja.md) (Japanese)
- Specifications: [USB 2.0 specification](https://www.usb.org/document-library/usb-20-specification), [WebUSB](https://wicg.github.io/webusb/), [Microsoft OS 2.0 descriptors](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-os-2-0-descriptors-specification)
- Host side: [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost)
