# EspUsbDevice CdcConsole

> 日本語版: [README.ja.md](README.ja.md)

Passes a USB CDC ACM port to code that takes an Arduino `Stream &`.

`EspUsbDeviceCdcSerial` is itself a `Stream`, so a console, a command parser or
a protocol library written for `Serial` can take it directly. No wrapper object
is needed. The sketch is a byte echo written against `Stream &` only.

## Hardware

- ESP32-S2 / S3 / P4 board with a native USB OTG port
- A USB host with a serial terminal (a PC, or another ESP32 running EspUsbHost)
- No Serial monitor output is used

## What It Does

- Configures the CDC port with a 256-byte receive ring, a 512-byte send ring and
  `transferBytes = 64`, then starts `EspUsbDevice`
- Calls `echo(console)` from `loop()`. `echo()` only knows `Stream &`: it reads
  a byte while one is available and there is room to send, and writes it back.

## Usage

1. Flash the sketch.
2. Connect the USB device port to the host.
3. Open the new serial port: `/dev/ttyACM*` on Linux (`picocom /dev/ttyACM0`),
   a `COM` port on Windows, `/dev/cu.usbmodem*` on macOS.
4. Type characters. Each one comes back as typed. Turn off local echo in the
   terminal to see the device's echo only.

## Key APIs

- `EspUsbDeviceCdcSerial console(device)` registers a CDC ACM function.
- `console.configure(EspUsbBulkStreamConfig)` sets `rxCapacity`, `txCapacity`
  and `transferBytes`. Only while stopped.
- Through `Stream &`: `available()` / `read()` never wait. `availableForWrite()`
  is the free space in the send ring. `write()` waits for room up to
  `setTxTimeoutMs()` (default 1000 ms); it does not wait here because the loop
  checks `availableForWrite()` first.
- `write()` does not require DTR. `connected()` ("configured and DTR=1") is not
  used in this sketch.

## Notes

- A failed `configure()` or `begin()` is printed on `Serial` (115200) of the
  separate debug connection, with the error name.
- Do not enable the core's USB CDC on boot; this library owns the OTG
  controller.
- `transferBytes = 64` is the most one IN submission carries. On the P4
  high-speed controller (bulk packet 512) each submission is a short packet.

## See Also

- [Serial](../Serial/) - CDC with line coding, DTR and break callbacks
- [SerialMulti](../SerialMulti/) - several CDC ports on one device
- [CompositeCdcMidi](../CompositeCdcMidi/) - CDC and MIDI in one device
- [CDC API](../../docs/classes/cdc.md)

## Walkthrough and checks

### Before running

Follow the hardware requirements above and select the board and the example's
profile. [Example workflow](../../docs/example-workflow.md) explains
installation, connectors, USB roles and the difference between startup and a
usable peer.

### Follow the code

`setup()` only configures and starts. All behavior is in `echo(Stream &)`, which
compiles unchanged against `Serial`, a `HardwareSerial` or a network client.

### API use and adaptation

The amount `available()` reports is not a line or a packet. A line-based
console must collect bytes until its own delimiter. Code written for `Serial`
may assume `write()` never fails; on USB it returns fewer bytes after a
timeout, a disconnect or a bus reset.

### Verify one change at a time

Run the unchanged example first and check the echo. Then replace `echo()` with
your own `Stream &` consumer.

## Changes from v2

This example is new in v3. v2 has no corresponding example.

- [Sketch](CdcConsole.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
