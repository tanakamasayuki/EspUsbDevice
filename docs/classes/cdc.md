# CDC ACM (virtual serial port)

[日本語](cdc.ja.md)

## Overview and when to use it

`EspUsbDeviceCdcSerial` is a Function the Host sees as a virtual COM port (the
ACM model of USB CDC). It implements Arduino `Stream`, so you use it like
`Serial`: `print()` / `read()` / `readBytes()`.

Which one to use:

- You want a serial terminal or an existing serial application to talk to it.
- Logs and command exchange.
- For your own protocol at speed use [Vendor](vendor.md); for small amounts of data without a driver use [HID Vendor](hid.md).

One device can carry several CDC Functions, within the endpoint budget
([FS and HS differences](#fs-and-hs-differences)).

The transfer machinery (rings, DMA, borrowed transfers, cancellation, reset by
standard requests) is shared with Vendor. See [Vendor](vendor.md) for the shared
parts; this document focuses on what differs for CDC.

Operation has been checked over a full-speed link between two ESP32-S3 boards,
a full-speed loopback on one ESP32-P4, and a high-speed link between an older P4
and a P4X (both directions). Interoperation with PC operating systems has not
been checked.

## Basic usage

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceCdcSerial console(device, "Console");  // second argument: interface name (optional)

void setup() {
  EspUsbBulkStreamConfig config;   // defaults if left alone (512 bytes RX and TX)
  config.rxCapacity = 1024;
  config.txCapacity = 2048;
  if (console.configure(config) != ESP_OK) return;  // succeeds only while stopped
  if (!device.begin()) return;  // device.lastError() gives the reason
}

void loop() {
  while (console.available()) {
    const int value = console.read();
    if (value >= 0) console.write(uint8_t(value));  // echo
  }
}
```

## Settings

The configuration type is `EspUsbBulkStreamConfig`, the same as Vendor. Each
field means the same as in [Vendor's settings](vendor.md#settings); the ones
whose default differs for CDC are marked.

| Item | Default | Range / meaning | FS/HS difference |
| --- | --- | --- | --- |
| `rxCapacity` | 512 | Receive ring size (bytes). At least 1 with `rxMode = Stream`, 0 otherwise | none |
| `txCapacity` | 512 | Transmit ring size (bytes). 0 makes `write()` return 0 at once | none |
| `rxBuffer` / `txBuffer` | `nullptr` | Supply the ring storage yourself. PSRAM is fine | none |
| `transferBytes` | 512 | Most bytes handed to DMA per copied send (1-65535) | Values below the MPS are accepted |
| `autoFlush` | `true` | Whether every `write()` starts sending | none |
| `streamTermination` | `FunctionDefault` | **For CDC this is `EndWithShortPacket`** (a ZLP when a send ends on a multiple of the MPS and the ring is empty) | ZLP on multiples of 64 at FS, 512 at HS |
| `rxMode` | `Stream` | `Stream` / `Callback` / `Borrowed` / `Disabled` | none |

CDC-specific settings:

| Item | Default | Meaning | When it can change |
| --- | --- | --- | --- |
| Constructor `name` / `setName(name)` | `nullptr` | Interface name (UTF-8). The Host uses it to tell CDC Functions apart | Only while stopped; `ESP_ERR_INVALID_STATE` while running |
| `setTxTimeoutMs(ms)` | 1000 | Upper limit (ms) for `write()` / `flush()` waiting to send. 0 does not wait | Any time. Each call uses the value at its start |
| `setTimeout(ms)` (Stream) | 1000 | Upper limit (ms) for reads such as `readBytes()`. Unrelated to USB transfers | Any time |

- The string given to `setName()` is kept as a pointer. Keep it valid until `device.begin()` returns; the descriptor uses a copy taken then.
- The values of `setTxTimeoutMs()` and `setTimeout()` survive `end()` → `begin()`.
- The rules for settings (`configure()` only while stopped, allocation at begin, the error values) are the same as [Vendor's rules](vendor.md#rules-for-configuring-starting-and-stopping).

## API

### State and notifications

| API | Description |
| --- | --- |
| `mounted()` | The Host has selected a configuration and the endpoints are open |
| `connected()` | `mounted()` and DTR = 1 (a hint that a terminal is open on the Host). `write()` does not require DTR |
| `lineCoding()` | A copy of the current baud, stop bits, parity and data bits. Initially 115200 / 0 / 0 / 8. Returned by value; call again for the latest |
| `lineState()` | A copy of the current DTR / RTS. Both return to false on bus reset |
| `onLineCoding(cb)` / `onLineState(cb)` / `onBreak(cb)` | Notifications of Host changes. Register while stopped (`ESP_ERR_INVALID_STATE` while running) |
| `port()` | This Function's position among CDC Functions, decided at begin |
| `readCapacity()` / `writeCapacity()` | The capacity actually allocated. 0 before start and after stop |
| `configuration()` | The requested settings (`FunctionDefault` and the like as given) |

- Notification callbacks run on the USB worker. Do not block, call `device.end()` or change settings there.
- `onBreak` receives the value of the SEND_BREAK request (duration in ms) unchanged. 0 and 0xFFFF mean what the CDC 1.2 PSTN specification says.
- Baud rate and parity only report what the Host application chose. They have no effect on USB transfer speed. When bridging to a UART, configure the UART from these values.

### Receiving (Arduino Stream)

A CDC Function can be passed directly as a `Stream&`.

```cpp
void consume(Stream &input) {
  input.setTimeout(25);
  uint8_t bytes[16];
  const size_t count = input.readBytes(bytes, sizeof(bytes));
}
consume(console);  // from an application task, after device.begin()
```

| Function | Behaviour |
| --- | --- |
| `available()` / `read()` / `read(buffer, size)` | Return what has arrived. Do not wait |
| `peek()` | The first byte without consuming it. -1 if empty or if `rxMode` is not `Stream` |
| `readBytes()`, `find()`, `parseInt()` and similar | Wait up to the Stream timeout (default 1000 ms) |

- Do not call the waiting functions from a USB worker callback.
- `readString()` and friends use Arduino `String` and therefore the heap. `read()` / `readBytes()` do not.
- No old bytes remain after a reset or end.

### Send timeout and flush

CDC's `write()` and `flush()` **wait, up to a limit**, as Arduino `Serial` does.
Vendor's `write()` / `flush()` do not wait; this is the difference.

- `write()`: copies at once if there is room. Otherwise it keeps the transfer moving and waits up to the limit, then returns the total accepted. Data larger than the ring can go out entirely within the limit. `print()` behaves the same.
- On timeout, disconnect, bus reset or stop it may return a partial count or 0.
- DTR is not a condition for `write()`. Even with `connected()` false, data can be sent once the Host has selected a configuration.
- `flush()`: starts sending queued data and waits, up to the limit, for DMA and any needed ZLP to finish. It does not mean the Host application has read the data. On timeout, unsent data is kept and goes out once the Host resumes reading.
- Even with `autoFlush = false`, `flush()` and a `write()` that has started waiting for room both start sending.
- With `txCapacity` 0, `write()` returns 0 at once.

By calling context:

| Caller | `write()` | `flush()` |
| --- | --- | --- |
| Ordinary task | Waits up to the limit | Waits up to the limit |
| USB worker callback | Accepts what fits, without waiting | Only starts sending |
| Interrupt (ISR) | Returns 0 | Does nothing |

Waiting on the worker would mean waiting for completions that only the worker
itself can deliver. Borrowed sends (`writeDirect`) never wait like this.

### Querying free space

`availableForWrite()` (a `Print` function) returns how many bytes `write()`
would accept now without waiting; values that do not fit an `int` become
`INT_MAX`. Use `writeAvailable()` for a `size_t`. It is 0 during a borrowed
send, while stopped, without a TX ring, and while the IN endpoint is halted.

### End of a copied send (ZLP)

The CDC default is `EndWithShortPacket`. When a copied send ends exactly on a
multiple of the MPS and the ring is empty, a ZLP ends the Host's read, so small
amounts of data are delivered at once even when the Host reads in large units.

- If a continuous stream needs no ZLP, choose `Continue`.
- A ZLP does not make one `write()` a message boundary. Several `write()` calls may go out together.

### Borrowed transfers

CDC also supports `writeDirect` / `readDirect`, per-direction completion
callbacks and `cancelDirectTx` / `cancelDirectRx`, with the same behaviour and
results as Vendor ([Vendor's borrowed transfers](vendor.md#borrowed-transfers)).

- With `rxMode = Borrowed` and `rxCapacity` / `txCapacity` / `transferBytes` at 0, no copy rings are allocated.
- If you do not receive at all, use `rxMode = Disabled` and `rxCapacity = 0`.

## Answering the Host

| Request | Behaviour |
| --- | --- |
| SET_LINE_CODING | Accepted only under the conditions below; after a successful status stage `lineCoding()` is updated and `onLineCoding` is called. Otherwise STALL |
| GET_LINE_CODING | Returns the current value in 7 bytes |
| SET_CONTROL_LINE_STATE | Updates DTR / RTS and calls `onLineState` |
| SEND_BREAK | Calls `onBreak` |
| SET_INTERFACE (data interface, alt 0) | Resets data transfers. A borrowed transfer returns as `Cancelled`; rings are discarded |
| SET_INTERFACE (control interface, alt 0) | Resets only the notification endpoint. Data transfers, DTR and line coding are kept |
| SET_FEATURE / CLEAR_FEATURE(ENDPOINT_HALT) | As for Vendor ([Reset by standard requests](vendor.md#reset-by-standard-requests)) |

SET_LINE_CODING is accepted when the request length (wLength) is 7, exactly 7
bytes arrive in the DATA stage, and the values are in these ranges:

| Field | Accepted values |
| --- | --- |
| baud | nonzero |
| stopBits | 0-2 |
| parity | 0-4 |
| dataBits | 5-8, 16 |

## FS and HS differences

| Item | FS (12 Mbps) | HS (480 Mbps) |
| --- | --- | --- |
| Data bulk MPS | 64 | 512 |
| `transferBytes` | 1-65535. Below the MPS is fine | same |
| `readDirect()` capacity | multiple of 64 | multiple of 512 |
| Length that gets a ZLP | multiple of 64 | multiple of 512 |
| Notification endpoint (interrupt IN) | 8 bytes, bInterval 1 (1 ms) | 8 bytes, bInterval 1 (125 µs) |

- One CDC Function uses two non-control IN endpoints (notification and data). The limit is 4 on ESP32-S3 and 7 on the P4 high-speed controller. With two CDC Functions on S3, no other Function that uses an IN endpoint fits.
- CDC does not send serial-state notifications. The notification endpoint is there because the ACM descriptor requires it.

## Buffers and memory

The same as Vendor ([Vendor's buffers and memory](vendor.md#buffers-and-memory)).

- The `rxBuffer` / `txBuffer` rings can live in PSRAM. The library places DMA buffers in internal RAM.
- Borrowed transfer buffers must be DMA-capable internal RAM. PSRAM gives `NotDmaCapable`.
- Specific to CDC, a 64-byte DMA region for line coding is allocated at begin and freed at end.
- Rings in PSRAM have been checked on hardware with Vendor. CDC uses the same transport layer but has not been checked as CDC.

## Host OS side

CDC ACM works with each OS's standard driver. The table describes how each OS
generally handles it. Interoperation of this version of the library with PCs has
not been checked.

| OS | Standard driver | Appears as |
| --- | --- | --- |
| Windows 10 and later | usbser.sys (no INF needed) | COM port |
| Linux | cdc_acm | `/dev/ttyACM*` |
| macOS | AppleUSBCDC | `/dev/cu.usbmodem*` |

- When an IAD is present, the device descriptor declares class 0xEF / 0x02 / 0x01 (composite device). CDC always emits an IAD, so Windows loads usbccgp as the parent and creates one COM port per CDC Function.
- Give each CDC Function a name to tell them apart (`EspUsbDeviceCdcSerial(device, "Console")`). Windows uses the IAD's iFunction string as the child device name.
- Windows remembers COM port numbers per VID/PID/serial and interface number. Setting a per-board `EspUsbDeviceConfig::serialNumber` keeps the number when the board moves to another USB port.
- On Linux, a normal user needs permission such as the `dialout` group. ModemManager may probe a new ACM device.
- The Windows behaviour above was confirmed with v2, which emits descriptors of the same form. It has not been checked on Windows with v3. macOS has not been checked either.

## Standards and design rationale

- **Descriptors**: following the ACM model (subclass 0x02) of CDC 1.2, the Function emits the Header, Call Management, ACM (bmCapabilities 0x06: line coding and SEND_BREAK) and Union functional descriptors, an interrupt IN for notifications, and a data interface (class 0x0A) with bulk IN/OUT.
- **Why only CDC implements Stream**: CDC stands in for a serial port, so being passable as the same type as `Serial` is worth having. Vendor implements only `Print`; MIDI has an event-based API.
- **Why only CDC waits with a timeout**: so it behaves like Arduino `Serial`. No task or synchronisation object is added for the wait.
- **Default ZLP termination**: so small amounts of data reach a Host that waits in a large read (USB 2.0 §5.8.3).
- **Checking SET_LINE_CODING**: accepting out-of-range values would hand an invalid setting to an application that bridges to a UART. A STALL tells the Host it failed.

## Changes from v2

- Counts and capacities are no longer set with `CFG_TUD_CDC` or `CFG_TUD_CDC_*_BUFSIZE`. Set capacities with `configure()`.
- `onLineCoding` / `onLineState` / `onRx` / `setName` return `esp_err_t` and work only while stopped.
- `lineCoding()` / `lineState()` return values, not references.
- `Device::maxCdcPorts()` is gone. begin validates the whole configuration; on failure check `device.lastError()`.
- `write()` / `flush()` wait up to a limit ([Send timeout and flush](#send-timeout-and-flush)).

See [Migrating from v2](../migrating-from-v2.md) for details.

## Related examples

| Example | Content |
| --- | --- |
| [Serial](../../examples/Serial/) | Echo on one CDC Function |
| [SerialMulti](../../examples/SerialMulti/) | Two named CDC Functions |
| [FirmwareCDC](../../examples/FirmwareCDC/) | Firmware update over CDC |
| [CompositeHidCdcMsc](../../examples/CompositeHidCdcMsc/) | HID with CDC and MSC |

Specification: [USB CDC 1.2](https://www.usb.org/document-library/class-definitions-communication-devices-12) (the CDC and PSTN documents)
