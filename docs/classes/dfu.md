# DFU (Device Firmware Upgrade)

> 日本語版: [dfu.ja.md](dfu.ja.md)

`EspUsbDeviceDfu` is the Function that lets firmware be rewritten with the USB DFU class.
Every DFU transfer travels on the control endpoint (EP0). **It uses one interface and no endpoints at all**,
so it can be added to a configuration whose endpoints are already used up.

## Overview and when to use it

There are two modes.

| Mode | Host side | Default behaviour | Use it for |
| --- | --- | --- | --- |
| `Download` (default) | `dfu-util -D firmware.bin` | Writes the image to the spare OTA partition, verifies it, switches the boot partition and restarts | Updating the application over USB while the sketch keeps running |
| `Runtime` | `dfu-util -e` | On DETACH, restarts into the chip's ROM download loader | Entering boot mode without pressing a button, so `esptool` can rewrite the whole flash |

- By default Download stores into the built-in OTA updater (`EspUsbDeviceFirmwareUpdate`). `setBackend()` replaces it with the application's own storage, such as external flash or another processor.
- See [firmware update over USB](../ota-over-usb.md) for a comparison with the other routes (MSC, HTTP, CDC, Vendor).

## Basic usage

### Download (built-in OTA)

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceDfu dfu(device);     // default: Download with the built-in OTA

void setup() {
  EspUsbDfuConfig config;
  config.transferBytes = 4096;   // largest block in bytes (default 1024)
  if (dfu.configure(config) != ESP_OK) return;
  dfu.onProgress([](size_t written) { /* bytes stored so far */ });
  dfu.onError([](EspUsbDfuStatus status) { /* record why it failed */ });
  if (!device.begin()) { /* check device.lastError() */ }
  // after a successful download it restarts into the new firmware about 500 ms later
}

void loop() {}
```

- The built-in OTA needs a partition table with two application partitions (Arduino's "Default", for example).
- The image is a plain Arduino `.bin` (not the `dfu.bin` from `idf.py dfu`, and not `*.merged.bin`).

### Runtime (move to the ROM loader)

```cpp
EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Runtime, "Bootloader");
// by default it restarts into the ROM download loader about 100 ms after the DETACH status stage
```

To change settings: `device.end()`, `configure()`, then `device.begin()`. No rebuild is needed.

## Configuration

### Constructor

| Argument | Default | Meaning |
| --- | --- | --- |
| `device` | — | The `EspUsbDevice` to register with |
| `mode` | `EspUsbDeviceDfuMode::Download` | `Download` or `Runtime` |
| `name` | `nullptr` | Interface name (string descriptor) |

### `EspUsbDfuConfig`

`configure()` is accepted only while stopped.

| Field | Default | Range and meaning | FS/HS difference |
| --- | --- | --- | --- |
| `transferBytes` | 1024 | Largest block (one DFU_DNLOAD) in bytes. 64 to 65535. The descriptor's `wTransferSize` | none |
| `transferBuffer` | `nullptr` | Application-supplied Download block buffer. Must be at least `transferBytes` long | none |
| `detachTimeoutMs` | 1000 | Runtime: longest time appDETACH is kept after DETACH (ms). The descriptor's `wDetachTimeOut` | none |
| `downloadPollMs` | 1 | Wait returned by GETSTATUS while a block is stored (bwPollTimeout, ms). 0 to 0xFFFFFF | none |
| `manifestPollMs` | 250 | Wait returned by GETSTATUS during manifestation (verify and activate) (ms). 0 to 0xFFFFFF | none |
| `manifestationTolerant` | `false` | `true`: after manifestation, another transfer can start without a USB reset | none |

### FS and HS

DFU uses EP0 only. EP0's MPS is 64 bytes at both full and high speed.
The `transferBytes` range, poll times and DETACH handling do not depend on speed.

## API

### Configuring and starting

| Function | Returns and constraints |
| --- | --- |
| `esp_err_t configure(config)` | Running: `ESP_ERR_INVALID_STATE`. `transferBytes` or a poll time out of range: `ESP_ERR_INVALID_SIZE`. `transferBuffer` address arithmetic overflows: `ESP_ERR_INVALID_ARG` |
| `configuration()` | The settings as given |
| `mode()` | `Runtime` or `Download` |
| `transferSize()` | The `transferBytes` declared in the descriptor |
| `transferCapacity()` | Allocated Download buffer bytes. 0 before begin, after end, and in Runtime |
| `written()` | Bytes stored by the current transfer |
| `startError()` | Why the last begin failed |

`device.begin()` failures: allocation failure gives `ESP_ERR_NO_MEM`; not registered with a Device, a second begin and similar give `ESP_ERR_INVALID_STATE`. Anything already allocated is released.

### Progress and completion (Download)

| Function | Description |
| --- | --- |
| `onProgress(callback)` | Called with the running total after each stored block. DFU never tells the device the image length, so there is no total |
| `onError(callback)` | Called with an `EspUsbDfuStatus` on each failure |
| `onComplete(callback)` | Built-in OTA only. Called after the image verified at manifestation. Returning `false` rejects the image and restores the running boot partition |
| `restartWhenComplete(enable)` | Built-in OTA only. Default `true`: restart about 500 ms after a successful manifestation. With `false` the application restarts |
| `firmware()` | The `EspUsbDeviceFirmwareUpdate` the built-in OTA uses |

- These setters return `esp_err_t` and `ESP_ERR_INVALID_STATE` while running.
- `manifestationTolerant = false` (default): after a successful manifestation the device waits in dfuMANIFEST-WAIT-RESET for a USB reset.
- `manifestationTolerant = true`: the GETSTATUS after manifestation returns to dfuIDLE and another transfer can start.
- A file that is not an ESP application fails on its first block with `Write`. A malformed image fails the final verification with `Verify`. Neither changes the boot partition.

### Your own storage (backend)

Supplying both `download` and `manifest` in `EspUsbDfuBackend` replaces the built-in OTA with the application's storage.

```cpp
EspUsbDfuBackend backend;
backend.download = [](uint16_t block, const uint8_t *data, size_t bytes) {
  // copy or process before returning; do not keep data
  return storeBlock(block, data, bytes) ? EspUsbDfuStatus::Ok : EspUsbDfuStatus::Write;
};
backend.manifest = []() {
  return verifyAndActivate() ? EspUsbDfuStatus::Ok : EspUsbDfuStatus::Verify;
};
backend.abort = []() { discardPartialUpdate(); };   // optional
dfu.setBackend(backend);
```

| Item | Behaviour |
| --- | --- |
| `setBackend()` result | Running: `ESP_ERR_INVALID_STATE`. Only one of `download` and `manifest`, `abort` without `download`, or `download` in Runtime: `ESP_ERR_INVALID_ARG` |
| An empty backend | Restores the built-in OTA |
| `download`'s `data` | Valid only inside the callback |
| A partial store | Do not return `Ok`; return a failure status. Out-of-range values become `Unknown` |
| `abort` | Called once when a transfer that started storing ends by failure, ABORT, reset or `end()`. Not called for a block cancelled before storing, or after a successful manifestation |
| After manifestation | Verification, activation and restart belong to the backend. `onComplete` and `restartWhenComplete` do not apply |

### DETACH (Runtime)

Whether to accept a host DFU_DETACH, and what to do after accepting, are decided separately.

| Function | When | Description |
| --- | --- | --- |
| `onDetach(bool callback())` | DETACH SETUP | Returning `false` stalls the request and changes neither state nor schedule. Empty accepts unconditionally |
| `setDetachHandler(void handler())` | Once, after the accepted DETACH's status stage | Runs instead of the default ROM restart, and then owns leaving the bus. Empty restores the default ROM restart |

| Stage | Behaviour |
| --- | --- |
| SETUP | Valid only as type 0x21, DETACH, wLength 0, in appIDLE. Calls `onDetach` once |
| Status-stage ACK | Moves to appDETACH and calls the handler once if set; otherwise schedules a restart into the ROM loader 100 ms later |
| After acceptance | Not withdrawn by `end()`, a USB reset or SET_CONFIGURATION 0 |
| Timeout | appDETACH lasts `min(wValue, detachTimeoutMs)`, then returns to appIDLE. The default ROM restart happens regardless of wValue |

- Registration works only in Runtime while stopped. While running it returns `ESP_ERR_INVALID_STATE`; in Download, `ESP_ERR_INVALID_ARG`.
- To refuse, return `false` from `onDetach`. An accepted DETACH cannot be withdrawn.
- Which USB port the ROM loader answers on differs per chip ([firmware update over USB](../ota-over-usb.md)).

### Several DFU interfaces

Several DFU interfaces can coexist. They share no state, buffers or backend.
So that two of them never write the same storage at once, only one may use the built-in OTA during a transfer (a second one fails with `Target`).

### Calling context

| Operation | Context |
| --- | --- |
| `configure`, `setBackend`, callback registration | An application task, while stopped |
| `onProgress`, `onError`, `onComplete`, the backend, `onDetach`, the DETACH handler | Called synchronously on the USB worker task |

- Inside a callback, do not wait, call `end()` or do long work. Hand heavy work to an application task.
- Restart by setting a flag and acting in `loop()`, not inside a callback. Restarting inside one cuts off the control transfer the host is still finishing.
- The backend's `download` is called synchronously per block, so flash writes happen inside it (there is no asynchronous storage API).

## Buffers and memory

| Area | Placement | Allocated |
| --- | --- | --- |
| Block buffer (Download only) | Ordinary `malloc`, or `transferBuffer` | At begin; `transferBytes` bytes |
| State | Ordinary heap | At begin |

- The block buffer is only copied by the CPU, so it needs no DMA capability or alignment. **An external buffer may in principle be in PSRAM** (not verified on hardware).
- Keep an external buffer valid from begin to end and do not write to it directly.
- Runtime declares the same `wTransferSize` but allocates no block buffer.
- No heap is allocated per block.

## Host OS

| OS | Tool | Verified with this library (v3) |
| --- | --- | --- |
| Linux | `dfu-util` (needs a udev rule for `303a:*`, or sudo) | DETACH confirmed with pyusb (P4X to a Linux PC at high speed). `dfu-util` not yet |
| Windows | `dfu-util` (needs WinUSB) | not yet |
| macOS | `dfu-util` | not yet |

- Operation has been confirmed with ESP32 hosts (two S3 boards at full speed, one P4 in loopback, and P4 to P4X high-speed links), and DETACH on a P4X to Linux PC high-speed link. Transfers were tested with a RAM backend. An actual OTA with `dfu-util`, including the restart, has not yet been confirmed with v3 (v2 was confirmed with `dfu-util` between a P4 and a PC).
- Windows has no built-in DFU driver. This library tags DFU interfaces with the WinUSB compatible ID in its Microsoft OS 2.0 descriptor, which is expected to bind WinUSB automatically; this has not been verified with v3. If it does not bind, install WinUSB with Zadig or similar.
- `dfu-util` may print an error on its final status read, because the device restarts about 500 ms after verification and disappears. The write has finished; check the board.

## Standards and design rationale

### DFU 1.1 mapping

The reference is [USB-IF DFU 1.1](https://www.usb.org/sites/default/files/DFU_1.1.pdf).

| Item | Implementation |
| --- | --- |
| Interface | Class 0xFE, subclass 1. Protocol 1 for Runtime, 2 for Download (§4.1, §4.2) |
| Functional descriptor (§4.1.3) | bcdDFUVersion 1.10, wTransferSize = `transferBytes`, wDetachTimeOut = `detachTimeoutMs` |
| bmAttributes | bitCanDnload always 1. Runtime sets bitWillDetach; Download sets bitManifestationTolerant when `manifestationTolerant`. bitCanUpload is 0 |
| Requests (§3) | DETACH (Runtime), DNLOAD, GETSTATUS, CLRSTATUS, GETSTATE, ABORT. UPLOAD stalls |
| State machine (§6, Appendix A) | A received block is not stored until its data and status stages are complete; it reaches storage after the following GETSTATUS |
| Block numbers | Start at 0 and wrap at 16 bits. A skipped number, a length above `wTransferSize` or an invalid request enters dfuERROR; CLRSTATUS recovers |
| bStatus (§6.1.2) | `EspUsbDfuStatus` values equal the DFU 1.1 status codes (OK, errTARGET … errSTALLEDPKT) |
| bwPollTimeout | `downloadPollMs` while storing, `manifestPollMs` during manifestation |

- In Runtime, an invalid DFU request never moves the interface into a download state.
- DFU 1.1 §4.2 expects a device in DFU mode to expose only the DFU interface. This library's Download adds a protocol-2 interface to the running application's configuration, so that updates happen while the sketch keeps running.

### Rationale

- **Own driver, run-time sizes.** An unused block buffer costs nothing, the block size changes without a rebuild, and several interfaces can coexist. Storage (the backend) is separated from USB state handling. The cost is allocation at begin and a few more callback rules.
- **DETACH is split into "accept?" and "after acceptance".** Refusing at SETUP reaches the host clearly as a STALL. Once accepted, the device always moves on, even through `end()` or a reset, so a host never sees "accepted but nothing happened".
- **The default ROM restart is 100 ms later.** It is scheduled after the status-stage ACK, so the host's control transfer finishes normally.
- **The appDETACH timeout is `min(wValue, detachTimeoutMs)`**, the shorter of the host's request and the device's declaration.

### Main changes from v2

- `CFG_TUD_DFU_XFER_BUFSIZE` became `EspUsbDfuConfig::transferBytes`.
- `onError` takes `EspUsbDfuStatus` instead of `uint8_t` (`unsigned(status)` gives the same number).
- v2's `onDetach(void callback)`, which replaced the default restart, is now `setDetachHandler()`. `onDetach()` is a `bool` accept decision at SETUP.
- Setters return `esp_err_t` and work only while stopped.
- `setBackend()` replaces the storage.

### Not implemented and not verified

- UPLOAD, choosing storage through several alternate settings, and asynchronous storage callbacks are not implemented.
- Even with Runtime only, the Download and built-in OTA code may be linked.
- Not verified: ESP32-S2 hardware, `dfu-util` on each OS, an actual OTA activation and restart with v3, block-number wrap-around, and an EP0 transfer cut off midway.

## Related examples

| Example | What it shows |
| --- | --- |
| [FirmwareDFU](../../examples/FirmwareDFU/) | A HID keyboard plus DFU (Download) on one device; update the application with `dfu-util -D`. 4096-byte blocks |
| [FirmwareBootMode](../../examples/FirmwareBootMode/) | CDC plus DFU (Runtime); enter the ROM loader with `dfu-util -e`, a 1200-baud touch, or a serial command |

Related documents: [firmware update over USB](../ota-over-usb.md), [USB device development guide](../usb-device-guide.md), [troubleshooting](../troubleshooting.md), [migrating from v2](../migrating-from-v2.md)
