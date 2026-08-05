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
