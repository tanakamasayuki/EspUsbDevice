# USB Device Development Guide

> 日本語: [usb-device-guide.ja.md](usb-device-guide.ja.md)

A guide to turning an ESP32 into a USB device and getting a PC or another host to recognize it. It covers EspUsbDevice v3.

- The first half covers USB device basics and the shape of the v3 API.
- The second half covers ESP32-specific constraints and a step-by-step bring-up procedure.

The goal is to be able to work out for yourself *why the host does not recognize the device*. If you are in a hurry, start at [Bring-up procedure](#5-bring-up-procedure) and come back to the first half when you meet an unfamiliar term.

- Detailed per-class usage lives in the guides under `docs/classes/` (linked from [the table in 1.7](#17-choosing-what-to-look-like)).
- For internals and limits, continue to the [USB Device Development Guide (Advanced)](usb-device-advanced.md).
- Coming from v2? Read [Migrating from v2](migrating-from-v2.md) first.
- If you want the ESP32 to be the host instead, see [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) and its [USB Host Development Guide](https://github.com/tanakamasayuki/EspUsbHost/blob/main/docs/usb-host-guide.md).

## Contents

1. [USB device basics](#1-usb-device-basics)
2. [The basic EspUsbDevice flow](#2-the-basic-espusbdevice-flow)
3. [Power and connectors](#3-power-and-connectors)
4. [ESP32-specific notes](#4-esp32-specific-notes)
5. [Bring-up procedure](#5-bring-up-procedure)
6. [Observing yourself from the host OS](#6-observing-yourself-from-the-host-os)
7. [Troubleshooting](#7-troubleshooting)
8. [Tools](#8-tools)
9. [What has been verified](#9-what-has-been-verified)
10. [References](#10-references)

---

## 1. USB device basics

### 1.1 Host and device

USB is not a peer-to-peer link. **One host sits at the top, and devices hang off it.** The host always starts communication. A device cannot push data on its own; it can only answer when the host comes to ask whether there is any.

With this library the ESP32 is the **device**. That means:

- **You cannot start a transfer.** `keyboard.tapKey('a')` reaches the bus only when the host polls that endpoint. If the host never comes, the send waits or is refused.
- **What you are is self-declared.** Whether you look like a keyboard or a serial port is decided by the descriptors you return.
- **The host changes your state when it likes.** Suspend, bus reset and deconfiguration can happen at any time. Each time, transfers stop and your state is reset.

If the host's job is "read and understand the other side's descriptors", the device's job is "**return descriptors the host understands**".

### 1.2 Which connector is the device side

USB connectors on an ESP32 board look the same but do different jobs.

| Connector | Connects to | Used for |
| --- | --- | --- |
| USB-UART bridge | A chip such as CP2102 or CH340 | Flashing and serial logs |
| USB Serial/JTAG | The SoC's built-in peripheral (S3, P4) | Flashing and logs |
| **USB OTG** | The SoC's OTG controller | **The port this library uses** |

On the ESP32-P4 the OTG side is further split into a high-speed and a full-speed controller ([4.2](#42-choosing-hs-or-fs-on-esp32-p4)).

Silkscreen labels such as `USB`, `OTG` and `UART` are not reliable. **Check the schematic.**

During development, **a board with two USB connectors** is strongly recommended. It keeps the port you read logs from separate from the port you connect to the host. On a one-connector board, the log port disappears the moment the device starts, and isolating problems becomes very hard.

### 1.3 Speed

| Speed | Short | Rate | This library |
| --- | --- | --- | --- |
| Low Speed | LS | 1.5 Mbps | Not handled |
| Full Speed | FS | 12 Mbps | ESP32-S2/S3, ESP32-P4 FS controller |
| High Speed | HS | 480 Mbps | ESP32-P4 HS controller only |
| SuperSpeed | SS | 5 Gbps and up | Not available on ESP32 |

**The device declares its speed.** It does so with the D+ pull-up and, for HS, the chirp handshake (USB 2.0 §7.1.7.5). You can only pick a speed the SoC's controller has. An HS controller attached to an FS host still links at FS.

A device running at HS is also asked "what would you look like at FS?". The answers are the **Device Qualifier** (USB 2.0 §9.6.2) and the **Other Speed Configuration** (§9.6.4). The bulk max packet size (MPS) is 64 bytes at FS and 512 at HS (§5.8.3), so one configuration needs a descriptor per speed. On a P4 using the HS controller, the library builds both.

### 1.4 Enumeration, from the answering side

The steps before the host can use the device (USB 2.0 §9.1.2). **Where it stops** is where isolation starts.

1. **Attach detection**: you pull D+ up, signalling an FS/HS device
2. **Bus reset**: the host resets the bus; HS chirp happens here
3. **Address**: you answer `SET_ADDRESS`
4. **Device descriptor**: you return 18 bytes for `GET_DESCRIPTOR(DEVICE)`
5. **Configuration descriptor**: you return the whole configuration; this is where most failures happen
6. **String descriptors**: manufacturer, product, serial number
7. **`SET_CONFIGURATION`**: only now do the Functions' endpoints open
8. **Host driver binding**: the OS looks at the class codes and loads a driver

`device.ready()` becomes `true` after step 7.

Isolation:

- **Stops at 1–3** (nothing shows on the host) → electrical. Cable, connector, board settings, controller selection.
- **Stops at 4–6** (the host reports something like "device descriptor request failed") → descriptors.
- **Reaches 7 but no driver loads** (shows as an unknown device) → class codes or the OS driver.
- **Reaches 8 but does not work** → the class protocol.

### 1.5 Descriptors are your declaration

A device declares its configuration as a sequence of structures called **descriptors**.

```
Device (VID/PID, USB version, EP0 size)
└── Configuration (current draw, bus/self powered)
    ├── (IAD: groups several interfaces into one function)
    ├── Interface 0 (class / subclass / protocol ← "what it is" lives here)
    │   ├── Class-specific descriptors (HID descriptor, CDC functional descriptors, ...)
    │   ├── Endpoint 0x81 (IN, interrupt, MPS 8, interval 10)
    │   └── Endpoint 0x01 (OUT, ...)
    └── Interface 1
        └── ...
```

With EspUsbDevice you do not write these by hand. `begin()` assembles them from the registered Functions. But **always check what was assembled**. [`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/README.md) prints all of it.

Points to keep in mind:

- **Classes belong to interfaces, not to the device.** The device descriptor's class is `0xEF/0x02/0x01` (composite device using IADs) when the configuration contains an IAD, and `0x00` (defined by the interfaces) otherwise. Either way, you have to look at the interfaces to know what the device is. IADs appear for Functions with several interfaces, such as CDC, MIDI, Audio and Video.
- **The library numbers interfaces and endpoints.** You do not choose them. Numbers follow registration order. Check the actual values with DescriptorDump or on the host.
- **The HID report descriptor is separate.** The configuration descriptor only says how many bytes it is; the host fetches the contents with another request. It is the report descriptor that gives HID input its meaning.
- **Several HID Functions share one interface.** Keyboard + mouse + gamepad do not add interfaces; they are told apart by report ID on one HID interface. Default IDs are keyboard 1, mouse 2, gamepad 3, consumer 4, system 5, vendor 6. Repeated types are given free IDs automatically.

### 1.6 Endpoints and transfer types

An endpoint is a channel. It has an address (as in `0x81`: bit 7 is the direction, the low 4 bits the number), a direction, a transfer type, an MPS and a polling interval.

| Transfer type | Characteristics | Used here for |
| --- | --- | --- |
| **Control** | Setup transfers on EP0. Every device has it | Enumeration, class requests, HID SET_REPORT, vendor control, DFU |
| **Interrupt** | The host polls at a fixed interval. Small, low latency | HID, CDC and NCM notifications, CCID slot notifications |
| **Bulk** | Large data. No bandwidth guarantee, errors are retried | CDC data, MSC, NCM, Vendor, MIDI, CCID, Video (bulk mode) |
| **Isochronous** | Reserved bandwidth. Errors are not retried | Audio, Video (default) |

Directions are **named from the host's side**. "IN" flows from the device (you) to the host; "OUT" from the host to the device.

On the device side, **endpoints use up the number the SoC's USB controller has, and the FIFO inside it**. This is the first wall in device development ([4.4](#44-endpoint-budget), [4.5](#45-fifo-budget)).

### 1.7 Choosing what to look like

In host development, the other side decided what it looked like. On the device side **you choose**. This is the biggest design decision in device work.

| Appearance | Class | Good for | Host driver | Guide |
| --- | --- | --- | --- | --- |
| HID keyboard / mouse / gamepad | `0x03` | Input devices, macro pads, automation | OS built-in. **No driver** | [HID](classes/hid.md) |
| HID (vendor / custom reports) | `0x03` | Two-way custom data, driverless even on Windows | OS built-in (open with HIDAPI etc.) | [HID](classes/hid.md) |
| CDC ACM | `0x02` | Appear as a serial port; existing terminal software works | OS built-in | [CDC](classes/cdc.md) |
| USB MIDI 1.0 | `0x01`/MIDI Streaming | DAWs, synths, controllers | OS built-in. **No driver** | [MIDI](classes/midi.md) |
| Mass Storage (BOT) | `0x08` | Appear as a drive; file exchange | OS built-in | [MSC](classes/msc.md) |
| USB Audio (UAC1 / UAC2) | `0x01` | Speakers, microphones | OS built-in | [Audio](classes/audio.md) |
| USB Video (UVC) | `0x0E` | Cameras | OS built-in. **No driver** | [Video](classes/video.md) |
| CDC-NCM | `0x02`/NCM | Appear as a network adapter; a web UI over USB | Built into the major OSes | [NCM](classes/ncm.md) |
| CCID | `0x0B` | Smart card readers | OS built-in (PC/SC) | [CCID](classes/ccid.md) |
| DFU | `0xFE`/DFU | Firmware update from `dfu-util` | On Windows, needs WinUSB as below | [DFU](classes/dfu.md) |
| Vendor Specific (bulk) | `0xFF` | Your own protocol, large bulk transfers | **Windows needs WinUSB bound** | [Vendor](classes/vendor.md) |

How to decide:

1. **If the host must not install a driver, use a standard class.** HID, CDC, MIDI and MSC work as-is on the major OSes.
2. **If you want custom data without a driver, use HID vendor reports.** Many commercial devices do this. It is not fast, but it is two-way with no driver and no permissions.
3. **If you need bandwidth, use Vendor (bulk).** On Windows a Microsoft OS 2.0 descriptor is needed to bind WinUSB. This library returns one automatically whenever a Vendor or DFU Function is present, with or without WebUSB.
4. **If you want an existing application to use it, pick the class that application expects.** MIDI for a DAW, CDC for a terminal program.

Composite devices with several Functions are possible, but endpoint and FIFO budgets and host OS behavior come into play. Get one Function working first ([Step 7](#step-7-grow-into-a-composite-device)).

### 1.8 VID, PID and the identity around them

The device descriptor carries two 16-bit numbers: the **vendor ID** (who made it) and the **product ID** (which of their products it is). Together with the **serial number string**, these three are how a host tells "that device I have seen before" from "a different device". Unlike the other descriptor fields, **the host remembers them**.

**What you get if you set nothing.** The defaults are `vid = 0x303A` and `pid = 0x4000`. `0x303A` is Espressif's vendor ID; `0x4000` is the first of the PIDs ESP-IDF's TinyUSB component uses by default (`0x4000`–`0x4007`). Espressif itself describes using its VID as "useful in the development stage of a product". The defaults are for the bench and for this repository's examples. **They are not your product's numbers.** Do not ship them in a device you give to others. A different product with the same VID:PID is the same device to Windows, and one product's driver and remembered settings land on the other.

**Where real numbers come from.** Vendor IDs are assigned by the USB Implementers Forum (USB-IF). A VID alone is US$6,000; it is included in the US$5,000 annual membership; the USB logo license is separate. Under the current terms a VID cannot be transferred or shared, and the VID owner assigns the PIDs under it. That is not a realistic amount for a small project, so there are three kinds of places that will let you use a number.

- **Chip vendor registries.** Espressif runs [espressif/usb-pids](https://github.com/espressif/usb-pids) for devices built on its USB-capable chips. A pull request describing the device in `allocated-pids.txt` gets a PID under `0x303A`. It is free, but Espressif "reserves the right to reject a pull request for any reason", says approval "should not be used in marketing materials", and describes the whole scheme as "a cooperative way for separate products not to pick the same number".
- **Open-source registries.** [pid.codes](https://pid.codes/howto/) assigns PIDs under VID `0x1209` to projects that publish their hardware or firmware under a recognized open-source license. The [Openmoko registry](https://github.com/openmoko/openmoko-usb-oui) does the same under `0x1D50`. pid.codes is open about its VID: it was "issued by the USB-IF to a company that has since gone out of business, who donated it", obtained "before the USB-IF changed its terms to forbid transfers". Both are run by volunteers handling pull requests by hand, and some requests have taken months to merge.
- **Test numbers.** pid.codes reserves `0x1209:0x0001`–`0x0010` for testing. Anyone may use them on the bench; none may ship in a product.

None of these is a USB-IF assignment. Someone who owns a VID is letting you use one number under it, so that you do not collide with whoever came first. Understood as such, it is enough for a product that does not seek USB-IF certification.

**A product needs one PID.** A PID identifies "the product as you want the host to see it", not a firmware configuration. Windows keys device instances on VID, PID and serial, and driver binding follows the descriptors on every enumeration ([6.2](#62-windows)). Under one PID you can add, move and remove Functions. There are only two reasons to use another number:

- It should look like **a different product** to people or to host applications: a different model, or a bootloader that must never be confused with the application (which is why the ESP32's own ROM bootloader has its own PID).
- You want the host to treat it as a device it has never met, discarding everything it remembered. Per unit, changing the serial does the same.

**The serial number is the third key.** With a `serialNumber`, one physical device is one instance whichever port it is plugged into. Make the string unique per unit, and keep it across firmware updates. Without one, Windows keys the instance on the port path, so the same board in a different socket is a different device with different settings. If several units may be plugged in at once, the serial is also how the host tells them apart.

How Windows treats the rest of what `EspUsbDeviceConfig` exposes to the host (`deviceVersion`, `product`, `manufacturer`, the device interface GUID, power attributes) is covered in [6.2](#62-windows).

---

## 2. The basic EspUsbDevice flow

### 2.1 Device and Functions

The v3 API is built from two kinds of object.

| Object | Role | Example |
| --- | --- | --- |
| **Device** (`EspUsbDevice`) | Owns USB as a whole: the controller, EP0, descriptors, the USB worker | One per sketch |
| **Function** | One capability shown to the host. Registers with the Device in its constructor | `EspUsbDeviceHidKeyboard`, `EspUsbDeviceCdcSerial`, `EspUsbDeviceMsc`, ... |

The main Functions:

| Area | Classes |
| --- | --- |
| HID | `EspUsbDeviceHidKeyboard`, `EspUsbDeviceHidMouse`, `EspUsbDeviceHidGamepad`, `EspUsbDeviceHidConsumerControl`, `EspUsbDeviceHidSystemControl`, `EspUsbDeviceHidCustom`, `EspUsbDeviceHidVendor` |
| CDC | `EspUsbDeviceCdcSerial` |
| Vendor | `EspUsbDeviceVendor` |
| MIDI | `EspUsbDeviceMidi` |
| MSC | `EspUsbDeviceMsc`; helpers `EspUsbDeviceMscRamDisk`, `EspUsbDeviceMscFatRamDisk`, `EspUsbDeviceMscSdCard`, `EspUsbDeviceMscFirmwareDisk` |
| Audio | `EspUsbAudioFunction` (UAC1 / UAC2) |
| Video | `EspUsbDeviceVideo` |
| NCM | `EspUsbDeviceNet` |
| CCID | `EspUsbDeviceCcid` |
| DFU | `EspUsbDeviceDfu`; helper `EspUsbDeviceFirmwareUpdate` |

- The Device does not own the Functions. Keep the Device alive longer than its Functions; normally both are globals.
- You can have several Functions of the same kind (two CDC ports, for instance).
- There is no fixed limit on the number of Functions. The limit is the endpoint and FIFO budget ([4.4](#44-endpoint-budget)). Because of string index allocation, more than 251 makes `begin()` fail.

### 2.2 configure, begin, end

The flow is: create the Functions → `configure()` them if needed → `device.begin()`.

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;                                // owns USB as a whole
EspUsbDeviceHidKeyboard keyboard(device);           // registers with device in its constructor
EspUsbDeviceCdcSerial console(device, "Console");   // the name is visible to the host

void setup() {
  Serial.begin(115200);  // Logs. Goes to the USB-UART or USB Serial/JTAG port

  EspUsbBulkStreamConfig buffers;  // capacities are chosen per Function at run time
  buffers.rxCapacity = 1024;
  buffers.txCapacity = 1024;
  if (console.configure(buffers) != ESP_OK) {
    Serial.println("configure failed");
  }

  EspUsbDeviceConfig config;
  config.product = "My Device";
  if (!device.begin(config)) {
    Serial.printf("begin failed: %s\n", device.lastErrorName());
  }
}

void loop() {
  if (device.ready() && console.available()) {
    console.write(console.read());
  }
  delay(1);
}
```

| Stage | What happens | How failure is reported |
| --- | --- | --- |
| Function constructor | Registers with the Device. Allocates no heap | None |
| `configure()` | Sets capacities and modes. Checks only the shape of the values | Returns `esp_err_t` |
| `device.begin(config)` | Builds descriptors, checks the controller budget, allocates each Function's resources, starts USB | Returns `false`; the reason is in `lastError()` |
| Running | The USB worker (a FreeRTOS task) handles transfers. No `task()` or polling call is needed | Each Function's return values |
| `device.end()` | Stops the USB worker and DMA, returns borrowed buffers, frees everything | Returns `esp_err_t`. `ESP_ERR_TIMEOUT` if the worker does not stop in time |

- **USB does not start by itself.** Until `begin()`, the host sees nothing.
- **Stop before changing settings.** The order is `end()` → `configure()` → `begin()`. The host sees an unplug and a replug.
- **Adding a Function while running is refused.** While a refused Function remains, the next `begin()` also fails with `ESP_ERR_INVALID_STATE`. Recreate it after stopping, or re-register it with `device.addClass(&function)`.
- **A failed `begin()` returns everything it took before returning.** Fix the settings and call `begin()` again.
- **`end()` waits up to `config.stopTimeoutMs` (default 5000 ms) for the worker to stop.** If a callback does not return in time, it returns `ESP_ERR_TIMEOUT`. The stop request stays in place, so calling `end()` again after the callback returns completes the stop. `0` waits without a limit. The Device and Function destructors wait until the stop completes.
- **A Function's `begin()` cannot be called.** Only `device.begin()` / `device.end()` start and stop Functions. Writing `console.begin()` is a compile error.

### 2.3 State: started, ready, suspended

| Function | True when |
| --- | --- |
| `started()` | From a successful `begin()` until `end()`. Means resources are held. Also true while waiting for the host or during a reset |
| `ready()` | The host has completed SET_CONFIGURATION (enumeration step 7) |
| `suspended()` | The bus is suspended (no traffic for 3 ms or more; USB 2.0 §7.1.7.6). The configuration survives suspend, so `ready()` stays true |

`device.speed()` returns the speed the host chose at its last bus reset (USB 2.0 §7.1.7.5).

| Value | Meaning |
| --- | --- |
| `EspUsbBusSpeed::None` | Before USB starts, in descriptors-only mode, after `end()`, or no bus reset yet |
| `EspUsbBusSpeed::Full` | 12 Mbps. An HS controller also runs at this speed behind an FS hub or host |
| `EspUsbBusSpeed::High` | 480 Mbps |

- `suspended()` is false before USB starts, in descriptors-only mode ([2.6](#26-checking-descriptors-without-starting-usb)), and after `end()`.
- Suspend is neither a reset nor a stop. Borrowed buffers stay borrowed until they come back through completion, cancellation, reset or `end()`.
- `ready() && !suspended()` does not promise that the next transfer will succeed. Use each Function's return values and completion notifications to know what was sent and what arrived.
- To wake a suspended host, advertise the capability with `config.remoteWakeupSupported = true`, then call `remoteWakeup()` once the host has enabled it (`remoteWakeupEnabled()`). Whether it may be used is the host's decision (USB 2.0 §9.4.1, §9.4.9).

### 2.4 Capacities are decided at run time

In v3, buffer capacities are chosen **per Function, at run time**. There are no build flags.

| Function | Config type | Main fields and defaults |
| --- | --- | --- |
| CDC, Vendor | `EspUsbBulkStreamConfig` | `rxCapacity` 512, `txCapacity` 512, `transferBytes` 512 |
| MIDI | `EspUsbMidiConfig` | `rxPackets` 128, `txPackets` 128 |
| MSC | `EspUsbMscConfig` | `transferBytes` 512 (must be a multiple of the bulk MPS) |
| Audio | `EspUsbAudioConfig` | `playbackBufferFrames` 256, `captureBufferFrames` 256, `eventCapacity` 8 |
| Video | `EspUsbVideoConfig` | `transport` Isochronous, `payloadBytes` 512 |
| NCM | `EspUsbNetConfig` | `ntbInBytes` / `ntbOutBytes` 3200 |
| CCID | `EspUsbCcidConfig` | `messageBytes` 0 (automatic: `max(271, Bulk-OUT MPS)`) |
| DFU | `EspUsbDfuConfig` | `transferBytes` 1024 |

How memory is used:

- **Allocation happens in `begin()`, release in `end()`.** Only the Functions in use, at the capacities chosen. No heap is allocated in the middle of a transfer.
- **Functions you do not use are not linked.** Code and buffers for Functions you never construct do not end up in the firmware.
- **Regions the USB DMA touches are in DMA-capable internal RAM.** Regions only the CPU reads and writes, such as rings and MSC backing storage, may be in PSRAM (`rxBuffer`, `txBuffer` and the like).
- **Buffers for borrowed transfers (`writeDirect` / `readDirect` and similar) must be in DMA-capable internal RAM, on a 64-byte boundary.** PSRAM is refused with `NotDmaCapable`, a misaligned address with `NotAligned`. A refused buffer is not borrowed.

For each Function's capacities and the difference between copy and borrowed modes, see the guides under `docs/classes/`.

### 2.5 Callbacks and the USB worker

USB processing runs in a USB worker (a FreeRTOS task) separate from `loop()`. Callbacks you register with Functions are called on this worker.

- **Keep callbacks short.** Long logging, waiting or file I/O delays the next USB event. Hand heavy work to `loop()` through a queue or similar.
- **Do not wait inside a callback.** Send HID reports with `timeoutMs = 0`; a waiting send from the worker is refused.
- **Do not call `begin()` / `end()` or reconfigure inside a callback.** `end()` from a callback returns `ESP_ERR_INVALID_STATE`.
- Data handed to a callback may be reused once it returns. Copy what you need later into your own buffer.

The worker can be tuned through `EspUsbDeviceConfig`. If the defaults suit you, write nothing.

| Field | Default | Meaning |
| --- | --- | --- |
| `taskStackBytes` | 4096 | Worker stack in bytes. Callbacks use this stack too |
| `taskPriority` | -1 | -1 means `configMAX_PRIORITIES - 1` (highest) |
| `taskCoreId` | -1 | -1 leaves the worker unpinned |
| `eventQueueDepth` | 16 | How many interrupt events can be held at once. 0 is not allowed |
| `stopTimeoutMs` | 5000 | How long `end()` waits for the worker to stop (ms). 0 waits without a limit |

### 2.6 Checking descriptors without starting USB

With `startMode = DescriptorsOnly`, `begin()` builds the descriptors and allocates the Functions' resources but does not start the controller, interrupts or worker. You can check a configuration with no host.

```cpp
EspUsbDeviceConfig config;
config.startMode = EspUsbDeviceStartMode::DescriptorsOnly;
if (device.begin(config)) {
  const uint8_t *descriptor = device.configurationDescriptor(0);
  // started() == true, ready() == false
  device.end();
}
```

The endpoint and FIFO budget checks also run in this mode. A configuration over budget fails here with `ESP_ERR_INVALID_SIZE`.

### 2.7 Reading errors

`begin()` returns a `bool`; the reason for a failure stays in `device.lastError()` (`esp_err_t`). `device.lastErrorName()` gives its name as a string.

```cpp
if (!device.begin(config)) {
  Serial.printf("begin failed: %s (0x%x)\n",
                device.lastErrorName(), unsigned(device.lastError()));
}
```

| Error | Typical causes |
| --- | --- |
| `ESP_ERR_INVALID_SIZE` | Endpoint budget exceeded. The IN endpoints do not fit the transmit FIFO ([4.5](#45-fifo-budget)). A HID packet over 64 bytes at FS or 1024 at HS. A string over 126 UTF-16 code units. MSC `transferBytes` not a multiple of the bulk MPS. An explicit CCID `messageBytes` below the Bulk-OUT MPS. A Vendor FS bulk size other than 8/16/32/64. `bulkInBuffering = Double` does not fit the FIFO. `taskStackBytes` too small |
| `ESP_ERR_NO_MEM` | Out of heap |
| `ESP_ERR_INVALID_ARG` | An invalid setting (`startMode`, `taskPriority`, `taskCoreId`, GUID format, ...). An inconsistent Audio configuration |
| `ESP_ERR_INVALID_STATE` | Already started. Another `EspUsbDevice` is running. A Function whose registration was refused remains. `end()` was called from a USB worker callback |
| `ESP_ERR_NOT_SUPPORTED` | The SoC has no USB OTG. No controller of the requested speed (for example `HighSpeed` on an S3) |
| `ESP_FAIL` | Hardware preparation failed: interrupt allocation, cache synchronization and the like |

- **Read `lastError()` right after `begin()`.** Other calls, such as `addClass()` and HID sends, overwrite it.
- **`lastErrorName()` returns the same name as `esp_err_to_name()`.** Values not in the table above (such as the `ESP_ERR_TIMEOUT` left by a HID send that timed out) are named too.
- `configure()` returns its own `esp_err_t`. When a Function fails inside `begin()`, that Function's reason goes into `lastError()`.
- Failures are detected before the PHY or the host is touched, so while `begin()` is failing nothing has happened on the host side. Suspect the local settings first.

---

## 3. Power and connectors

### 3.1 A device receives VBUS

In host development, "can you supply VBUS" was the first hurdle. On the device side it is the reverse: **the host supplies VBUS**. Power itself rarely trips you up, but these things do happen:

- **Some boards do not feed the OTG connector's VBUS to the SoC supply.** Plugging in only that connector will not boot the board. Power it from the log connector as well.
- **Declare self power.** If the board runs from its own supply, set `config.selfPowered = true`. It is a declaration in the descriptor; make it match the real wiring. The host's power management reads it.
- **VBUS sensing is not used by default.**

#### vbusSensing

| Value | Behavior |
| --- | --- |
| `false` (default) | Behaves as if VBUS were always present. For boards without a sensing circuit |
| `true` | Uses the board's VBUS-valid signal |

- Choose `true` only if the board has a sensing circuit and its signal is wired to the controller. The option does not provide a GPIO or a circuit.
- Most ESP32 boards do not wire VBUS sensing. With `false`, there may be no way to learn that the cable was pulled until the host next resets the bus.
- On the HS port of an older P4 (chip revision v1.x), the session-valid (BSesVld) bit did not reflect VBUS. VBUS detection with a real cable pull has not been tested yet.

### 3.2 bMaxPower

`config.maxPowerMilliamps` (default 100 mA) is the current you declare you will draw from the bus. The descriptor stores it in 2 mA units, up to 500 mA (USB 2.0 §9.6.3). For configurations that draw a lot, such as ones using Wi-Fi, raising it to 500 mA is the straightforward choice. Some hosts drop the port when a device draws more than it declared.

### 3.3 Connector setup during development

| Purpose | Connect to |
| --- | --- |
| Flashing and logs | USB-UART bridge, or USB Serial/JTAG |
| Host under test | The ESP32's USB OTG connector |

A one-connector board (an AtomS3, for instance) is fine as a final product. **During development, use a two-connector board.** If the log drops the moment enumeration starts, you cannot even see `ready()` change.

Another option is to use a second ESP32 running [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) as the host. You can read both sides' logs from the PC at once. This repository's tests ([validation](../validation/README.ja.md)) use that arrangement.

A one-connector ESP32-S3 can still be flashed over the same cable: after a reset the shared PHY returns to USB Serial/JTAG and the ROM loader appears. How a running sketch can get there without the BOOT button (`device.rebootToBootloader()`) is described in [Firmware update over USB](ota-over-usb.md).

---

## 4. ESP32-specific notes

### 4.1 Supported chips and controllers

| Chip | USB device capability | `EspUsbController::Auto` selects | Status |
| --- | --- | --- | --- |
| ESP32-S3 | One FS controller | FullSpeed | Main target. Verified on hardware |
| ESP32-S2 | One FS controller | FullSpeed | Compile-checked only. Not verified on hardware |
| ESP32-P4 (older revision v1.x) | One HS and one FS controller | **HighSpeed** | Verified on hardware |
| ESP32-P4X (revision v3.x) | Same | **HighSpeed** | Verified on hardware |
| ESP32-S31 | — | — | Not supported |
| ESP32 (original), C3, C6, ... | No USB OTG | — | Does not work |

The reference Arduino-ESP32 version is **3.3.12**.

### 4.2 Choosing HS or FS on ESP32-P4

Choose with `config.controller`.

| Value | Controller |
| --- | --- |
| `Auto` (default) | HS if the SoC has one (P4), otherwise FS (S2/S3) |
| `HighSpeed` | The HS controller. If there is none, `begin()` fails with `ESP_ERR_NOT_SUPPORTED` |
| `FullSpeed` | The FS controller |

| | FS controller | HS controller |
| --- | --- | --- |
| PHY | SoC's built-in FS PHY | UTMI HS PHY |
| Default pins | GPIO26 = D−, GPIO27 = D+ | The board's UTMI wiring |
| Speed | 12 Mbps | 480 Mbps (FS when attached to an FS host) |
| Endpoint numbers | 1–6 | 1–15 |
| Non-control IN endpoints | 4 | 7 |
| FIFO | 256 words (1 KB) | 1024 words (4 KB) |

How to choose:

- **Need bandwidth, many Functions, or large packets** → HS.
- **Connecting to the board's FS connector, or carrying over an FS configuration unchanged** → FS.
- Which connector is wired to which controller **varies by board**. Check the schematic. A common layout is a third connector, neither USB Serial/JTAG nor the GPIO26/27 FS pair, for HS.
- If `config.controller` does not match the connector the cable is in, `begin()` succeeds but the host sees nothing.

The FS PHY shares pins with USB Serial/JTAG. `usb_wrap_ll_phy_select(&USB_WRAP, 0)` moves the FS controller to GPIO24/25 (shown as a comment in [`P4FullSpeedDevice`](../examples/P4FullSpeedDevice/README.md)). USB Serial/JTAG then moves to GPIO26/27, so a Serial/JTAG connection on GPIO24/25 is lost.

#### Older P4 and P4X

The library source has no code that distinguishes the older P4 from the P4X. The difference is in the build settings.

| | Older P4 (v1.x) | P4X (v3.x) |
| --- | --- | --- |
| Arduino `ChipVariant` | `prev3` | `postv3` |
| Example profile | `esp32p4` | `esp32p4x` |
| Revisions the SDK supports | v1.x | **v3.1 and later** |

- The board menu says "v3.00 or newer", but the shipped SDK's minimum revision is 3.1. v3.0 silicon is not treated as supported.
- Endpoint counts and FIFO size are the same on both.

### 4.3 What changes at HS

At HS the bulk MPS goes from 64 to 512 bytes, and the interrupt limit from 64 to 1024 bytes (USB 2.0 §5.7.3, §5.8.3). So **a configuration that works at FS can fail in `begin()` at HS.**

| Item | Requirement at HS |
| --- | --- |
| MSC `transferBytes` (default 512) | Must be a multiple of 512. 64 or 256 gives `ESP_ERR_INVALID_SIZE` |
| CCID `messageBytes` | The default 0 (automatic) becomes 512. An explicit value below 512 gives `ESP_ERR_INVALID_SIZE` |
| Borrowed receive (`readDirect`) capacity | Must be a multiple of 512 (a multiple of 64 at FS) |
| CDC, Vendor, MIDI `transferBytes` | Values below 512 are accepted |
| Vendor bulk packet size | Always 512 (the constructor's FS value is not used) |
| HID interrupt endpoints | Up to 1024 bytes (`EspUsbDeviceHidCustom`, `EspUsbDeviceHidVendor`). Read the actual value with `inputPacketSize()` |
| UAC2 feedback | 16.16 in 4 bytes (FS: 10.14 in 3 bytes, USB 2.0 §5.12.4.2) |

When the HS controller is selected, these checks apply even if the link ends up at FS, so that the HS MPS can be handled.

### 4.4 Endpoint budget

**This is the first limit you hit on the device side.** The controller has a maximum endpoint number and a maximum count. `begin()` refuses a configuration that exceeds them with `ESP_ERR_INVALID_SIZE`, before the PHY starts.

| Controller | Highest endpoint number | Non-control IN | Non-control OUT |
| --- | --- | --- | --- |
| ESP32-S2/S3 | 6 | 4 | 6 |
| ESP32-P4 FS | 6 | 4 | 6 |
| ESP32-P4 HS | 15 | 7 | 15 |

One number can be shared by an IN and an OUT endpoint. Per-Function use:

| Function | IN | OUT |
| --- | --- | --- |
| HID (all HID Functions share one interface) | 1 | 1 if there is an output report |
| CDC ACM | 2 (notification, data) | 1 |
| CDC-NCM | 2 (notification, data) | 1 |
| Vendor | 1 | 1 |
| MIDI | 1 | 1 |
| MSC | 1 | 1 |
| CCID | 1 (plus 1 for notifications if the slot is removable) | 1 |
| Audio | 1 for capture, plus 1 for feedback with UAC2 playback | 1 for playback |
| Video | 1 | 0 |
| DFU | 0 (uses EP0 only) | 0 |

What bites is the **4 IN endpoints** of S2/S3 and P4 FS.

- HID + CDC + MSC uses 1 + 2 + 1 = 4 IN, exactly the limit. Adding Vendor does not fit.
- Two CDC ports use 4 and fit. Three use 6 and do not.
- DFU uses no endpoints, so it can be added even when the budget is spent.

To find out whether a configuration fits, set the `DUMP_ENABLE_*` switches of [`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/README.md) to it, build, and read the `endpoint budget` lines at the end. No host is needed.

### 4.5 FIFO budget

Separately from the endpoint count, the **FIFO** inside the controller is shared. It is a fixed region used by the receive FIFO and by the transmit FIFO of every IN endpoint. Adding RAM does not make it larger.

| Controller | FIFO | Roughly left for IN endpoints |
| --- | --- | --- |
| S2/S3, P4 FS | 256 words (1 KB) | About 650 bytes (with a 64-byte largest OUT packet) |
| P4 HS | 1024 words (4 KB) | About 2.6 KB (with a 512-byte largest OUT packet) |

The estimate is what remains after the receive FIFO (a fixed part, the largest OUT packet, and a share per endpoint) and EP0. A large OUT packet (Audio playback, for example) enlarges the receive FIFO and leaves less.

- **`begin()` does the transmit FIFO arithmetic for every IN endpoint in the configuration** and fails with `ESP_ERR_INVALID_SIZE` if they do not fit. Starting anyway would give a device whose endpoints fail to open at SET_CONFIGURATION, or, with isochronous IN, one that enumerates and binds a driver but delivers nothing. The check runs before the PHY is touched.
- Example: at P4 HS, up to five Vendors (512-byte bulk IN) fit. Six fit the endpoint count but not the FIFO, and fail with `ESP_ERR_INVALID_SIZE`. At FS, a camera (default 512-byte payload) together with Audio capture tends to hit this limit.
- **Bulk IN double buffering** (`config.bulkInBuffering`) gives bulk IN endpoints two packets of FIFO when there is room, which speeds up sustained sending.

| Value | Behavior |
| --- | --- |
| `Auto` (default) | Doubles every bulk IN endpoint if they all fit; otherwise changes nothing |
| `Single` | One packet each |
| `Double` | Doubles every bulk IN endpoint; if that does not fit, `begin()` fails with `ESP_ERR_INVALID_SIZE` |

`device.bulkInDoubleBuffered()` returns which endpoints actually got two packets (a bitmap by endpoint number). If a stream is slower than expected, check whether it is 0.

### 4.6 Other limits

| Item | Limit | When exceeded |
| --- | --- | --- |
| Number of Functions | 251 | `begin()` fails with `ESP_ERR_INVALID_SIZE` |
| Strings (manufacturer, product, serial, Function names) | 126 UTF-16 code units each | `begin()` fails with `ESP_ERR_INVALID_SIZE`; nothing is truncated |
| Configuration descriptor | 65535 bytes (`wTotalLength` is 16 bits) | `begin()` fails with `ESP_ERR_INVALID_SIZE` |
| WebUSB URL | 252 bytes after removing `http(s)://` | `begin()` fails with `ESP_ERR_INVALID_SIZE` |

Strings are given as UTF-8. `begin()` converts and copies them to UTF-16, so non-ASCII text works. Invalid UTF-8 is refused.

### 4.7 Not usable together with Arduino-ESP32's own USB

This library initializes and drives the OTG controller itself. Arduino-ESP32's standard USB stack uses the same controller, so the two cannot be used together.

- **Do not call `USB.begin()`.** `USBHIDKeyboard`, `USBHIDMouse`, `USBCDC` and the like cannot be mixed in either.
- **On ESP32-S3, set the board menu's `USB Mode` to "USB-OTG (TinyUSB)".** The S3 default is "Hardware CDC and JTAG", which routes D+/D− to USB Serial/JTAG. In `arduino-cli`, `esp32:esp32:esp32s3:USBMode=default` is "USB-OTG (TinyUSB)".
- **Leave `USB CDC On Boot`, `USB MSC On Boot` and `USB DFU On Boot` disabled.**
- **On ESP32-P4, the HS controller is separate from USB Serial/JTAG.** The examples build with `USBMode=default`. The tests also verified building the P4X with `USBMode=hwcdc,CDCOnBoot=cdc`, sending logs to USB Serial/JTAG while EspUsbDevice uses the HS controller.
- **TinyUSB build flags (`CFG_TUD_*` and so on) are not used.** v3 does not use TinyUSB's device stack; it runs on its own core. An old flag left in `build_opt.h` or elsewhere causes a compile error that points to the migration (even if set to 0).

With USB OTG in use on an S3, `Serial` goes to the UART. That is why a separate log port is needed ([3.3](#33-connector-setup-during-development)).

### 4.8 Logs

When `begin()` fails, print `lastErrorName()` first ([2.7](#27-reading-errors)). Enumeration failures are often invisible from the device side, so **always look at the host OS logs too** ([chapter 6](#6-observing-yourself-from-the-host-os)). In device work, what the host received and what it disliked is the best information you have.

---

## 5. Bring-up procedure

The steps to build a new USB device. **Go in order and skip nothing.** Most failures come from puzzling at a later stage over an earlier-stage problem (connector, build settings, budget).

### Step 0. Desk check (before writing code)

- In the schematic, find **which connector goes to the ESP32's USB OTG**. On P4, also which of HS and FS
- Make sure the log connector and the host connector can be kept separate
- Make sure the cable carries data (is not charge-only)
- Check the board settings (S3: `USB Mode` is "USB-OTG (TinyUSB)"; P4: `ChipVariant` matches the chip revision)

### Step 1. Check startup and enumeration

Flash [`EspUsbDeviceBringUpCheck`](../examples/Info/EspUsbDeviceBringUpCheck/README.md). It enumerates as a HID keyboard but never sends a key, so it is safe to leave plugged into a PC.

- Does `BEGIN ok` appear? → If not, look up the printed error name in [2.7](#27-reading-errors). The host is not involved yet
- Connected to a host, does `MOUNTED` appear? → If not, it is electrical or descriptors. A checklist prints after 10 seconds
- Press CapsLock on the host. Does `HOST_OUTPUT_REPORT` appear? → The host-to-device direction works too
- Suspend the host. Does `SUSPENDED` appear?

If all of this passes, the board and build settings are fine. Later problems can be attributed to your own configuration.

### Step 2. Decide what to look like

Pick the class that fits from [the table in 1.7](#17-choosing-what-to-look-like). This choice shapes the whole project, so decide it first.

### Step 3. Run the minimal configuration from an example

Flash the example for the class you chose, unchanged.

| Class | Examples |
| --- | --- |
| HID keyboard / mouse | [`Keyboard`](../examples/Keyboard/README.md), [`Mouse`](../examples/Mouse/README.md) |
| HID gamepad / media keys | [`Gamepad`](../examples/Gamepad/README.md), [`MediaKeys`](../examples/MediaKeys/README.md) |
| HID custom reports | [`CustomHID`](../examples/CustomHID/README.md), [`VendorHID`](../examples/VendorHID/README.md) |
| CDC ACM | [`Serial`](../examples/Serial/README.md), [`SerialMulti`](../examples/SerialMulti/README.md) |
| USB MIDI | [`MIDI`](../examples/MIDI/README.md), [`MIDIController`](../examples/MIDIController/README.md) |
| MSC | [`MSC`](../examples/MSC/README.md), [`MSCFatRamDisk`](../examples/MSCFatRamDisk/README.md), [`MSCSdCard`](../examples/MSCSdCard/README.md) |
| USB Audio | [`AudioSpeaker`](../examples/AudioSpeaker/README.md), [`AudioMicrophone`](../examples/AudioMicrophone/README.md), [`AudioHeadset`](../examples/AudioHeadset/README.md) |
| USB Video | [`VideoCamera`](../examples/VideoCamera/README.md) |
| CDC-NCM | [`UsbNetwork`](../examples/UsbNetwork/README.md) |
| CCID | [`SmartCardReader`](../examples/SmartCardReader/README.md) |
| Vendor bulk | [`USBVendor`](../examples/USBVendor/README.md) |
| DFU, firmware update | [`FirmwareDFU`](../examples/FirmwareDFU/README.md), [`FirmwareMSC`](../examples/FirmwareMSC/README.md) |
| P4 HS / FS | [`P4HighSpeedDevice`](../examples/P4HighSpeedDevice/README.md), [`P4FullSpeedDevice`](../examples/P4FullSpeedDevice/README.md) |

**Write your own code only after the example works.** When something fails, you can then tell a library problem from a configuration problem. How to read the examples is covered in [Working through the examples](example-workflow.md).

### Step 4. Check what you are declaring

Set the `DUMP_ENABLE_*` switches of [`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/README.md) to your configuration, build, and read the output. No host is needed.

Check:

- How many interfaces, and each one's class / subclass / protocol
- Each endpoint's type, direction, MPS and interval
- How many bytes the HID report descriptor is
- **Whether it fits the endpoint budget**, and what happened to bulk IN double buffering

To check inside your own sketch, use `DescriptorsOnly` from [2.6](#26-checking-descriptors-without-starting-usb).

### Step 5. Compare with what the host received

Step 4 is "what you meant to send". Now check that it matches what the host actually received.

```sh
# Linux
lsusb -v -d 303a:4051
```

If they match, the descriptor layer is fine. If they do not, or nothing shows on the host, the problem is lower down (enumeration, electrical, controller selection). Chapter [6](#6-observing-yourself-from-the-host-os) covers observation in detail.

### Step 6. Narrow it down by hand

When the class works but "the host software does not react as expected", [`EspUsbDeviceConsole`](../examples/Info/EspUsbDeviceConsole/README.md) helps. You can try one transfer at a time without rebuilding.

```
> state
> key 0x04                            # does the host react to this usage?
> report 1 00 00 05 00 00 00 00 00    # send a raw report
> vendor 01 02 03 04                  # raw bytes on the vendor bulk IN
```

Everything coming from the host (HID output reports, protocol switches, vendor control requests, bulk OUT) is printed with a `HOST_` prefix. **Watching this while the host's own software runs shows the initialization sequence the host expects.**

### Step 7. Grow into a composite device

Once single Functions work, combine them. There are two constraints:

- **Endpoint and FIFO budgets** ([4.4](#44-endpoint-budget), [4.5](#45-fifo-budget)). Check with DescriptorDump first
- **The host OS.** Some OSes behave differently depending on interface order and the presence of IADs

Several HID Functions add report IDs, not interfaces. [`CompositeHidCdcMsc`](../examples/CompositeHidCdcMsc/README.md) and [`KeyboardMouse`](../examples/KeyboardMouse/README.md) are good references.

Registration order decides only interface and endpoint numbers. HID works the same whether registered first or last. But things such as Windows COM port numbers attach to interface numbers, so reordering changes what the host sees ([6.2](#62-windows)). After rearranging, recheck on the target OS.

### Step 8. Check robustness

Enumerating once and staying usable are different things.

- Suspend and resume the host (the change in `suspended()`, and transfers after resume)
- Unplug and replug the cable many times
- Repeat `end()` → `begin()` (the host sees an unplug and replug)
- Reboot the host and see whether it is recognized during boot (BIOS / UEFI). HID switches to boot protocol there

### Step 9. Record the result

- Keep the smallest sketch that works
- **Write down the host OS and its version, the chip and its revision, and the controller (HS / FS) you used.** In device work, this is the most important record

---

## 6. Observing yourself from the host OS

In host development you observed the other device. On the device side you observe **how the host sees you**. The tools differ by OS.

### 6.1 Linux

```sh
# What happened (start before plugging in)
sudo dmesg -w
udevadm monitor --udev

# All descriptors
lsusb -v -d 303a:4050

# Tree and speed (12M / 480M)
lsusb -t

# HID report descriptor
sudo usbhid-dump -d 303a:4050

# What arrives as HID keyboard / mouse input
sudo evtest

# Capture traffic to and from you
sudo modprobe usbmon
sudo wireshark   # pick usbmonX
```

`dmesg` matters most. An invalid descriptor gets a reason, and you see which driver bound. Whether the P4 HS controller enumerated at HS is shown by `480M` in `lsusb -t`. On the device side, read it with `device.speed()` ([2.3](#23-state-started-ready-suspended)).

### 6.2 Windows

- **Device Manager**: "Unknown USB Device (Device Descriptor Request Failed)" is a failure at the descriptor stage; a "!" with Code 10 / Code 43 is a driver binding failure.
- **[USB Device Tree Viewer](https://www.uwe-sieber.de/usbtreeview_e.html)**: the most useful tool on Windows. Shows both the raw descriptor bytes and the errors Windows reported.
- **Event Viewer**: details of binding failures.
- **`pnputil /enum-devices /connected`**: which driver is bound.

> The observations in this section were made on Windows 11 25H2 (build 26200.9457) with **EspUsbDevice v2** firmware. Windows decides from descriptors and identity alone, so the same rules are expected to hold for v3, but **they have not been re-measured with v3**. Windows driver binding and class behavior are also unverified with v3.

#### What Windows re-reads and what it keeps

"Windows caches descriptors" is too coarse to act on. Changing one thing at a time gave this:

| What changed | Does Windows follow? |
| --- | --- |
| Interfaces, endpoints, packet sizes, which Functions exist and their order | **Yes, every time** |
| Which driver should bind | **Yes, every time** |
| `deviceInterfaceGuid` (vendor revision allowed to move = default) | **Yes** |
| `deviceInterfaceGuid` (vendor revision pinned) | **No**. The old GUID stays |
| `deviceVersion`, `product` | **Yes**. Hardware ID and bus-reported description update; the Device Manager display name does not |
| `vid`, `pid`, `serialNumber` | **Yes**. To Windows it is a different device, so everything starts fresh |

- **Standard descriptors are read on every enumeration.** A single vendor interface that becomes composite is split into child devices, and becomes one node again when you go back. Driver selection follows the descriptors, so there is nothing for you to do.
- **The exception is Microsoft OS 2.0 registry properties.** Windows reads `DeviceInterfaceGUIDs` once per instance and keeps it, re-reading only when the descriptor set's `wVendorRevision` changes. By default this library derives the revision from the set's contents, so a changed set is re-read automatically. Only pinning `msOs20VendorRevision` by hand leaves the old GUID in place.
- **Changing identity starts fresh.** Windows identifies the instance by VID, PID and serial (`USB\VID_303A&PID_4080\GUID-TEST-1`). Change any of them and it is a new instance. The old one stays in the registry, unused.
- **Without a `serialNumber`, the instance is keyed by port path** (`USB\VID_303A&PID_4080\8&2EBC545B&0&4`). The same board in a different socket is a different device to Windows.
- **Child instances of a composite device hang off interface numbers** (`USB\VID_303A&PID_4090&MI_01\...`), and it is the child that holds settings. The path an application gets from GUID enumeration moves with the Function. Enumerate by GUID every time; do not store the path.

**COM ports attach to interface numbers.** One CDC Function, one PID and serial:

| Configuration | CDC child | COM |
| --- | --- | --- |
| CDC alone | `MI_00` | COM16 |
| CDC + vendor (CDC registered first) | `MI_00`, same instance | **Still COM16** |
| vendor + CDC (vendor registered first) | `MI_01`, **new instance** | **COM34** |
| Back to CDC alone | `MI_00` | Back to COM16 |

Adding a Function *after* the serial port does not move its COM number. Moving the serial port to another interface number gives it a new one. If users have COM16 saved in their terminal software, whether it still works after an update is decided by your Function registration order.

**A value left in the registry is not a device left behind.** Windows writes `DeviceInterfaceGUIDs` (and the COM `PortName`) and never deletes them. After rearranging, old values remain, but enumerating the way an application does (`SetupDiGetClassDevs` with `DIGCF_PRESENT | DIGCF_DEVICEINTERFACE`) returned only the live interfaces.

#### Identity fields

| Field | Default | What changing it did |
| --- | --- | --- |
| `vid`, `pid` | `0x303A`, `0x4000` | New instance. Driver and GUID recorded afresh |
| `serialNumber` | none | Likewise a new instance. Without one, the instance is the port path |
| `deviceVersion` (bcdDevice) | `0x0100` | On the next connection the hardware ID becomes `...&REV_0200` and so on. Same instance and driver |
| `product` | `"EspUsbDevice"` | The bus-reported description shows the new string. The Device Manager name is set when the driver is installed and changes only when Windows reconfigures the device |
| `manufacturer` | `"EspUsbDevice"` | No visible change. Windows' manufacturer column comes from the driver INF |
| `deviceInterfaceGuid` | Library default | Applied on the next connection with an automatic revision; kept with a pinned one. Give your product its own GUID |
| `msOs20VendorRevision` | 0 (automatic) | The only field that can make a GUID change not take effect |
| `msOs20Layout` | Auto | **Forcing `Subsets` on a single-interface device leaves it with no driver (Code 28).** Auto picks the right shape |
| `msOs20CcgpDevice` | off | Parent is `usbccgp`, child `MI_00` is `WINUSB`; the GUID goes only on the child |
| `webusbEnabled`, `webusbUrl` | off | Nothing happens on the Windows side. Browsers read these |
| `maxPowerMilliamps`, `selfPowered` | 100, false | Visible only in the configuration descriptor |
| Function name (CDC constructor `name` and the like) | none | A composite child's description becomes that name; otherwise `product` |

#### Measuring it yourself

Windows behavior changes between builds. Before a product depends on any row above, measure again on the target Windows and record the build (`winver`, or `DisplayVersion` and `CurrentBuild`.`UBR` in the registry).

1. **Change one thing per flash.** To see "what happens on a PC that has met this device before", keep the same PID. That is what matters for a shipped product.
2. **Do not attach the device to WSL.** A device attached through usbipd is a USBIP device to Windows and nothing binds to it.
3. **Read Windows first, the board second.** Some UART adapters reset the board when the port opens, which is a reconnect.
4. **Do not trust `OK` from `Get-PnpDevice`.** It returned OK for children whose start was pending or had failed. Read `Microsoft-Windows-Kernel-PnP/Configuration` in Event Viewer (400 = configured, 410 = started, 411 = start failed, 430 = further installation required). WinUSB binding may not be recorded in `setupapi.dev.log`.
5. **Check the device interface, not just the driver.** Use `pnputil /enum-interfaces /class {GUID}` and the same enumeration an application uses.
6. **Read what Windows kept.** Compare the instance's `Device Parameters` (`DeviceInterfaceGUIDs`, `PortName`) with what the device sent.

The PC-side script of the v2 test tool [`windows_identity`](https://github.com/tanakamasayuki/EspUsbDevice/tree/3fed50d0d777f600432b71381faccd93adb9a202/tests/manual/windows_identity/) does steps 4–6 for every node matching a VID/PID in one command from a WSL shell. The sketch in the same directory is written against the v2 API and does not build with v3.

### 6.3 macOS

- **System Information → USB**: tree and descriptor summary
- `ioreg -p IOUSB -l -w 0`: more detail
- Capture with Wireshark on the `XHC20` interface

v3 has not been checked on macOS.

### 6.4 Using another ESP32 as the host

If the target is an embedded device rather than a PC, an ESP32 running [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) can act as the host. You read both sides' logs from the PC at once, which speeds up isolation. This repository's tests ([validation](../validation/README.ja.md)) use this arrangement.

### 6.5 Comparing with a device that works

When the host dislikes your device, the shortest path is to **put a commercial device of the same class side by side**.

```sh
lsusb -v -d <vid:pid of the commercial device> > known-good.txt
lsusb -v -d 303a:4051                          > mine.txt
diff -u known-good.txt mine.txt
```

Interface order, IAD presence, `bInterfaceProtocol`, endpoint intervals: items that are optional in the specification but that real host drivers assume show up in the diff.

---

## 7. Troubleshooting

A symptom-indexed list lives in [troubleshooting.md](troubleshooting.md). Only the main items are here.

| Symptom | Likely cause | Check / fix |
| --- | --- | --- |
| `begin()` fails with `ESP_ERR_INVALID_SIZE` | Endpoint or FIFO budget exceeded; at HS, MSC `transferBytes` not a multiple of the MPS; etc. | Read the budget in DescriptorDump. Remove a Function. On P4, use HS ([4.3](#43-what-changes-at-hs), [4.4](#44-endpoint-budget), [4.5](#45-fifo-budget)) |
| `begin()` fails with `ESP_ERR_NO_MEM` | Out of heap | Reduce capacities. Put rings and similar storage in PSRAM ([2.4](#24-capacities-are-decided-at-run-time)) |
| `begin()` fails with `ESP_ERR_NOT_SUPPORTED` | A chip without USB OTG, or a controller it does not have | Check it is S2 / S3 / P4, and check `config.controller` |
| `begin()` fails with `ESP_ERR_INVALID_STATE` | Double `begin()`; a Function added while running remains | `end()` and reconfigure ([2.2](#22-configure-begin-end)) |
| `end()` returns `ESP_ERR_TIMEOUT` | A callback did not return, so the worker did not stop within `stopTimeoutMs` | Keep callbacks short. Call `end()` again after it returns ([2.2](#22-configure-begin-end)) |
| `begin()` succeeds but nothing shows on the host | Wrong connector (UART side, Serial/JTAG side) | Check the schematic ([1.2](#12-which-connector-is-the-device-side)) |
| Same | S3 `USB Mode` is Hardware CDC and JTAG | Set "USB-OTG (TinyUSB)" ([4.7](#47-not-usable-together-with-arduino-esp32s-own-usb)) |
| Same | Charge-only cable | Use a data cable |
| Same (P4) | Controller and connector do not match | Set `config.controller` to the connector you used (HS / FS) ([4.2](#42-choosing-hs-or-fs-on-esp32-p4)) |
| P4 HS controller enumerates at 12 Mbps | The host or hub is FS | Check whether `device.speed()` is `Full`. Fine if intended. For HS, use an HS port |
| Windows: "Device Descriptor Request Failed" | Invalid descriptor, or it could not be returned | Look at raw bytes in USB Device Tree Viewer and compare with DescriptorDump |
| Changed something but the host still shows the old value | A value Windows keeps | See what is kept in [6.2](#62-windows). Change PID or serial to start fresh |
| Reaches SET_CONFIGURATION but no driver loads | Class / subclass / protocol combination | Diff against a commercial device of the same kind ([6.5](#65-comparing-with-a-device-that-works)) |
| Cannot open the vendor interface on Windows | WinUSB not bound | Check the Microsoft OS 2.0 descriptor. Set `msOs20Layout` back to Auto |
| HID key input comes out garbled | Host keyboard layout mismatch | Match `keyboard.setLayout()` to the host layout |
| Keyboard does not work in BIOS / UEFI | Switched to boot protocol | Check `HOST_HID_PROTOCOL` in Console. NKRO folds to 6 keys in boot protocol |
| Sent but nothing reaches the host | Sending before `ready()` | Check `device.ready()` before sending, and check the return value |
| No log, or it stops right after `begin()` | One-connector board; log and device share the port | Use a two-connector board, or an ESP32 as host ([3.3](#33-connector-setup-during-development)) |
| Stops working after repeated replugging | State from the previous connection remains | Reproduce with repeated `end()` / `begin()`. Without VBUS sensing, an unplug is not visible until the next bus reset |
| MSC fails in `begin()` at HS | `transferBytes` not a multiple of 512 | Make it a multiple of 512 ([4.3](#43-what-changes-at-hs)) |
| No idea at all | Not looking at host logs | Look at `dmesg -w` (Linux) or USB Device Tree Viewer (Windows) first ([chapter 6](#6-observing-yourself-from-the-host-os)) |

---

## 8. Tools

### For users (examples/Info/)

| Tool | Purpose |
| --- | --- |
| [`EspUsbDeviceBringUpCheck`](../examples/Info/EspUsbDeviceBringUpCheck/README.md) | **Run first.** `begin()` result, enumeration, suspend, host-to-device path. Prints a checklist when enumeration does not happen |
| [`EspUsbDeviceDescriptorDump`](../examples/Info/EspUsbDeviceDescriptorDump/README.md) | **Run next.** Every descriptor the library built, and the endpoint budget. No host needed |
| [`EspUsbDeviceConsole`](../examples/Info/EspUsbDeviceConsole/README.md) | **For narrowing down.** Send HID reports and vendor transfers by hand from serial, and print every request from the host |

For per-feature examples see [examples](../examples/README.md).

### Host-side tools

- Linux: `lsusb`, `dmesg`, `usbhid-dump`, `evtest`, Wireshark (usbmon)
- Windows: USB Device Tree Viewer, Device Manager, Event Viewer, `pnputil`
- v2 test tools ([v2 `tests/manual/`](https://github.com/tanakamasayuki/EspUsbDevice/tree/3fed50d0d777f600432b71381faccd93adb9a202/tests/manual/)): the PC-side PyUSB scripts can read standard descriptors. The sketches bundled with them use the v2 API and do not build with v3.

---

## 9. What has been verified

v3's tests live in [validation](../validation/README.ja.md). They are test sketches separate from the examples; `--combo all` runs six wiring combinations and more than 200 tests.

| Setup | What was checked |
| --- | --- |
| Two ESP32-S3 boards (FS) | Main test environment. All classes and the common core |
| One ESP32-P4, HS controller ↔ FS controller (FS link) | Basic traffic for all 10 classes, both controllers, restart, storage in PSRAM |
| Older P4 and P4X connected at HS (480 Mbps, both directions) | The S3 tests extended to HS, plus HS-specific tests |
| P4X and a Linux PC (HS) | MSC, DFU and other traffic, checked with pyusb |

Not verified:

- ESP32-S2 hardware (compile only)
- ESP32-S31 (not supported)
- v3 class behavior and driver binding on Windows and macOS
- VBUS detection with a real cable pull (`vbusSensing = true`)

---

## 10. References

- [USB 2.0 Specification](https://www.usb.org/document-library/usb-20-specification): the specification itself
- [USB Class Codes](https://www.usb.org/defined-class-codes): list of class codes
- [USB Device Class Documents](https://www.usb.org/documents): HID, CDC, MSC, CCID, UAC and other class specifications
- [HID Usage Tables](https://usb.org/document-library/hid-usage-tables-16): HID usages; needed when writing report descriptors
- [USB Made Simple](https://www.usbmadesimple.co.uk/): an introduction for beginners
- [USB Device Tree Viewer](https://www.uwe-sieber.de/usbtreeview_e.html): observation on Windows
- [pid.codes](https://pid.codes/): VID/PIDs for open-source projects
- [ESP32-P4 chip revision v3.x User Guide](https://documentation.espressif.com/esp32-p4-chip-revision-v3.x_user_guide_en.html): differences between the older P4 and the P4X
- This repository's [README](../README.md): installation and support status
- [USB Device Development Guide (Advanced)](usb-device-advanced.md): internals, descriptor layout, callback context
- [Working through the examples](example-workflow.md): how to try and read the examples
- [Migrating from v2](migrating-from-v2.md), [Migrating from Arduino-ESP32 USB](migrating-from-arduino-esp32-usb.md)
- [Firmware update over USB](ota-over-usb.md): per-chip boot modes, entering them from a sketch, OTA paths
- [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost): the library that makes an ESP32 the host. [USB Host Development Guide](https://github.com/tanakamasayuki/EspUsbHost/blob/main/docs/usb-host-guide.md)
