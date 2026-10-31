# Smart card reader (CCID)

> 日本語版: [ccid.ja.md](ccid.ja.md)

`EspUsbDeviceCcid` is the Function that makes the board look like a smart card reader to the host (the USB CCID class, Chip/Smart Card Interface Devices).
The library is the reader; the sketch plays the card in it.

- `insertCard()` decides whether a card is present and its ATR (the identification a card answers with at power-up).
- `onApdu()` answers the APDUs (card commands) the host sends.

## Overview and when to use it

- To let PC/SC applications on a PC use the ESP32 as "a reader with a card in it".
- For prototyping authentication tokens or cards, and for testing host software.

Scope:

| Item | Scope |
| --- | --- |
| Slots | One |
| Protocol | T=1 |
| Exchange unit | Short APDU (up to 255 command data bytes, up to 256 response data bytes plus SW1 SW2) |
| Not handled | Extended APDU, chaining, PIN pads, clock or data-rate negotiation, T=0 |

The library does not interpret APDU contents (status words included). The sketch returns them as they are.

## Basic usage

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceCcid reader(device);

void setup() {
  EspUsbCcidConfig config;               // leave messageBytes at 0 (automatic)
  config.name = "Card reader";
  if (reader.configure(config) != ESP_OK) return;

  const uint8_t atr[] = {0x3b, 0x80, 0x01, 0x81}; // a T=1 card
  reader.insertCard(atr, sizeof(atr));

  reader.onApdu([](const uint8_t *apdu, size_t length, uint8_t *reply, size_t capacity) -> size_t {
    // Runs on the USB worker task. Write the answer (SW1 SW2 included) and return its length
    if (capacity < 2) return 0;
    reply[0] = 0x6d; reply[1] = 0x00;    // "instruction not supported"
    return 2;
  });

  if (!device.begin()) { /* check device.lastError() */ }
}

void loop() {}
```

To change settings: `device.end()`, `configure()`, then `device.begin()`.

## Configuration

### `EspUsbCcidConfig`

`configure()` is accepted only while stopped.

| Field | Default | Range and meaning | FS/HS difference |
| --- | --- | --- | --- |
| `messageBytes` | 0 (automatic) | Largest CCID message, 10-byte header included. 0 resolves at begin to `max(271, Bulk-OUT MPS)`. An explicit value is 64 to 65535 | Automatic value: 271 at FS, 512 at HS. An explicit value below the Bulk-OUT MPS makes begin fail with `ESP_ERR_INVALID_SIZE` |
| `removable` | `true` | `true`: the card can be inserted and removed, and changes are reported on a notification endpoint. `false`: a fixed card and no notification endpoint | none |
| `name` | `nullptr` | Interface name (string descriptor) | none |

**How messageBytes is resolved**

- 271 holds exactly one short APDU (command 10 + 4 + 1 + 255 + 1, response 10 + 256 + 2).
- CCID 1.1 §5.1 requires `dwMaxCCIDMessageLength` to be at least the Bulk-OUT wMaxPacketSize for a short-APDU reader, so high speed needs at least 512.
- An explicit value below the MPS is not silently raised; begin fails.
- The resolved value appears in `messageCapacity()` and in the descriptor's `dwMaxCCIDMessageLength`. `configuration().messageBytes` stays as set (0 when automatic).
- The `capacity` passed to `onApdu` / `onEscape` is `messageBytes − 10` (by default 261 at FS, 502 at HS).
- A larger value does not enable extended APDUs or chaining.

### FS and HS

"HS-capable controller" means an ESP32-P4 whose `EspUsbDeviceConfig::controller` is not `FullSpeed`.
The automatic value and the minimum then use 512, also if the host actually connects at full speed.

| Item | FS | HS |
| --- | --- | --- |
| Bulk MPS (USB 2.0 §5.8.3) | 64 bytes | 512 bytes |
| Automatic `messageBytes` | 271 | 512 |
| Explicit `messageBytes` | 64 to 65535 | 512 to 65535 (below 512, begin fails with `ESP_ERR_INVALID_SIZE`) |
| Callback `capacity` (default) | 261 | 502 |
| Notification endpoint | 8 bytes, bInterval 16 (16 ms) | 8 bytes, bInterval 8 (2^(8−1) × 125 µs = 16 ms) |

The endpoints are bulk IN, bulk OUT and a notification IN (no notification when `removable = false`).

## API

### Configuring and starting

| Function | Returns and constraints |
| --- | --- |
| `esp_err_t configure(config)` | Running: `ESP_ERR_INVALID_STATE`. `messageBytes` neither 0 nor 64 to 65535: `ESP_ERR_INVALID_SIZE` |
| `configuration()` | The settings as given |
| `size_t messageCapacity()` | Message capacity in use. 0 before begin and after end |
| `onApdu(callback)` / `onEscape(callback)` / `onPower(callback)` | `esp_err_t`. Stopped only; `ESP_ERR_INVALID_STATE` while running |
| `startError()` | Why the last begin failed |

`device.lastError()` when `device.begin()` fails:

| Value | Cause |
| --- | --- |
| `ESP_ERR_INVALID_STATE` | `removable = false` without `insertCard()` before start, not registered with a Device, already started, or USB already running |
| `ESP_ERR_INVALID_SIZE` | The resolved `messageBytes` is below the Bulk-OUT MPS, or too few endpoints or too little descriptor space |
| `ESP_ERR_NO_MEM` | Allocation failed |

### Card state

| Function | Description |
| --- | --- |
| `bool insertCard(atr, length)` | Inserts the card and sets its ATR. `length` is 2 to 33 (`ESP_USB_DEVICE_CCID_MAX_ATR`, the ISO 7816-3 maximum) |
| `esp_err_t removeCard()` | Removes the card |
| `bool cardPresent()` | `true` while a card is inserted |
| `bool cardPowered()` | `true` once the host activated the card with IccPowerOn |
| `size_t atr(buffer, capacity)` | Copies the current ATR and returns its length |
| `bool mounted()` | `true` while the host has the CCID interface configured |
| `commandCount()` / `apduCount()` / `lastMessageType()` | Commands answered, APDU exchanges, and the last message type (diagnostics) |

- `removable = true` (default): `insertCard()` / `removeCard()` work while running. Changes are reported on the notification endpoint (RDR_to_PC_NotifySlotChange). If the host has not yet read the previous notification, the latest state is held and sent after it. The current state is also reported once when the host configures the reader.
- `removable = false`: `insertCard()` is required before start. While running, `insertCard()` returns `false` and `removeCard()` returns `ESP_ERR_INVALID_STATE`. Both work again after stopping.

### Callbacks

| Callback | Signature | Behaviour |
| --- | --- | --- |
| `onApdu` | `size_t (const uint8_t *apdu, size_t length, uint8_t *response, size_t capacity)` | Write the answer (SW1 SW2 included) within `capacity` and return its length. Returning 0 fails the command (bError = ICC_MUTE) |
| `onEscape` | Same as `onApdu` | Receives the PC_to_RDR_Escape payload and returns the RDR_to_PC_Escape payload. Unregistered or 0 gives CMD_NOT_SUPPORTED |
| `onPower` | `void (bool on)` | `true` when the host activates the card, `false` when it deactivates it |

- Without `onApdu`, every APDU is answered with `6D 00` (instruction not supported).
- With no card, or a card not powered, an APDU fails (ICC_MUTE) without calling the callback.
- The response buffer is the DMA reply buffer itself.

### Calling context

| Operation | Context |
| --- | --- |
| `configure`, callback registration | An application task, while stopped |
| `insertCard`, `removeCard`, state queries | An application task |
| `onApdu`, `onEscape`, `onPower` | Called on the USB worker task |

- Return from callbacks promptly without waiting, and do not perform USB operations on this Function (stop, reconfigure) from inside them.
- There is no API to answer later (keeping the buffer and replying asynchronously). If the work is heavy, prepare the results in advance.

### Transport recovery

- **SET_INTERFACE (alternate 0)**: DMA stops and the bulk and notification endpoints return to DATA0. A partly assembled message, an unread reply and a pending Abort are dropped. The card state and callbacks stay.
- **ABORT class request (CCID 1.1 §5.3.1)**: the slot and sequence are recorded and other commands are refused until the matching PC_to_RDR_Abort arrives.
- **Bulk HALT / CLEAR**: the reader's partly assembled message, unread reply and pending Abort are dropped. No new command is accepted while either bulk endpoint is halted. Only the cleared endpoint returns to DATA0. Card presence, power, callbacks and other readers' traffic are kept.
- **Notification HALT / CLEAR**: the bulk exchange is not dropped, and the latest slot state is reported again after recovery.
- An interrupted exchange is not resumed; the host sends the command again.
- An invalid slot, an oversized message or a truncated message is answered with an error in the response type that request expects.

### Several readers

Several CCID Functions can be registered as separate interfaces. `messageBytes`, card state, callbacks and buffers are per Function.
The limit is the endpoint count of the whole configuration. On S3, two removable readers use up the four non-control IN endpoints.

## Buffers and memory

Each Function allocates the following at begin. There is no per-transfer heap allocation and no CCID task.

| Area | Placement | Size |
| --- | --- | --- |
| Receive packet DMA buffer | DMA-capable internal RAM | Bulk-OUT MPS |
| Message assembly area | Ordinary heap | `messageBytes` |
| Reply DMA buffer (also the callback's output) | DMA-capable internal RAM | `messageBytes` |
| Notification DMA buffer | DMA-capable internal RAM | 8 bytes (when `removable = true`) |
| Bookkeeping | Ordinary heap | small |

- There is no setting for external buffers. DMA buffers are always in internal RAM.
- The message assembly area comes from ordinary `malloc`. When malloc is configured to use PSRAM it may land there (that configuration has not been verified on hardware).
- The next OUT is not accepted until the reply IN and its ZLP have finished (one command at a time on one slot).
- The 33-byte ATR array stays inside each Function as the protocol maximum.
- An unused CCID links no driver or buffers.

## Host OS

| OS | Built-in support | Verified with this library |
| --- | --- | --- |
| Windows | Built-in CCID driver (Smart Card service) | not yet |
| macOS | Built-in CCID support (CryptoTokenKit) | not yet |
| Linux | pcsc-lite with the CCID driver (libccid) | not yet |

- Operation has been confirmed with ESP32 hosts (two S3 boards at full speed, one P4 in loopback, and P4 to P4X high-speed links). Interoperation with PC/SC on each OS, and with real cards or readers, has not yet been confirmed.
- On Linux, `pcsc_scan` shows the reader and card state, and `scriptor` or `opensc-tool` can exchange APDUs.
- Escape commands are reader-specific, not general card commands.

## Standards and design rationale

### CCID 1.1 mapping

The reference is [USB-IF CCID 1.1](https://www.usb.org/sites/default/files/DWG_Smart-Card_CCID_Rev110.pdf).

| Item | Implementation |
| --- | --- |
| Interface | Class 0x0b, subclass 0, protocol 0 (bulk transfers) |
| Class descriptor (§5.1) | bcdCCID 1.10, bMaxSlotIndex 0, dwProtocols T=1, bVoltageSupport 5V/3V/1.8V, dwFeatures 0x000204FE, dwMaxCCIDMessageLength = `messageBytes`, bPINSupport 0, bMaxCCIDBusySlots 1 |
| What dwFeatures declares | Automatic configuration from the ATR; automatic activation, voltage, clock, baud rate, parameter negotiation and PPS; T=1 IFSD exchange; short-APDU-level exchange |
| Clock and rate | Only the defaults, 3.58 MHz and 9600 bps (no negotiation) |
| Endpoints (§5.2) | Bulk OUT, bulk IN, interrupt IN (removable only) |
| Bulk-OUT messages (§6.1) | IccPowerOn, IccPowerOff, GetSlotStatus, XfrBlock, Escape, GetParameters, ResetParameters, SetParameters, Abort. IccClock, T0APDU and the rest get CMD_NOT_SUPPORTED |
| Bulk-IN messages (§6.2) | DataBlock, SlotStatus, Parameters, Escape |
| Interrupt-IN (§6.3) | NotifySlotChange |
| Class requests (§5.3) | ABORT |

The difference between a removable reader and a fixed card (whether there is a notification endpoint) follows CCID 1.1 §3.1.2 and §5.2.

### Rationale

- **`messageBytes` defaults to automatic.** A fixed default cannot meet the 512-byte MPS minimum at high speed, so begin would always fail there. Resolving `max(271, Bulk-OUT MPS)` at begin works unchanged at both speeds. An explicit value below the MPS is refused, not silently corrected.
- **Capacity per Function.** Capacity and the fixed-card setting belong to each Function, so several readers are independent.
- **HALT recovery drops the exchange.** After a standard pipe recovery the reader must not stay stuck waiting. Resending a partial reply could duplicate data the host already read, so exchanges are not resumed.
- **Stop order.** end stops the Device worker and DMA before freeing state.

### Main changes from v2

- `ESP_USB_DEVICE_CCID_BUFFER_SIZE` is gone (defining it is a compile error). Use `EspUsbCcidConfig::messageBytes`. It is automatic by default, so the reader also starts on the P4 high-speed port unchanged.
- `configure` / `onApdu` / `onEscape` / `onPower` / `removeCard` return `esp_err_t`.

## Related examples

| Example | What it shows |
| --- | --- |
| [SmartCardReader](../../examples/SmartCardReader/) | A one-slot reader. Answers `FF CA 00 00 00` (get UID) and an echo command; `i` / `r` on the serial port insert and remove the card |

Related documents: [USB device development guide](../usb-device-guide.md), [troubleshooting](../troubleshooting.md), [migrating from v2](../migrating-from-v2.md)
