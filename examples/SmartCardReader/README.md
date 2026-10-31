# EspUsbDevice SmartCardReader

> 日本語版: [README.ja.md](README.ja.md)

This example turns the board into a USB CCID smart card reader with one slot.
The reader is the library (`EspUsbDeviceCcid`); the card behind the slot is the
sketch.

CCID is the standard class every PC/SC stack speaks, so no driver is needed:
Windows' Smart Card service, macOS, `pcsc_scan` on Linux, and EspUsbHost's
`ccid*` API all see it as a reader.

```cpp
EspUsbDevice device;
EspUsbDeviceCcid reader(device);

EspUsbCcidConfig config;                  // messageBytes 0 = automatic
config.name = "EspUsbDevice CCID Reader";
reader.configure(config);
reader.onApdu(answerApdu);
reader.insertCard(cardAtr, sizeof(cardAtr));
device.begin(identity);
```

## Hardware

- USB-device-capable ESP32-S3 (or ESP32-S2 / ESP32-P4) Arduino-ESP32 board
- PC, or another ESP32 running EspUsbHost
- Serial monitor connection for logs and for putting the card in and out

## Behavior

- Enumerates as `bInterfaceClass = 0x0b` with bulk IN / bulk OUT and an
  interrupt IN endpoint for slot change notifications (`removable = true`, the
  default).
- The slot starts with a card in it. The ATR is the PC/SC synthetic ATR for a
  MIFARE Classic 1K, so a host that names cards from their ATR names this one.
- The emulated card answers two instructions:
  - `FF CA 00 00 00` (PC/SC Get UID) with `04 11 22 33` and `9000`
  - `80 01 00 00 Lc <data>` with the same data back and `9000`
  - anything else with `6D00`, like a card that does not know the instruction
- `i` on the serial monitor inserts the card, `r` removes it. Both are reported
  to the host over the interrupt endpoint, so a host application sees insertion
  and removal events.
- Each APDU, card activation and deactivation is printed, together with a
  status line every 2 seconds.

## Trying It

With `pcsc-lite` on Linux:

```sh
pcsc_scan
```

The reader appears as `EspUsbDevice CCID Reader`, and the ATR printed on card
insertion is the one above. `opensc-tool --atr` and `scriptor` work too:

```sh
echo "FF CA 00 00 00" | scriptor      # 04 11 22 33 90 00
echo "80 01 00 00 02 AB CD" | scriptor # AB CD 90 00
```

With another ESP32 running EspUsbHost, see
[v2 tests/peer/usb_ccid](https://github.com/tanakamasayuki/EspUsbDevice/tree/3fed50d0d777f600432b71381faccd93adb9a202/tests/peer/usb_ccid/) for a
host sketch that opens the reader and runs the same exchanges.

## Writing Your Own Card

Everything specific to the card lives in the `onApdu` callback: it receives the
APDU the host sent and writes the answer, including the two status bytes, into
`reply` (at most `capacity` bytes). Returning 0 fails the command, which the
host sees as an ICC_MUTE error. The device handles the CCID protocol around it -
slot status, activation, the ATR, parameters and abort - so a card only has to
answer APDUs.

The callbacks run on the USB worker task. Keep them short and do not call back
into USB APIs from them. This sketch does not print from them: it copies the
APDU under a lock and prints it from `loop()`.

## Notes

- One slot, T=1, short APDU level exchange. Chaining, extended APDUs, PIN pads
  and clock / data-rate negotiation are out of scope; the class descriptor
  declares none of them.
- The largest CCID message is `messageBytes`. The default 0 resolves at
  `begin()` to 271 bytes at full speed (one whole short APDU) and 512 at high
  speed (the bulk packet, which CCID requires as the minimum). The sketch
  prints the value with `reader.messageCapacity()`. Set `config.messageBytes`
  for more; an explicit value below the bulk packet makes `begin()` fail with
  `ESP_ERR_INVALID_SIZE`.
- Set `config.removable = false` for a fixed card without the notification
  endpoint; `insertCard()` is then required before `device.begin()`.
- Do not use this library together with Arduino-ESP32's `USB.begin()`.

## Walkthrough and checks

### Before running

Select the board's profile (`esp32s3` is the default; `esp32s2`, `esp32p4`
for P4 v1.x and `esp32p4x` for P4 v3.1 or newer; v3.0 is not supported), and locate the
diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md)
explains installation, connectors, USB roles and the difference between
startup and a usable peer.

### Follow the code

The ATR and UID are example arrays, not data from a physical reader chip.
`answerApdu()` checks input length and output capacity before building data
and status bytes. Use `i`/`r` to change the inserted state, then query it from
a PC/SC client; this does not implement arbitrary real-card applications.

### API use and adaptation

Separate reader enumeration, interface opening, card presence, activation/ATR
and APDU exchange. A successful CCID transfer and an application status word
answer different questions. Keep the supplied capacity and actual response
length checks; do not interpret an absent card as an empty valid response.

Reader-specific pseudo-APDUs and escape commands (`onEscape`) are not
universal smart-card commands. Check the exact reader/card combination before
reusing a command. Keep callback work bounded; anything slow belongs in the
sketch task.

### Verify one change at a time

Run the unchanged example first and compare its log with the PC/SC client.
Test with the card absent and then inserted. Change one command at a time.
The error printing is not a retry or recovery policy.

## Changes from v2

- `ESP_USB_DEVICE_CCID_BUFFER_SIZE` is gone (defining it is a compile error).
  The message size is `EspUsbCcidConfig::messageBytes`, automatic by default,
  which also makes the reader start on the P4 high-speed port.
- `configure()`, `onApdu()`, `onPower()` and `removeCard()` return `esp_err_t`;
  the sketch checks them.
- The card is inserted before `device.begin()` (a removable reader also accepts
  it later, and reports it once the host configures the reader).
- The APDU and power callbacks no longer print on the USB worker; `loop()`
  prints what they recorded.

Compile-checked with Arduino-ESP32 3.3.12 for every profile in
`sketch.yaml`; this sketch itself has not been run on hardware.

- [Sketch](SmartCardReader.ino)
- [All examples](../README.md)
- [CCID API](../../docs/classes/ccid.md)
- [Example workflow](../../docs/example-workflow.md)
