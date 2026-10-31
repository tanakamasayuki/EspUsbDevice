# Migrating from EspUsbDevice 2.x

> 日本語版: [migrating-from-v2.ja.md](migrating-from-v2.ja.md)

This guide is for sketches written against EspUsbDevice 2.x (the last release
is 2.6.1) that you want to build with 3.0. Most sketches keep their shape: one
`EspUsbDevice`, one object per Function, one `device.begin(config)`. What
changes is where buffer sizes come from, when callbacks may be registered, and
a set of behaviours that v3 corrected to follow the USB and class
specifications.

Coming from the Arduino-ESP32 core's USB API (`USB.begin()`, `USBHIDKeyboard`,
...) instead? See [Migrating from the Arduino-ESP32 USB API](migrating-from-arduino-esp32-usb.md).

## Contents

1. [Read this first](#1-read-this-first)
2. [Migration steps](#2-migration-steps)
3. [Common changes](#3-common-changes)
4. [Changes per class](#4-changes-per-class)
5. [Removed and renamed APIs](#5-removed-and-renamed-apis)
6. [Example mapping](#6-example-mapping)
7. [Not yet verified](#7-not-yet-verified)

## 1. Read this first

### What changed

1. **No build flags.** `build_opt.h`, `CFG_TUD_*` and
   `ESP_USB_DEVICE_CCID_BUFFER_SIZE` are gone. Each Function's buffers are set
   at run time with its `configure()`. v3 uses its own USB stack, not TinyUSB.
2. **Resources are allocated by `begin()` and freed by `end()`.** Only the
   Functions you create are linked. `end()` → `configure()` → `begin()` changes
   sizes without rebuilding.
3. **Callbacks are registered while stopped.** Every `on...()` setter returns
   `esp_err_t`. After `device.begin()` it fails with `ESP_ERR_INVALID_STATE`.
4. **The lifecycle is explicit.** `device.task()` is gone. A Function's own
   `begin()` is no longer public. `started()`, `ready()`, `suspended()` and
   `speed()` report the state. `end()` returns `esp_err_t`.
5. **`begin()` checks the whole configuration first.** The endpoint and FIFO
   budget, packet sizes and class settings are checked before the PHY starts.
   A configuration that cannot work fails `begin()` instead of failing
   enumeration on the host.
6. **Some behaviours changed without a compile error.** Keyboard keys now
   combine, `mouse.move()` keeps held buttons, System Control uses standard
   usage IDs, CDC `write()`/`flush()` wait, MIDI rejects out-of-range values,
   and more. See [Changes you will not see at compile time](#changes-you-will-not-see-at-compile-time).
7. **Borrowed transfers are a common API.** `writeDirect()` / `readDirect()`
   return `EspUsbTransferSubmission`, finish with one typed completion, and
   have no 65535-byte limit.

### What happens if you do not migrate

| v2 code | Result with v3 |
| --- | --- |
| `build_opt.h` with `CFG_TUD_CDC_RX_BUFSIZE`, `CFG_TUD_HID_EP_BUFSIZE`, ... | Compile error: `Remove TinyUSB build flags; configure EspUsbDevice/Function runtime APIs instead` |
| `-DESP_USB_DEVICE_CCID_BUFFER_SIZE=...` | Compile error that names `EspUsbCcidConfig::messageBytes` |
| `device.task()`, `config.startTinyUsb`, `EspUsbDevice::maxCdcPorts()`, `device.hidEndpointSize()` | Compile error (removed) |
| `cdc.begin()`, `keyboard.begin()` and other Function `begin()` calls | Compile error (not public) |
| `EspUsbDeviceVideo::payloadBufferSize()` and other static queries | Compile error (now per instance) |
| `dfu.onError([](uint8_t s) {...})` | Compile error (takes `EspUsbDfuStatus`) |
| `#include <tusb.h>`, `tud_*()` calls | Compile error (TinyUSB is not part of v3) |
| Callback registered after `device.begin()` | Compiles; the setter returns `ESP_ERR_INVALID_STATE` and the callback is never called |
| Behaviour changes in [the table below](#changes-you-will-not-see-at-compile-time) | Compiles; the device behaves differently |

Every `CFG_TUD_*` / `CFG_TUSB_*` flag of v2 and its bundled TinyUSB, and the
v2 capacity flags (`ESP_USB_DEVICE_CDC_PORTS`, `ESP_USB_DEVICE_HID_EP_BUFSIZE`,
`ESP_USB_TINYUSB_AUDIO_*_EP_SIZE`), stops the build with an error. Delete
`build_opt.h` as a whole rather than editing it.

### Changes you will not see at compile time

These compile unchanged and behave differently. Check each one that your
sketch or your host software touches. Section 4 has the details.

| Area | v2 | v3 |
| --- | --- | --- |
| Callback registered after `device.begin()` | Took effect | Refused with `ESP_ERR_INVALID_STATE`; never called |
| Keyboard `pressUsage()` / `pressKey()` (6KRO) | Replaced the held key | Adds to the held keys (up to 6) |
| Keyboard `tapKey()` / `tapUsage()` | Released every key | Releases only what the tap added |
| `mouse.move(x, y)` | Released held buttons | Keeps held buttons |
| System Control | 1 / 2 / 3 | 0x81 / 0x82 / 0x83; integers 1–3 are refused |
| Gamepad hat | Center read as a direction by hosts | Center is the null state |
| Consumer Control usage > 0x3FF | Sent | Refused |
| HID Vendor `sendInput()` too long | Cut | Refused |
| CDC `write()` / `flush()` | Returned at once | Wait up to 1000 ms (`setTxTimeoutMs()`) |
| Vendor ZLP after a multiple of the packet size | Sent | Not sent by default |
| Vendor requests with bRequest 0x01 | Taken by the library while WebUSB was on | Only WebUSB GET_URL is taken; the rest reach the sketch |
| WebUSB URL without a scheme | Treated as `https://` | Sent as the whole URL; browsers reject it |
| MIDI out-of-range channel / data | Clamped and sent | Refused |
| MSC callback returning less than requested | Retried or continued | Medium error |
| MSC medium re-inserted | Next command succeeded | One UNIT ATTENTION (06/28/00) |
| MSC default staging | 4096 bytes | 512 bytes |
| SD card `readOnly(true)` | Medium error on write | Write protection reported |
| Audio `capture.write()` when full | Overwrote old PCM | Partial write |
| UAC2 FS feedback | 4 bytes (16.16) | 3 bytes (10.14) |
| CCID default message size at FS | 320 | 271 |
| NCM default MAC | Chip Ethernet MAC | Same with the locally administered bit set |
| P4 defaults (Vendor TX, Video payload) | Larger on P4 | Same on every chip |

### What you gain

Measured on ESP32-S3 with Arduino-ESP32 3.3.12, comparing v2.6.1 and v3
examples with the same function:

- **Static RAM:** about 59.5 KB → about 22 KB. v2 reserved every class's
  buffers at link time, whether the sketch used the class or not. v3 links
  only the Functions you create. v3 allocates their buffers from the heap in
  `begin()`, so check the free heap after `begin()` too.
- **Flash:** 1.6 % to 10 % smaller.

Other gains, as facts of the implementation:

- No Function-count limit (v2: 6). The controller's endpoints and FIFO decide,
  and `begin()` says so with `ESP_ERR_INVALID_SIZE`.
- ESP32-P4 high speed (480 Mbps) on both P4 revision v1.x and v3.x (P4X).
  `device.speed()` reports the speed the host negotiated.
- Each Function reports why it failed: `startError()`, plus
  `device.lastError()` / `lastErrorName()`.
- Per-instance sizes. Two CDC ports or two Vendor interfaces can have
  different buffers.
- Rings may be supplied by the sketch, including PSRAM.
- CDC is an Arduino `Stream` (`peek()`, `readBytes()`, `setTimeout()`).
- `DescriptorsOnly` start mode builds and checks the descriptors without
  touching the USB hardware.
- Remote wakeup (`remoteWakeupSupported`, `device.remoteWakeup()`) and bus
  suspend status (`device.suspended()`).
- Standard conformance fixes: full-speed bulk packet sizes (USB 2.0 §5.8.3),
  UAC2 full-speed feedback format (USB 2.0 §5.12.4.2), gamepad hat null state,
  System Control usages, write-protect reporting for read-only SD cards, and
  WebUSB / Microsoft OS 2.0 handling that takes only its own requests.

## 2. Migration steps

Work through these in order. Each step builds on the previous one.

1. **Delete `build_opt.h`.** Note the values it set; you will move the ones
   you need into `configure()` in step 2.
2. **Move sizes into `configure()`.** Call each Function's `configure()` in
   `setup()`, before `device.begin()`. Start from the Function's current
   configuration so you change only what you mean to:

   ```cpp
   EspUsbBulkStreamConfig c = cdc.configuration();
   c.rxCapacity = 4096;          // was CFG_TUD_CDC_RX_BUFSIZE
   if (cdc.configure(c) != ESP_OK) { /* report and stop */ }
   ```

   See the mapping per class in [section 4](#4-changes-per-class).
3. **Register callbacks before `device.begin()` and check the result.**

   ```cpp
   if (cdc.onLineState(handleLineState) != ESP_OK) { /* report */ }
   ```

4. **Update the lifecycle.**
   - Remove `device.task()`.
   - Remove calls to a Function's own `begin()` (`cdc.begin()`,
     `keyboard.begin()`, ...). Keep `msc.begin(blockCount, blockSize)`; it only
     sets the geometry.
   - Replace `config.startTinyUsb = false` with
     `config.startMode = EspUsbDeviceStartMode::DescriptorsOnly`.
   - Send only while `device.ready()` is `true`.
   - If you stop the device, check `device.end()`'s `esp_err_t`.
5. **Apply the per-class changes** in [section 4](#4-changes-per-class). Read
   [the silent changes](#changes-you-will-not-see-at-compile-time) even if
   the sketch compiles.
6. **Report failures.** Print `device.lastErrorName()` when `begin()` returns
   `false`, and the Function's `startError()`:

   ```cpp
   if (!device.begin(config)) {
     Serial.printf("begin failed: %s (cdc: %s)\n", device.lastErrorName(),
                   esp_err_to_name(cdc.startError()));
   }
   ```

7. **Check on the host.** Change the PID or serial number while testing so
   the host does not reuse what it cached for the v2 firmware. Compare the
   descriptors (`lsusb -v` on Linux) and test every function the sketch
   exposes. The [USB Device Development Guide](usb-device-guide.md) has the
   bring-up procedure.

## 3. Common changes

### Device configuration

`EspUsbDeviceConfig` keeps its v2 fields (strings, VID/PID, `deviceVersion`,
`selfPowered`, `maxPowerMilliamps`, WebUSB, the Microsoft OS 2.0 fields,
`deviceInterfaceGuid`, `controller`, `bulkInBuffering`, `taskCoreId`). Changes:

| v2 | v3 | Notes |
| --- | --- | --- |
| `startTinyUsb = true` | `startMode = EspUsbDeviceStartMode::Usb` (default) | |
| `startTinyUsb = false` | `startMode = EspUsbDeviceStartMode::DescriptorsOnly` | Builds descriptors and allocates resources; no PHY, interrupt or worker. `ready()` stays `false` |
| `-DCFG_TUD_VBUS_DETECT_HW` | `vbusSensing = true` | Only on boards that route VBUS to the chip |
| fixed worker task | `taskStackBytes` (4096), `taskPriority` (-1 = highest), `eventQueueDepth` (16) | Callbacks run on this task and use its stack |
| `end()` waits without limit | `stopTimeoutMs` (5000, 0 = no limit) | On timeout `end()` returns `ESP_ERR_TIMEOUT`; call it again |
| `webusbUrl = "example.com/x"` | `webusbUrl = "https://example.com/x"` | See below |
| - | `remoteWakeupSupported` | Advertises remote wakeup; the host decides whether to enable it |

Strings are UTF-8 and may be up to 126 UTF-16 code units. Invalid UTF-8 or
longer text makes `begin()` fail instead of being cut.

**WebUSB URL.** v2 treated a URL without a scheme as `https://`. v3 copies the
text as given. Without `http://` or `https://` the URL descriptor uses scheme
255, which means "the text is the whole URL", and a browser rejects
`example.com/x`. Write the scheme.

### Lifecycle and state

```cpp
// v2
void loop() {
  device.task();
  if (device.ready()) keyboard.tapKey('a');
}
```

```cpp
// v3
void loop() {
  if (device.ready() && !device.suspended()) keyboard.tapKey('a');
}
```

| Query | `true` when |
| --- | --- |
| `started()` | `begin()` succeeded and `end()` has not run. Also during reset and while unplugged |
| `ready()` | The host has configured the device (SET_CONFIGURATION) |
| `suspended()` | The bus is suspended. `ready()` stays `true` |
| `speed()` | `EspUsbBusSpeed::None` / `Full` / `High` |

- `begin()` validates, allocates and starts. `end()` stops the worker and DMA,
  returns borrowed buffers and frees memory.
- Do not call `begin()`, `end()` or `configure()` from a callback. `end()`
  from the USB worker returns `ESP_ERR_INVALID_STATE`.
- A Function's destructor and `end()` wait until the device is stopped.
- `ready() && !suspended()` does not promise that the next transfer succeeds.
  Check each send's return value.

### Callbacks

- All registration functions return `esp_err_t`: `ESP_OK` while stopped,
  `ESP_ERR_INVALID_STATE` while started. `device.onAnyControlRequest()` too.
- Callbacks run on the USB worker task. Keep them short. Do not block, wait
  for a transfer, or print large amounts. A common v3 pattern is to set a flag
  or copy data in the callback and act in `loop()`.
- The worker's stack is `taskStackBytes` (4096 by default). Raise it if a
  callback needs more.

### Reading errors

- `device.begin()` returns `bool`. On `false`, `device.lastError()` /
  `lastErrorName()` hold the reason. `lastErrorName()` returns the
  `esp_err_to_name()` text.
- Each Function has `startError()` for its own part of `begin()`.
- `configure()` returns `esp_err_t` immediately.

| Code | Usual meaning |
| --- | --- |
| `ESP_ERR_INVALID_ARG` | A setting is invalid or inconsistent (for example an Audio stream with no format) |
| `ESP_ERR_INVALID_SIZE` | A size does not fit: endpoint or FIFO budget, packet size, buffer smaller than needed |
| `ESP_ERR_INVALID_STATE` | Called at the wrong time: callback registration while started, `end()` from a callback |
| `ESP_ERR_NO_MEM` | Heap allocation failed |
| `ESP_ERR_NOT_SUPPORTED` | The chip or mode cannot do it |
| `ESP_ERR_TIMEOUT` | A wait ran out (HID send, `end()`) |

See [Troubleshooting](troubleshooting.md) for symptoms and causes.

### ESP32-P4 controller and high speed

- `config.controller` existed in v2 and keeps its meaning: `Auto` (HS on P4,
  FS on S2/S3), `FullSpeed`, `HighSpeed`. v2 sketches that relied on TinyUSB
  port numbers (rhport 0/1) no longer need them.
- v3 supports P4 revision v1.x and v3.x (P4X). Set the Arduino board option
  `ChipVariant` to match the chip (`prev3` or `postv3`).
- At HS, bulk packets are 512 bytes. Settings that worked at FS can fail
  `begin()` at HS:

| Setting | Rule at HS |
| --- | --- |
| MSC `transferBytes` | Multiple of 512 (the default 512 is fine; 64 or 256 fail) |
| CCID `messageBytes` | 0 (automatic) or at least 512 |
| `readDirect()` capacity | Multiple of 512 |
| Non-control IN endpoints | 7 on the P4 HS controller (4 on S2/S3 and the P4 FS controller) |

- Some v2 P4 defaults were larger than v3's: Vendor TX 4096 with 4096-byte
  transfers, HID Vendor packets up to 512, Video payload 1023. v3 uses the same
  defaults on every chip. Set them with `configure()` if you need the old
  throughput.

### Build settings

- Arduino-ESP32 3.3.12 is the reference core.
- ESP32-S3: *USB Mode* = "USB-OTG (TinyUSB)" (`USBMode=default`).
- Leave *USB CDC On Boot*, *USB MSC On Boot* and *USB DFU On Boot* disabled.
- Remove `build_opt.h`. If the sketch folder had one only for this library,
  delete the file. Arduino keeps old build results, so build once with a clean
  cache (`arduino-cli compile --clean`).

### Your own Function class

A class derived from `EspUsbDeviceClass` needs more than a rename: the
descriptor writer contract, the lifecycle hooks and the HID base changed. HID
classes derive from `EspUsbHidFunction`. See
[Advanced topics](usb-device-advanced.md).

## 4. Changes per class

### Default capacities

| Function | v2 default | v3 default (`configure()`) |
| --- | --- | --- |
| CDC | RX 512, TX 512 | `rxCapacity` 512, `txCapacity` 512, `transferBytes` 512 |
| Vendor | RX 512, TX 512 (P4: TX 4096, 4096-byte transfers) | 512, 512, `transferBytes` 512 |
| MIDI | 512 bytes each way | `rxPackets` 128, `txPackets` 128 (512 bytes) |
| MSC | 4096-byte staging | `transferBytes` 512 |
| DFU | 1024-byte blocks | `transferBytes` 1024 |
| CCID | 320-byte messages | `messageBytes` 0 = `max(271, Bulk-OUT MPS)`: 271 at FS, 512 at HS |
| Video | payload 512 (P4: 1023) | `payloadBytes` 512 |
| NCM | 3 TX / 2 RX NTBs of 3200 bytes | same: `txBuffers` 3, `rxBuffers` 2, `ntbInBytes`/`ntbOutBytes` 3200 |

Larger rings cost heap and give more slack. The MSC default is smaller than
v2's; set `transferBytes = 4096` to match v2 throughput.

### HID

```cpp
// v2 (build_opt.h: -DCFG_TUD_HID_EP_BUFSIZE=512)
EspUsbDeviceHidVendor hid(device, 256);
hid.onOutputReport(handleOutput);
```

```cpp
// v3: no flag; up to 65534 bytes per report
EspUsbDeviceHidVendor hid(device, 256);
if (hid.onOutputReport(handleOutput) != ESP_OK) { /* before device.begin() */ }
```

- `CFG_TUD_HID*` is gone. Report storage comes from the report descriptor in
  `begin()`.
- `onOutputReport()`, `onFeatureReport()`, `onProtocol()`, `enableNkro()`
  return `esp_err_t` and work only while stopped.
- `device.hidEndpointSize()` → each Function's `inputPacketSize()` (the
  negotiated packet size; 0 before bus reset).
- New: `inputReportCapacity()`, `outputReportCapacity()`,
  `featureReportCapacity()`, `setFeatureReport()`, `compositeReportId()`,
  `keyboard.releaseKey(char)`.
- `EspUsbDeviceHidCustom`'s `inputReportSize` sets the interrupt IN packet
  size (up to 64 at FS, 1024 at HS). Longer reports are sent in several
  packets.
- A composite HID interface now uses the largest packet size its Functions
  ask for (NKRO 32, Custom/Vendor their report length). v2 fixed it at 16.

Behaviour changes (no compile error):

| | v2 | v3 |
| --- | --- | --- |
| Keyboard `pressUsage()` / `pressKey()` without NKRO | Replaced the held key and modifiers | Adds to the held keys, up to 6. A 7th fails with `ESP_ERR_INVALID_SIZE` and changes nothing. Modifiers are OR-ed |
| Modifier usages 0xE0–0xE7 | Placed in the key array | Set the modifier bit |
| `tapUsage()` / `tapKey()` | Ended with `releaseAll()` | Releases only what the tap added |
| `pressUsage(u, m, x)` third argument | `holdMs`, ignored | `timeoutMs` (default 100) |
| `pressKey()` with NKRO enabled | Sent a boot report | Uses the NKRO report |
| Character the layout cannot type | `false` | `false`, `lastError` = `ESP_ERR_NOT_SUPPORTED` |
| `mouse.move(x, y)` | Released held buttons (`buttons` defaulted to 0) | Keeps held buttons (`KeepButtons`). Pass 0–255 to set them |
| System Control values | `POWER_OFF` = 1, `STANDBY` = 2, `WAKE_HOST` = 3 | 0x81, 0x82, 0x83 (Generic Desktop usage IDs). Integers 1/2/3 are rejected; `usage()` returns 0x81–0x83 |
| Consumer Control usage > 0x3FF | Sent | Rejected with `ESP_ERR_INVALID_ARG` |
| Gamepad hat | Logical 0–8: center (0) read as a direction | Logical 1–8 with a null state: `HAT_CENTER` (0) means no direction. Constants and report length unchanged |
| HID Vendor `sendInput()` longer than `reportSize()` | Cut to `reportSize()` | Returns `false` |

The System Control and hat changes follow HID Usage Tables (Generic Desktop
0x81–0x83, Hat Switch with a null state). Host software that compensated for
the old hat encoding must be checked again.

```cpp
// v2 and v3: constants keep working
system.click(ESP_USB_DEVICE_SYSTEM_CONTROL_STANDBY);
// v2 only: integers 1..3
system.click(2);   // v3: rejected, use the constant
```

See [HID](classes/hid.md).

### CDC

```cpp
// v2 (build_opt.h: -DCFG_TUD_CDC_RX_BUFSIZE=4096)
EspUsbDeviceCdcSerial cdc(device);
void setup() {
  device.begin();
  cdc.onLineState(handleLineState);   // after begin: worked in v2
}
```

```cpp
// v3
EspUsbDeviceCdcSerial cdc(device);
void setup() {
  EspUsbBulkStreamConfig c = cdc.configuration();
  c.rxCapacity = 4096;
  if (cdc.configure(c) != ESP_OK || cdc.onLineState(handleLineState) != ESP_OK) return;
  device.begin();
}
```

- `CFG_TUD_CDC_*` → `configure(EspUsbBulkStreamConfig)`: `rxCapacity`,
  `txCapacity`, `transferBytes`, `rxMode`, optional external `rxBuffer` /
  `txBuffer`.
- CDC is an Arduino `Stream`: `peek()`, `readBytes()`, `setTimeout()`, and it
  can be passed as `Stream&`.
- `onLineCoding()`, `onLineState()`, `onRx()`, `setName()` return `esp_err_t`.
- `lineCoding()` / `lineState()` return values, not references.
- `port()` is the index in CDC descriptor order. In v2 it was also the TinyUSB
  instance; that meaning is gone.
- `EspUsbDevice::maxCdcPorts()` is gone. `begin()` checks the whole
  configuration; read `lastError()` on failure and `readCapacity()` /
  `writeCapacity()` after success.
- New: `onBreak()`, `setTxTimeoutMs()`, `peek()`, borrowed transfers.

Behaviour change: **`write()` and `flush()` wait.** When the TX ring is full,
`write()` / `print()` wait for room up to the TX timeout (default 1000 ms).
`flush()` waits until the data and any needed ZLP have been sent. v2 returned
at once with a partial count, and `flush()` only started the send. If a stalled
terminal must not block `loop()`, set a shorter limit:

```cpp
cdc.setTxTimeoutMs(100);   // 0 = v2-like: return at once
```

Calls from the USB worker never wait. See [CDC](classes/cdc.md).

### Vendor

```cpp
// v2
vendor.onRx([](size_t n) {
  uint8_t buf[64];
  size_t got = vendor.read(buf, min(n, sizeof(buf)));
  vendor.write(buf, got);
  vendor.flush();                       // needed for short replies
});
```

```cpp
// v3
volatile bool rxPending = false;
// setup(): before device.begin()
vendor.onRx([](size_t) { rxPending = true; });

void loop() {
  uint8_t buf[64];
  size_t got = vendor.read(buf, sizeof(buf));
  if (got && vendor.waitWritable(got)) vendor.write(buf, got);   // autoFlush sends it
}
```

- `CFG_TUD_VENDOR_*` → `configure(EspUsbVendorConfig)` (the same struct as
  CDC).
- `autoFlush` is on by default, so `flush()` after each reply is no longer
  needed. `flush()` starts sending and does not wait.
- `onRx()` is a notification. Read in `loop()`, or use
  `rxMode = EspUsbBulkRxMode::Callback` with `onRxData()` to get each packet
  on the worker.
- `writeCapacity()` is per instance (was static). `waitWritable()` and
  `writeAvailable()` remain.
- The constructor's `endpointSize` is the FS bulk packet size. It must be 8,
  16, 32 or 64 (larger values select 64); other values fail `begin()` with
  `ESP_ERR_INVALID_SIZE` (USB 2.0 §5.8.3). HS always uses 512.

Borrowed transfers:

| v2 | v3 |
| --- | --- |
| Needed `-DCFG_TUD_VENDOR_TXRX_BUFFERED=0` | Always available |
| `bool writeDirect(buf, len)` | `EspUsbTransferSubmission writeDirect(buf, len, termination)`. `if (!vendor.writeDirect(...))` still compiles; `bool ok = vendor.writeDirect(...)` does not (explicit conversion) |
| `lastDirectError()`, `EspUsbDeviceVendorDirectError` | `submission.error` (`EspUsbTransferError`) |
| Completion through `onTxComplete(size_t)` | `onDirectTxComplete(const EspUsbTransferResult &)`, required before `begin()`. It returns the buffer on every outcome: success, cancel, bus reset, stop |
| 65535-byte limit | No fixed limit (`maxDirectTransfer()` is the representable maximum, not free memory) |
| - | `readDirect()`, `cancelDirectTx()` / `cancelDirectRx()` |

Buffers must be 64-byte aligned and in DMA-capable internal RAM. PSRAM is
refused with `NotDmaCapable`.

Control requests:

- The library now takes only the exact WebUSB GET_URL request (bRequest 0x01,
  bmRequestType 0xC0, wIndex 2, when WebUSB is enabled) and the Microsoft OS
  2.0 descriptor request (bRequest 0x02, 0xC0, wIndex 7). Every other request
  reaches `onControlRequest()`. In v2, any bRequest 0x01 was taken while
  WebUSB was enabled.
- An OUT request with a data stage needs a receive buffer. Answer the SETUP
  stage with the buffer, check `actualBytes` at DATA, and apply at ACK:

```cpp
// v2: status only; the data stage was never received
return vendor.sendControlResponse(r);
```

```cpp
// v3
uint8_t cmd[64];   // must stay valid until the transfer ends
switch (r.stage) {
case ESP_USB_DEVICE_CONTROL_STAGE_SETUP: return vendor.sendControlResponse(r, cmd, r.wLength);
case ESP_USB_DEVICE_CONTROL_STAGE_DATA:  return r.actualBytes == r.wLength;
default:                                 apply(cmd, r.actualBytes); return true;
}
```

With `wLength` 0, `sendControlResponse(r)` sends only the status stage, as
before.

Behaviour change: **no automatic ZLP.** v2 sent a zero-length packet after a
transfer whose length was a multiple of the packet size. v3 Vendor streams do
not (`streamTermination` defaults to `Continue`). A host that waits for a short
packet to end a read now waits for more data. Frame your messages with a
length, or set `streamTermination = EspUsbStreamTermination::EndWithShortPacket`.
See [Vendor](classes/vendor.md).

### MIDI

```cpp
// v2 (build_opt.h: -DCFG_TUD_MIDI_RX_BUFSIZE=1024)
EspUsbDeviceMidi midi(device, 2);
```

```cpp
// v3
EspUsbDeviceMidi midi(device, 2);
void setup() {
  EspUsbMidiConfig c = midi.configuration();   // keeps the 2 cables
  c.rxPackets = 256;
  c.name = "My MIDI";
  if (midi.configure(c) != ESP_OK) return;
  device.begin();
}
```

- `CFG_TUD_MIDI_*` → `configure(EspUsbMidiConfig)`: `inCables`,
  `outCables`, `rxPackets`, `txPackets`, `transferPackets`, `rxMode`, `name`.
- `EspUsbMidiConfig` carries the cable counts. Its default 0 keeps the current
  count, so a fresh config keeps the cables given to the constructor.
- Cable counts must be 1–16. v2 clamped 17+; v3 rejects them.
- MIDI is not `Print`. Use `readPacket()` / `writePacket()` and the message
  helpers, as in v2.
- New: `onPacket()` (with `rxMode = Callback`), `receiveErrors()`,
  `receiveCapacity()`, `sendCapacity()`, `startError()`.

Behaviour change: channel > 15, a data byte > 127 or pitch bend > 16383 is
rejected (`false`, `ESP_ERR_INVALID_ARG`) and nothing is sent. v2 clamped the
value and sent it. Incoming packets on unknown cables or with reserved CINs are
dropped and counted in `receiveErrors()`. See [MIDI](classes/midi.md).

### MSC and the disk helpers

```cpp
// v2 (build_opt.h: -DCFG_TUD_MSC_EP_BUFSIZE=4096)
msc.mediaPresent(true);
disk.attach(msc);
device.begin();
```

```cpp
// v3
EspUsbMscConfig c;
c.transferBytes = 4096;          // multiple of 512: valid at FS and HS
c.name = "RAM disk";
if (msc.configure(c) != ESP_OK) return;
if (!disk.attach(msc)) return;
msc.mediaPresent(true);
device.begin();
```

- `CFG_TUD_MSC_EP_BUFSIZE` → `EspUsbMscConfig::transferBytes` (default 512;
  must be a multiple of 64, and of the bulk packet size: 512 at HS).
- `onRead()`, `onWrite()`, `onStartStop()`, `vendorID()`, `productID()`,
  `productRevision()` return `esp_err_t` and work only while stopped.
- `msc.begin(blockCount, blockSize)` still sets the geometry. Register
  `onRead()` first; without it, it returns `false`.
- `RamDisk::attach()` and `FatRamDisk::attach()` still do not change media
  presence or write access. Call `msc.mediaPresent(true)` yourself.
  `FirmwareDisk::attach()` still marks the medium present.
- `SdCard::attach()` now fails while the device runs.

Behaviour changes:

| | v2 | v3 |
| --- | --- | --- |
| Read/write callback returns less than requested, or 0 to retry later | TinyUSB retried or continued | Treated as a medium error (sense 03/11/00). Return the full length synchronously |
| `mediaPresent(false)` → `true` while configured | Next command succeeded | The next command (except INQUIRY and REQUEST SENSE) fails once with UNIT ATTENTION 06/28/00, as SPC requires |
| `mediaPresent(false)` during a READ/WRITE | - | The rest of the command fails with 02/3A/00 |
| `SdCard::readOnly(true)` | Writes failed as Medium Error | Reported as write protection (MODE SENSE WP); writes fail with 07/27/00 |
| `FatRamDisk` larger than about 2 MiB | Invalid FAT12 (too many clusters) | Cluster size grows with the volume; always valid FAT12 |
| Default staging | 4096 bytes | 512 bytes |

See [MSC](classes/msc.md).

### Firmware update and DFU

`EspUsbDeviceFirmwareUpdate`, `EspUsbDeviceMscFirmwareDisk` and
`EspUsbDeviceMscSdCard` keep their API.

```cpp
// v2 (build_opt.h: -DCFG_TUD_DFU_XFER_BUFSIZE=4096)
dfu.onError([](uint8_t status) { Serial.println(status); });
runtime.onDetach([]() { startMyUpdate(); });   // replaced the default restart
```

```cpp
// v3
EspUsbDfuConfig c = dfu.configuration();
c.transferBytes = 4096;
dfu.configure(c);
dfu.onError([](EspUsbDfuStatus status) { lastStatus = unsigned(status); });
runtime.setDetachHandler([]() { startMyUpdate(); });
runtime.onDetach([]() { return !busy; });        // optional: false stalls DETACH
```

- `CFG_TUD_DFU_XFER_BUFSIZE` → `EspUsbDfuConfig::transferBytes`.
- `transferSize()` is per instance (was static).
- `onError()` takes `EspUsbDfuStatus`. `unsigned(status)` gives the v2 number.
- Runtime mode: v2's `onDetach(void())` replaced the default restart into the
  ROM loader. In v3 that is `setDetachHandler()`. `onDetach()` now returns
  `bool` at SETUP to accept or refuse DETACH.
- `onProgress()`, `onComplete()`, `onError()`, `restartWhenComplete()` return
  `esp_err_t` and work only while stopped.
- New: `setBackend()` to write somewhere other than the built-in OTA.

See [DFU](classes/dfu.md) and [OTA over USB](ota-over-usb.md).

### Audio

```cpp
// v2 (build flags set the FIFO sizes)
playback.addFormat({48000, 2, 2, 16});
device.begin(config);
```

```cpp
// v3
if (!playback.addFormat({48000, 2, 2, 16})) return;
EspUsbAudioConfig c;
c.playbackBufferFrames = 480;    // 10 ms at 48 kHz
c.eventCapacity = 16;
if (audio.configure(c) != ESP_OK) return;
device.begin(config);
```

- Audio build flags → `EspUsbAudioConfig`: `eventCapacity`,
  `playbackBufferFrames`, `captureBufferFrames`, optional `playbackBuffer` /
  `captureBuffer` with their byte lengths, `playbackOverflowPolicy`.
- An inconsistent setup (a stream with no format, different rate sets between
  shapes, a channel count that changes within one direction) fails `begin()`
  with `ESP_ERR_INVALID_ARG`. Too little buffer fails with
  `ESP_ERR_INVALID_SIZE`.
- New: `clearFormats()`, `currentFormat()`, `capacity()`,
  `packetCapacity()`, `capture.availableForWrite()`.

Behaviour changes:

| | v2 | v3 |
| --- | --- | --- |
| `capture.write()` when full | Accepted everything and overwrote the oldest PCM | Partial write: returns what fit and never overwrites. Accepts whole frames only |
| Playback ring full | - | Default `DropOldest` (keeps the newest frames). Set `DropNewest` to keep unread PCM instead |
| UAC2 FS feedback endpoint | 4 bytes, 16.16 | 3 bytes, 10.14 (USB 2.0 §5.12.4.2). HS stays 4 bytes, 16.16 |

Capture code should write only what `availableForWrite()` allows, or keep the
remainder. See [Audio](classes/audio.md).

### Video

```cpp
// v2 (build_opt.h: -DCFG_TUD_VIDEO_STREAMING_BULK=1)
camera.onFrameComplete([]() { frameDone = true; });
Serial.println(EspUsbDeviceVideo::payloadBufferSize());
```

```cpp
// v3
EspUsbVideoConfig c;
c.transport = EspUsbVideoTransport::Bulk;
c.payloadBytes = 1024;
camera.configure(c);
camera.onFrameResult([](const EspUsbTransferResult &r) { frameDone = true; });
device.begin(config);
Serial.println(camera.payloadBufferSize());   // per instance, 0 before begin()
```

- `CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE` / `_BULK` → `EspUsbVideoConfig`:
  `transport`, `payloadBytes`, `isochronousPacketBytes`, `payloadBuffer`.
- `payloadBufferSize()`, `isochronousPacketSize()`, `bulkStreaming()` are
  per instance (were static).
- `sendFrame()` and `onFrameComplete()` remain. `submitFrame()` returns
  `EspUsbTransferSubmission` with the reason for a refusal, and
  `onFrameResult()` returns every frame, including cancelled and interrupted
  ones. `cancelFrame()` is new.
- `onFrameComplete()`, `onCommit()` return `esp_err_t`.

See [Video](classes/video.md).

### CCID

```cpp
// v2 (build_opt.h: -DESP_USB_DEVICE_CCID_BUFFER_SIZE=600)
reader.onApdu(handleApdu);
```

```cpp
// v3
EspUsbCcidConfig c;
c.messageBytes = 600;            // 0 = automatic
reader.configure(c);
if (reader.onApdu(handleApdu) != ESP_OK) return;
```

- `ESP_USB_DEVICE_CCID_BUFFER_SIZE` → `EspUsbCcidConfig::messageBytes`. The
  default 0 picks `max(271, Bulk-OUT MPS)`: 271 at FS, 512 at HS. CCID 1.1
  requires at least the Bulk-OUT packet size; v2's 320 could not start on the
  P4 HS port. At FS the default is now 271 (one short APDU); set a larger value
  if your answers are longer. `messageCapacity()` reports the result.
- `removable = false` omits the interrupt endpoint. Insert the card before
  `begin()` in that case.
- `onApdu()`, `onEscape()`, `onPower()`, `removeCard()` return `esp_err_t`.

See [CCID](classes/ccid.md).

### NCM (USB network)

- The fixed NTB pool → `EspUsbNetConfig`: `ntbInBytes`, `ntbOutBytes`,
  `txBuffers`, `rxBuffers`, `maxDatagrams`, `maxFrameBytes`, optional buffers,
  `name`.
- `macAddress()`, `onFrame()`, `ipConfig()`, `dhcpServer()`, `dhcpClient()`,
  `dhcpAdvertiseGateway()`, `dhcpDns()`, `defaultRoute()` return `esp_err_t`.
  The MAC and settings cannot change while running.
- New: `endNetwork()`, `stats()`, capacity queries.

```cpp
// v2
net.dhcpServer(true);
```

```cpp
// v3
if (net.dhcpServer(true) != ESP_OK) { /* report */ }
```

Behaviour change: the default MAC is derived from the chip's Ethernet MAC with
the locally administered bit set and the multicast bit cleared. v2 used the
chip's Ethernet MAC unchanged, so the default value differs. Host settings tied
to the MAC (a static address, a firewall rule) may need to be redone; set
`macAddress()` explicitly to keep a fixed value. See [NCM](classes/ncm.md).

## 5. Removed and renamed APIs

| v2 | v3 | Notes |
| --- | --- | --- |
| `build_opt.h`, `CFG_TUD_*` | each Function's `configure()` | Listed flags are a compile error |
| `ESP_USB_DEVICE_CCID_BUFFER_SIZE` | `EspUsbCcidConfig::messageBytes` | Compile error |
| `#include <tusb.h>`, `tud_*()` | Function and Device APIs | e.g. `tud_speed_get()` → `device.speed()` |
| `device.task()` | (remove) | The USB worker runs everything |
| `config.startTinyUsb` | `config.startMode` | `Usb` / `DescriptorsOnly` |
| `void end()` | `esp_err_t end()` | |
| `EspUsbDevice::maxCdcPorts()` | (removed) | `begin()` checks the configuration |
| `device.hidEndpointSize()` | `hid.inputPacketSize()` | Per Function |
| Function `begin()` / `afterDeviceStarted()` | (protected) | Use `device.begin()` / `end()`. `msc.begin(count, size)` stays |
| `void on...()` setters | `esp_err_t on...()` | Only while stopped |
| `void onAnyControlRequest()` | `esp_err_t onAnyControlRequest()` | Only while stopped |
| `EspUsbDeviceClass::isHid()` default `true` | default `false` | Custom classes only |
| `keyboard.pressUsage(u, m, holdMs)` | `pressUsage(u, m, timeoutMs)` | |
| `mouse.move(..., uint8_t buttons = 0)` | `move(..., int16_t buttons = KeepButtons)` | |
| `ESP_USB_DEVICE_SYSTEM_CONTROL_*` = 1/2/3 | = 0x81/0x82/0x83 | Same names |
| `cdc.lineCoding()` returning `const &` | returns a value | Also `lineState()` |
| `cdc.setName()` (`void`) | `esp_err_t setName()` | |
| `EspUsbDeviceVendor::writeCapacity()` (static) | per instance | |
| `vendor.writeDirect()` → `bool` | → `EspUsbTransferSubmission` | |
| `vendor.lastDirectError()`, `lastDirectErrorName()` | `submission.error` | |
| `EspUsbDeviceVendorDirectError` | `EspUsbTransferError` | |
| `-DCFG_TUD_VENDOR_TXRX_BUFFERED=0` | (not needed) | Compile error if left; delete it |
| `msc.vendorID()` etc. (`void`) | `esp_err_t` | |
| `EspUsbDeviceDfu::transferSize()` (static) | per instance | |
| `dfu.onDetach(void())` | `setDetachHandler(void())` | `onDetach(bool())` decides acceptance |
| `dfu.onError(void(uint8_t))` | `onError(void(EspUsbDfuStatus))` | |
| `EspUsbDeviceVideo::payloadBufferSize()` (static) | per instance | Also `isochronousPacketSize()`, `bulkStreaming()` |
| `camera.onFrameComplete()` only | also `onFrameResult()`, `submitFrame()`, `cancelFrame()` | |
| `reader.removeCard()` (`void`) | `esp_err_t` | |
| `net.ipConfig()` etc. (`void`) | `esp_err_t` | |
| - | `device.started()`, `suspended()`, `speed()`, `remoteWakeup()` | New |
| - | `EspUsbDeviceConfig::taskStackBytes`, `taskPriority`, `eventQueueDepth`, `vbusSensing`, `stopTimeoutMs`, `remoteWakeupSupported` | New |

## 6. Example mapping

Every v2 example has a v3 example with the same name and path: 35 examples plus
the 3 sketches under `Info`, 38 sketches in all. Each v3 example README has a
"Changes from v2" section that lists what changed in that sketch.

| v2 example | v3 example | Main migration points |
| --- | --- | --- |
| Keyboard | [Keyboard](../examples/Keyboard/README.md) | `onOutputReport()` returns `esp_err_t`; keys combine |
| KeyboardMouse | [KeyboardMouse](../examples/KeyboardMouse/README.md) | `compositeReportId()`; composite packet size |
| KeyboardNKRO | [KeyboardNKRO](../examples/KeyboardNKRO/README.md) | `enableNkro()` / `onProtocol()` return `esp_err_t` |
| Mouse | [Mouse](../examples/Mouse/README.md) | `move()` keeps held buttons |
| Gamepad | [Gamepad](../examples/Gamepad/README.md) | Hat null state |
| MediaKeys | [MediaKeys](../examples/MediaKeys/README.md) | System Control usage IDs |
| CustomHID | [CustomHID](../examples/CustomHID/README.md) | `inputReportSize` is the packet size |
| VendorHID | [VendorHID](../examples/VendorHID/README.md) | No `CFG_TUD_HID_EP_BUFSIZE`; `sendInput()` does not cut |
| Serial | [Serial](../examples/Serial/README.md) | `configure()`, `onBreak()`, TX timeout |
| SerialMulti | [SerialMulti](../examples/SerialMulti/README.md) | No `maxCdcPorts()`; per-port sizes |
| USBVendor | [USBVendor](../examples/USBVendor/README.md) | Request numbers, OUT data stage, `https://` |
| MIDI | [MIDI](../examples/MIDI/README.md) | `configure()`, `receiveErrors()` |
| MIDIController | [MIDIController](../examples/MIDIController/README.md) | Retries refused sends; callback receive |
| MIDIInterface | [MIDIInterface](../examples/MIDIInterface/README.md) | Keeps and resends refused messages |
| MSC | [MSC](../examples/MSC/README.md) | `transferBytes`; UNIT ATTENTION on re-insert |
| MSCFatRamDisk | [MSCFatRamDisk](../examples/MSCFatRamDisk/README.md) | `configure()`; eject handled in `loop()` |
| MSCSdCard | [MSCSdCard](../examples/MSCSdCard/README.md) | 4 KiB staging; write protection |
| CompositeHidCdcMsc | [CompositeHidCdcMsc](../examples/CompositeHidCdcMsc/README.md) | Per-Function `configure()`; CDC waits |
| AudioSpeaker | [AudioSpeaker](../examples/AudioSpeaker/README.md) | `EspUsbAudioConfig`; `DropOldest` |
| AudioMicrophone | [AudioMicrophone](../examples/AudioMicrophone/README.md) | Partial `write()` |
| AudioHeadset | [AudioHeadset](../examples/AudioHeadset/README.md) | Partial `write()` |
| AudioSpeakerM5 | [AudioSpeakerM5](../examples/AudioSpeakerM5/README.md) | Same, for M5Stack |
| AudioMicrophoneM5 | [AudioMicrophoneM5](../examples/AudioMicrophoneM5/README.md) | Same, for M5Stack |
| AudioHeadsetM5 | [AudioHeadsetM5](../examples/AudioHeadsetM5/README.md) | Same, for M5Stack |
| VideoCamera | [VideoCamera](../examples/VideoCamera/README.md) | `EspUsbVideoConfig`; `onFrameResult()` |
| SmartCardReader | [SmartCardReader](../examples/SmartCardReader/README.md) | `messageBytes`; setters return `esp_err_t` |
| UsbNetwork | [UsbNetwork](../examples/UsbNetwork/README.md) | `EspUsbNetConfig`; setters return `esp_err_t` |
| FirmwareDFU | [FirmwareDFU](../examples/FirmwareDFU/README.md) | `transferBytes`; `EspUsbDfuStatus` |
| FirmwareBootMode | [FirmwareBootMode](../examples/FirmwareBootMode/README.md) | `setDetachHandler()` / `onDetach(bool)` |
| FirmwareCDC | [FirmwareCDC](../examples/FirmwareCDC/README.md) | `flush()` waits for the reply |
| FirmwareMSC | [FirmwareMSC](../examples/FirmwareMSC/README.md) | `transferBytes`; UF2 |
| FirmwareVendor | [FirmwareVendor](../examples/FirmwareVendor/README.md) | `configure()`; status bytes |
| FirmwareHTTP | [FirmwareHTTP](../examples/FirmwareHTTP/README.md) | `EspUsbNetConfig`; setters return `esp_err_t` |
| P4FullSpeedDevice | [P4FullSpeedDevice](../examples/P4FullSpeedDevice/README.md) | `controller`, `vbusSensing`, `ChipVariant` |
| P4HighSpeedDevice | [P4HighSpeedDevice](../examples/P4HighSpeedDevice/README.md) | HS size rules |
| Info/EspUsbDeviceBringUpCheck | [Info/EspUsbDeviceBringUpCheck](../examples/Info/EspUsbDeviceBringUpCheck/README.md) | |
| Info/EspUsbDeviceConsole | [Info/EspUsbDeviceConsole](../examples/Info/EspUsbDeviceConsole/README.md) | |
| Info/EspUsbDeviceDescriptorDump | [Info/EspUsbDeviceDescriptorDump](../examples/Info/EspUsbDeviceDescriptorDump/README.md) | |

Examples that exist only in v3:

| Example | Shows |
| --- | --- |
| [CdcConsole](../examples/CdcConsole/CdcConsole.ino) | CDC passed as `Stream&` |
| [MidiEcho](../examples/MidiEcho/MidiEcho.ino) | MIDI packet echo |
| [VendorEcho](../examples/VendorEcho/VendorEcho.ino) | Vendor echo that keeps partial writes |
| [AudioPlayback](../examples/AudioPlayback/AudioPlayback.ino) | UAC2 playback with several formats |
| [MscCdcBlocks](../examples/MscCdcBlocks/MscCdcBlocks.ino) | Raw MSC blocks plus CDC |
| [CompositeCdcMidi](../examples/CompositeCdcMidi/CompositeCdcMidi.ino) | CDC and MIDI with separate sizes |
| [ReconfigureVendor](../examples/ReconfigureVendor/ReconfigureVendor.ino) | `end()` → `configure()` → `begin()` at run time |

## 7. Not yet verified

The library is tested on hardware with: two ESP32-S3 boards as an FS
host/device pair, one ESP32-P4 as an HS↔FS loopback, an older P4 and a P4X
connected at HS in both directions, and a P4X with a Linux PC at HS.

Not verified:

- ESP32-S2 on hardware (compile only).
- Windows and macOS hosts.
- MSC on a real SD card, and OTA activation and restart on hardware.
- Most examples on hardware. Each example README states what was checked.

Check these on your own hardware before relying on them.
