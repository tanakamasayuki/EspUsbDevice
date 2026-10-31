# Vendor (custom bulk transfer)

[日本語](vendor.ja.md)

## Overview and when to use it

`EspUsbDeviceVendor` creates one class 0xFF (Vendor Specific) interface and
exchanges data in your own format over one bulk IN/OUT endpoint pair. Of the
standard classes it moves the most data the fastest. It can also receive vendor
requests (control transfers).

Which one to use:

- A dedicated Host application (libusb, pyusb, WinUSB, WebUSB and so on) speaking your own protocol.
- Moving a lot of data quickly.
- If you want a serial terminal to work, use [CDC](cdc.md); for small amounts of data without a driver, use [HID Vendor](hid.md).

A serial terminal cannot open a vendor interface. On the Host, an application
selects the interface and opens it.

The transfer machinery (rings, DMA, borrowed transfers, cancellation, reset by
standard requests) is shared with [CDC](cdc.md) and is described in this
document.

Operation has been checked over a full-speed link between two ESP32-S3 boards,
a full-speed loopback on one ESP32-P4 (including rings in PSRAM), and a
high-speed link between an older P4 and a P4X (both directions). Interoperation
with PC operating systems has not been checked.

Terms:

- **MPS** (Max Packet Size): the longest packet. For bulk, 64 bytes or less at FS and 512 bytes at HS.
- **Ring**: a circular buffer that holds data in transit. DMA does not use it directly.
- **Borrowed transfer**: a transfer that hands the application's buffer to DMA without copying (`writeDirect` / `readDirect`).
- **ZLP** (Zero Length Packet): a packet of length 0. It tells the Host that a transfer ending exactly on a multiple of the MPS is over (USB 2.0 §5.8.3).

## Basic usage

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceVendor vendor(device);

void setup() {
  EspUsbVendorConfig config;      // alias of EspUsbBulkStreamConfig
  config.rxCapacity = 2048;
  config.txCapacity = 4096;
  if (vendor.configure(config) != ESP_OK) return;  // succeeds only while stopped
  if (!device.begin()) {
    // device.lastError() / device.lastErrorName() give the reason
    return;
  }
}

void loop() {
  uint8_t buffer[64];
  const size_t n = vendor.read(buffer, sizeof(buffer));  // returns what has arrived
  if (n) vendor.write(buffer, n);  // returns the bytes accepted (not always all)
}
```

## Settings

Every field of `EspUsbVendorConfig` (the same type as `EspUsbBulkStreamConfig`):

| Item | Default | Range / meaning | FS/HS difference |
| --- | --- | --- | --- |
| `rxCapacity` | 512 | Receive ring size (bytes). At least 1 with `rxMode = Stream`, 0 otherwise | none |
| `txCapacity` | 512 | Transmit ring size (bytes). 0 disables `write()`, leaving only borrowed sends | none |
| `rxBuffer` / `txBuffer` | `nullptr` | Supply the ring storage yourself. `nullptr` allocates at begin. PSRAM is fine | none |
| `transferBytes` | 512 | Most bytes handed to DMA per copied send (1-65535). Must not be 0 when `txCapacity` is 1 or more | Values below the MPS are accepted |
| `autoFlush` | `true` | `true` starts sending on every `write()`. `false` holds data until `flush()` | none |
| `streamTermination` | `FunctionDefault` | Whether a copied send ends with a ZLP. For Vendor, `FunctionDefault` is `Continue` (no ZLP) | The length that gets a ZLP depends on the MPS |
| `rxMode` | `Stream` | Receive mode: `Stream` / `Callback` / `Borrowed` / `Disabled` ([Receive modes](#receive-modes)) | none |

The constructor's second argument `endpointSize` (default 64) is the bulk MPS
on a full-speed link.

| `endpointSize` | Result |
| --- | --- |
| 8, 16, 32, 64 | Used as the FS MPS |
| above 64 | Treated as 64 |
| anything else (e.g. 10) | begin fails with `ESP_ERR_INVALID_SIZE` |
| (high-speed link) | Always 512 |

### Rules for configuring, starting and stopping

- `configure()` succeeds only while stopped. While running it returns `ESP_ERR_INVALID_STATE`; for invalid values it returns `ESP_ERR_INVALID_ARG`. Either way the previous settings stay.
- Rings and DMA buffers are allocated at begin. On failure, whatever was allocated is returned and USB does not start.
- To change settings, call `device.end()` → `configure()` → `device.begin()`. No rebuild is needed. The Host sees a disconnect and a reconnect.
- Callbacks (`onRx`, `onRxData`, `onTxComplete`, `onDirectTxComplete`, `onDirectRxComplete`, `onControlRequest`) are also registered only while stopped. While running they return `ESP_ERR_INVALID_STATE`.
- A requested size is never silently reduced.

`configure()` returns `ESP_ERR_INVALID_ARG` when:

- `txCapacity` is 1 or more but `transferBytes` is 0
- `rxMode = Stream` with `rxCapacity` 0, or a mode other than `Stream` with a nonzero `rxCapacity`
- `rxBuffer` / `txBuffer` is given with a capacity of 0, or the RX and TX regions overlap
- `streamTermination` or `rxMode` is out of range

Why begin fails (read with `device.lastError()`):

| Value | Cause |
| --- | --- |
| `ESP_ERR_INVALID_ARG` | `rxMode = Callback` without `onRxData`, or `rxMode = Borrowed` without `onDirectRxComplete` |
| `ESP_ERR_INVALID_SIZE` | `endpointSize` is not 8/16/32/64 (values above 64 count as 64). Or `device.lastError()`: the controller's transmit FIFO cannot hold the IN endpoints of the whole configuration ([FS and HS differences](#fs-and-hs-differences)) |
| `ESP_ERR_NO_MEM` | A ring or DMA buffer could not be allocated |
| `ESP_ERR_INVALID_STATE` | Already started |

## API

### State and capacity

| Function | Description |
| --- | --- |
| `mounted()` | The Host has selected a configuration and the endpoints are open |
| `configuration()` | The requested settings (`FunctionDefault` and the like are returned as given) |
| `readCapacity()` / `writeCapacity()` | The capacity actually allocated. 0 before start and after stop |
| `endpointSize()` | The FS MPS given to the constructor |

### Copied sends

| Function | Return value / behaviour |
| --- | --- |
| `write(data, n)` / `write(byte)` | Copies into the ring and returns the bytes accepted; only part when there is not enough room. Does not wait |
| `flush()` | Starts sending what is queued. Does not wait for completion |
| `availableForWrite()` | Bytes `write()` would accept now (`int`; large values become `INT_MAX`) |
| `writeAvailable()` | The same as `size_t` |
| `waitWritable(n, timeoutMs = 100)` | Waits until at least n bytes are free. false at once if n exceeds `writeCapacity()`. Polls every tick |
| `onTxComplete(cb)` | Notifies DMA completion of copied sends (bytes sent) |

- The return value is "how much entered the ring". It does not mean the Host received it. The source buffer can be reused as soon as the call returns.
- Free space is 0 during a borrowed send, while stopped, without a TX ring, and while the IN endpoint is halted. A query reserves nothing, so check `write()`'s return value as well.
- `EspUsbDeviceVendor` implements Arduino `Print`, so `print()` works too.

### Copied receive

| Function | Return value / behaviour |
| --- | --- |
| `available()` | Bytes ready to read |
| `read(buffer, size)` | Returns what has arrived. Does not wait |
| `read()` | One byte (-1 if none) |
| `peek()` | The first byte without consuming it (-1 if empty) |
| `onRx(cb)` | Reports `available()` on every receive |

Received data is not lost even when the ring is smaller than one packet. The
part that does not fit stays in the DMA buffer, and the next receive is held
until it has been read (the Host gets NAKs and waits). So `available()` can
exceed `readCapacity()`.

### Message boundaries and ZLPs

The copied path is a byte stream. Several `write()` calls may go out together,
and one `write()` is not necessarily one Host read. Frame your messages on the
Host side, for example by putting a length in your protocol.

| `streamTermination` | Behaviour |
| --- | --- |
| `FunctionDefault` | `Continue` for Vendor, `EndWithShortPacket` for CDC |
| `Continue` | No ZLP when the ring empties |
| `EndWithShortPacket` | If a send ends on a multiple of the MPS and the ring is empty, a ZLP follows |

To say explicitly "this transfer ends here", use `EndWithShortPacket` on a
borrowed send.

### Receive modes

| `rxMode` | How data arrives | `rxCapacity` |
| --- | --- | --- |
| `Stream` | Read with `available()` / `read()` | 1 or more |
| `Callback` | Delivered to `onRxData(data, length)`; `data` is valid only during the callback | 0 |
| `Borrowed` | Delivered into a buffer the application passes to `readDirect()` | 0 |
| `Disabled` | Nothing is received. The OUT endpoint stays in the descriptor but is never armed | 0 |

With `Disabled`, `available()` / `read()` return 0 and `readDirect()` returns
`NotSupported`.

### Borrowed transfers

For large data or speed, the application's buffer can go straight to DMA.

Buffer requirements (both directions):

- Start address on a 64-byte boundary
- Entirely in DMA-capable internal RAM. PSRAM is refused (`NotDmaCapable`)
- Not modified or freed until the completion callback runs

```cpp
// Register before begin
vendor.onDirectTxComplete([](const EspUsbTransferResult &result) {
  // result.buffer is back with the application. Check result.status and result.actualBytes
});

// After begin
auto submission = vendor.writeDirect(dmaBuffer, length,
                                     EspUsbTransferTermination::EndWithShortPacket);
if (!submission) {
  // Not accepted. submission.error says why; the buffer still belongs to the application
}
```

**Borrowed send (`writeDirect`)**

- One instance runs one borrowed send at a time. It is also `Busy` while copied data is still pending.
- `Continue` adds no ZLP. `EndWithShortPacket` sends a ZLP before completing when the length is a multiple of the MPS.
- No short packet or ZLP is inserted in the middle. There is one completion for the whole transfer.
- `maxDirectTransfer()` gives the length limit (2147483647 on 32-bit ESP32, 0 while stopped). Lengths that do not fit one DMA programming of the controller are split by the library.

**Borrowed receive (`readDirect`)**

```cpp
EspUsbVendorConfig config;
config.rxMode = EspUsbVendorRxMode::Borrowed;
config.rxCapacity = 0;
config.txCapacity = 0;     // 0 if copied sends are not used either
config.transferBytes = 0;
vendor.configure(config);
vendor.onDirectRxComplete([](const EspUsbTransferResult &done) {
  // only done.actualBytes are received data
});
device.begin();

auto submission = vendor.readDirect(dmaBuffer, capacity);
```

- `capacity` must be a multiple of 64 and of the current MPS (a multiple of 64 at FS, of 512 at HS). Otherwise `BadArgument`.
- It completes when the buffer is full, or on a short packet or ZLP.
- Post the next receive with another `readDirect()`. While nothing is posted, OUT is not accepted and the Host waits. The callback may post the next buffer.
- `maxDirectReceive()` gives the upper limit.

**Submission result (`EspUsbTransferSubmission`)**

Check acceptance with `if (submission)`. On rejection no completion callback
follows and the buffer stays with the application.

| `error` | Meaning |
| --- | --- |
| `None` | Accepted. `id` is nonzero |
| `NotSupported` | The current `rxMode` does not allow this operation |
| `NotMounted` | Not connected, or the endpoints are not open |
| `BadArgument` | Bad buffer or length, no completion callback, or a receive capacity that is not a multiple of the MPS/64 |
| `NotAligned` | Start address not on a 64-byte boundary |
| `NotDmaCapable` | Memory DMA cannot use (PSRAM and so on) |
| `Busy` | A transfer is already in progress in that direction |
| `TransferFailed` | The controller refused it |

**Completion result (`EspUsbTransferResult`)**

The completion callback runs once on the USB worker. It carries `buffer`,
`requestedBytes`, `actualBytes`, `status`, `actualBytesKnown` and `id`.

| `status` | Meaning |
| --- | --- |
| `Success` | Done. `actualBytes` is valid. It does not mean the Host application processed it |
| `BusReset` | Discarded by a bus reset or similar. How much went out is unknown (`actualBytesKnown = false`) |
| `Stopped` | Returned after `end()` stopped DMA. How much went out is unknown |
| `TransferError` | Lower-level transfer error (including HALT) |
| `Cancelled` | Returned because of a cancellation |

Allowed in the callback: posting the next prepared buffer, notifying another task.
Not allowed: blocking, `end()`, changing settings, destroying the Function.

**Cancellation**

- `cancelDirectTx(id)` / `cancelDirectRx(id)` request a cancellation. With `id` omitted (0) the current transfer is targeted.
- `ESP_OK` means "the cancellation was accepted", not that the buffer is back. The buffer comes back through the completion callback (`Cancelled` or another status).
- With nothing to cancel or a mismatching `id`, the result is `ESP_ERR_INVALID_STATE`. An old `id` never cancels a newer transfer. If the controller cannot cancel, `ESP_ERR_NOT_SUPPORTED`.
- `id` keeps increasing across begin/end.
- Copied `write()` / `read()` cannot be cancelled.

### Vendor requests (control transfers)

`onControlRequest(callback)` receives vendor/class requests addressed to this
interface. The callback runs at each stage: SETUP, DATA and ACK. Start the data
stage with `sendControlResponse(request, data, length)` (the data to send for
IN, the destination for OUT).

```cpp
vendor.onControlRequest([&](const EspUsbDeviceVendorControlRequest &r) {
  if (r.bmRequestType != 0x41 || r.bRequest != 0x11 ||
      !r.wLength || r.wLength > sizeof(commandBuffer)) return false;
  if (r.stage == ESP_USB_DEVICE_CONTROL_STAGE_SETUP)
    return vendor.sendControlResponse(r, commandBuffer, r.wLength);  // OUT destination
  if (r.stage == ESP_USB_DEVICE_CONTROL_STAGE_DATA)
    return r.actualBytes == r.wLength && validate(commandBuffer, r.actualBytes);
  if (r.stage == ESP_USB_DEVICE_CONTROL_STAGE_ACK)
    apply(commandBuffer, r.actualBytes);
  return true;
});
```

- Returning `false` at SETUP/DATA rejects the request (STALL). Validate at DATA and apply at ACK. Returning `false` at ACK cannot undo a completed request.
- `actualBytes` is the number of bytes actually moved in the data stage; 0 at SETUP.
- Keep the source or destination buffer valid until the control transfer ends.
- `rawRequest` is an internal reference for answering. It cannot be stored to answer later.
- **Avoid bRequest 0x01 and 0x02.** The library answers two exact requests itself, before this callback:
  - WebUSB GET_URL: bRequest 0x01, bmRequestType 0xC0, wIndex 2 (only when `webusbEnabled`).
  - Microsoft OS 2.0 descriptor set: bRequest 0x02, bmRequestType 0xC0, wValue 0, wIndex 7 (only when the descriptor set is built).

  Other requests with bRequest 0x01 or 0x02 reach `onControlRequest()`. Using other codes still keeps your protocol clear of these two.

## Reset by standard requests

When the Host sends one of the following standard requests, Vendor and CDC bulk
transfers stop and reset the affected transfer. A borrowed buffer is returned
exactly once, after DMA has stopped and older completions have been processed.

| Host request | Affects | Result of a borrowed transfer | Data toggle |
| --- | --- | --- | --- |
| SET_INTERFACE (alt 0) | Both directions of that interface | `Cancelled` | Both back to DATA0 |
| SET_FEATURE(ENDPOINT_HALT) | That endpoint only | `TransferError` | Stays halted |
| CLEAR_FEATURE(ENDPOINT_HALT) | That endpoint only | `Cancelled` | Back to DATA0 |

- Other Functions, and the direction not named, are not affected.
- On the copied path too, the ring for that direction is discarded. Data accepted by `write()` but not yet sent can be lost. If delivery must be guaranteed, confirm and resend in your application.
- While IN is halted or the reset is still being completed, `write()` and `writeAvailable()` return 0.
- To watch these requests, use `device.onAnyControlRequest()`.

## FS and HS differences

| Item | FS (12 Mbps) | HS (480 Mbps) |
| --- | --- | --- |
| Bulk MPS | 64 (8/16/32 also possible with `endpointSize`) | 512 |
| `transferBytes` | 1-65535. Below the MPS is fine | same |
| `readDirect()` capacity | multiple of 64 | multiple of 512 |
| Length that gets a ZLP | multiple of the MPS | multiple of 512 |

- Non-control IN endpoints are limited to 4 on ESP32-S3 and 7 on the P4 high-speed controller. Vendor uses one IN endpoint.
- Every IN endpoint also needs at least one packet in the controller's transmit FIFO, which is shared by all endpoints (256 words on ESP32-S2/S3 and the P4 FS controller, 1024 words = 4 KiB on the P4 HS controller, including the receive FIFO). `device.begin()` checks the whole configuration and fails with `ESP_ERR_INVALID_SIZE` if it does not fit, instead of a device that enumerates and then cannot open its endpoints. On the P4 HS controller, five Vendor Functions with default settings fit; a sixth (six 512-byte bulk IN packets) does not.
- For a sustained stream to the Host, giving each bulk IN endpoint two packets of transmit FIFO can help. `EspUsbDeviceConfig::bulkInBuffering` selects this (default `Auto`: double only if all of them fit); `device.bulkInDoubleBuffered()` shows the result.

## Buffers and memory

| Region | Where it can live |
| --- | --- |
| `rxBuffer` / `txBuffer` (rings) | Internal RAM or PSRAM. No alignment requirement |
| Automatically allocated rings | Plain `malloc` (placement follows the sdkconfig malloc settings) |
| DMA buffers and the copied-send staging | Allocated by the library in DMA-capable internal RAM, 64-byte aligned |
| Borrowed transfer buffers | Must be DMA-capable internal RAM. PSRAM is refused with `NotDmaCapable` |

- The library never frees `rxBuffer` / `txBuffer`. Keep them valid from begin to end.
- On a controller that supports HS (the P4 high-speed controller), the receive DMA buffer is allocated at 512 bytes, also when the link runs at FS.
- When `device.end()` returns `ESP_OK`, everything allocated has been freed and every borrowed buffer has been returned. Calling `device.end()` from a USB callback is rejected with `ESP_ERR_INVALID_STATE`.
- A program that does not use Vendor does not link its implementation.

## Host OS side

| OS | What is needed | What is known |
| --- | --- | --- |
| Windows | WinUSB | The library emits a Microsoft OS 2.0 descriptor (compatible ID `WINUSB` and `DeviceInterfaceGUIDs`) for every Vendor interface, so WinUSB binds without an INF by design |
| Linux | nothing (libusb, pyusb) | Opening it as a normal user needs a udev rule |
| macOS | nothing (libusb, pyusb) | No standard class driver binds, so libusb can open it |

- On Windows, the GUID a Host application looks for is set with `EspUsbDeviceConfig::deviceInterfaceGuid`. The default GUID is shared by every product using this library, so give your product its own.
- A single-interface device carries the compatible ID directly under the set header; a multi-interface device puts it in a function subset (`msOs20Layout = AUTO`). Windows uses function subsets only through usbccgp, the parent driver for composite devices.
- Windows stores the registry values read from the descriptor (`DeviceInterfaceGUIDs`) per VID/PID/serial and re-reads them only when `wVendorRevision` changes. By default the library derives that value from the descriptor contents, so it changes whenever the contents do.
- For WebUSB, set `EspUsbDeviceConfig::webusbEnabled = true`, and `webusbUrl` if you want a landing page.
- The Windows behaviour above was confirmed with v2, which emits descriptors of the same form. It has not been checked on Windows with v3. macOS has not been checked either.
- See the [advanced guide](../usb-device-advanced.md) for how to inspect this.

Common Host-side pitfalls:

- If an echo moves more data than the Device's RX and TX storage combined, and the Host sends all its OUT data before reading IN, both sides wait forever. Receive and send in parallel on the Host.
- With `Continue`, a send ending on a multiple of the MPS has no ZLP. If the Host reads "until a short packet", put the length in your protocol or use `EndWithShortPacket`.

## Standards and design rationale

- **Why the FS MPS is limited to 8/16/32/64**: these are the only values USB 2.0 §5.8.3 allows for a full-speed bulk endpoint. High-speed bulk is 512 only.
- **ZLP**: under USB 2.0 §5.8.3, when a transfer ends exactly on a multiple of the MPS, the Host waits for another packet. A ZLP is sent only when the end has to be signalled.
- **Per-instance settings**: capacities live in `configure()` rather than in build settings, so one binary can change its configuration and unused resources are not allocated. The cost is allocation failure at begin and stopping before reconfiguring.
- **Submission result as a return value**: each call reports its own reason, which a later call cannot overwrite.
- **Why borrowed receive is post-based**: no internal packet buffer is needed and the application decides how many buffers there are. Requiring a multiple of the MPS and of 64 and handing the whole region to DMA keeps cache maintenance on P4 and similar chips from corrupting neighbouring variables.
- **Why cancellation separates request and return**: DMA is stopped and older completions are processed on the USB worker queue before the buffer is returned, so a buffer never comes back while DMA may still use it.
- **Why `waitWritable()` polls**: so that no wait primitive has to be allocated permanently.

## Changes from v2

- Capacities and modes in `build_opt.h`, and `CFG_TUD_VENDOR`, are not used. Set capacities with `configure()`.
- Register callbacks before starting. Registering while running is an error.
- Borrowed sends return `EspUsbTransferSubmission`. The form `if (!vendor.writeDirect(...))` still works.
- `flush()` only starts sending; it does not wait. Review Host reads that relied on an implicit ZLP.

See [Migrating from v2](../migrating-from-v2.md) for details.

## Related examples

| Example | Content |
| --- | --- |
| [USBVendor](../../examples/USBVendor/) | Bulk echo, vendor requests, WebUSB |
| [FirmwareVendor](../../examples/FirmwareVendor/) | Firmware update over Vendor bulk |

Specifications: [USB 2.0](https://www.usb.org/document-library/usb-20-specification), [Microsoft OS 2.0 Descriptors](https://learn.microsoft.com/windows-hardware/drivers/usbcon/microsoft-os-2-0-descriptors-specification), [WebUSB](https://wicg.github.io/webusb/)
