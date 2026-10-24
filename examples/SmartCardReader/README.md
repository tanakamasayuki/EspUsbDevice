# EspUsbDevice SmartCardReader

> Japanese: [README.ja.md](README.ja.md)

This example turns the board into a USB CCID smart card reader with one slot.
The reader is the library (`EspUsbDeviceCcid`); the card behind the slot is the
sketch.

CCID is the standard class every PC/SC stack speaks, so no driver is needed:
Windows' Smart Card service, macOS, `pcsc_scan` on Linux, and EspUsbHost's
`ccid*` API all see it as a reader.

## Hardware

- USB-device-capable ESP32-S3 (or ESP32-S2 / ESP32-P4) Arduino-ESP32 board
- PC, or another ESP32 running EspUsbHost
- Serial monitor connection for logs and for putting the card in and out

## Behavior

- Enumerates as `bInterfaceClass = 0x0b` with bulk IN / bulk OUT and an
  interrupt IN endpoint for slot change notifications.
- The slot starts with a card in it. The ATR is the PC/SC synthetic ATR for a
  MIFARE Classic 1K, so a host that names cards from their ATR names this one.
- The emulated card answers two instructions:
  - `FF CA 00 00 00` (PC/SC Get UID) with `04 11 22 33` and `9000`
  - `80 01 00 00 Lc <data>` with the same data back and `9000`
  - anything else with `6D00`, like a card that does not know the instruction
- `i` on the serial monitor inserts the card, `r` removes it. Both are reported
  to the host over the interrupt endpoint, so a host application sees insertion
  and removal events.

## Trying It

With `pcsc-lite` on Linux:

```sh
pcsc_scan
```

The reader appears as `EspUsbDevice CCID Reader`, and the ATR printed on card
insertion is the one above. `opensc-tool --atr` and `scriptor` work too.

With another ESP32 running EspUsbHost, see `tests/peer/usb_ccid` in this
repository for a host sketch that opens the reader and runs the same exchanges.

## Writing Your Own Card

Everything specific to the card lives in the `onApdu` callback: it receives the
APDU the host sent and writes the answer, including the two status bytes. The
device handles the CCID protocol around it - slot status, activation, the ATR,
parameters, and abort - so a card only has to answer APDUs.

The callback runs in the TinyUSB device task. Keep it short, and do not call
back into USB APIs from it.

## Notes

- One slot, T=1, short APDU level exchange. Chaining, extended APDUs, PIN pads,
  and clock / data-rate negotiation are out of scope; the class descriptor
  declares none of them.
- The largest CCID message is 271 bytes. Define
  `ESP_USB_DEVICE_CCID_BUFFER_SIZE` at build time for more.
- Do not use this library together with Arduino-ESP32's `USB.begin()`.

## Walkthrough and checks

### Before running

Emulate a CCID reader and respond to selected APDUs. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

The ATR and UID are example arrays, not data from a physical reader chip. `onApdu()` checks input length and output capacity before building data and status bytes. Use UART i/r to change inserted state, then query it from a PC/SC client; this does not implement arbitrary real-card applications.

### API use and adaptation

Separate reader enumeration, interface opening, card presence, activation/ATR and APDU exchange. A successful CCID transfer and an application status word answer different questions. Keep the supplied output capacity and actual response length checks; do not interpret an absent card as an empty valid response.

Reader-specific pseudo-APDUs and escape commands are not universal smart-card commands. Check the exact reader/card combination before reusing a command. Keep callback work bounded and run waiting reader operations through the application path shown in the sketch. Returned data must be copied if needed after a callback.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](SmartCardReader.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
