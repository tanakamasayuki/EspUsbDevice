# EspUsbDevice MidiEcho

> 日本語版: [README.ja.md](README.ja.md)

A USB MIDI 1.0 device that sends every event packet it receives back to the
host, unchanged.

It shows the packet API (`readPacket()` / `writePacket()`) and how to handle a
full send ring: the sketch holds the one packet it could not send and retries
it, instead of reading on and losing it.

## Hardware

- ESP32-S2 / S3 / P4 board with a native USB OTG port
- A USB host with MIDI software: a PC with `amidi`, a DAW or a MIDI monitor, or
  another ESP32 running EspUsbHost
- No Serial monitor output is used

## What It Does

- Configures one cable in each direction, 16-packet receive and send rings and
  `transferPackets = 4` (one IN submission carries at most 4 packets, 16 bytes),
  then starts `EspUsbDevice`
- While the host has the device configured, reads one 4-byte USB-MIDI event
  packet and writes it back
- Keeps the packet in `pending` while `writePacket()` returns `false`, and
  drops it when the host goes away

## Checking From the Host

On Linux with ALSA:

```sh
amidi -l                                   # find the port, e.g. hw:1,0,0
amidi -p hw:1,0,0 -S '90 3C 64 80 3C 00' -d -t 1
```

`amidi -d` prints the echoed `90 3C 64` and `80 3C 00`. ALSA turns the raw
MIDI bytes into USB-MIDI event packets on cable 0, and the device returns the
same packets.

In a DAW or MIDI monitor, route the device's output port to its input port and
watch what comes back. Some applications need the ports enabled by hand after
enumeration. Windows and macOS have not been checked with this example.

## Key APIs

- `EspUsbDeviceMidi midi(device)` registers a USB MIDI function with one cable
  each way.
- `EspUsbMidiConfig`: `inCables` / `outCables` (1..16; 0 keeps the current
  count), `rxPackets` / `txPackets` (ring sizes in packets), `transferPackets`
  (0 = one bus packet: 64 bytes at full speed, 512 at high speed).
  `midi.configure()` only while stopped.
- `midi.readPacket(packet)` returns `true` with one packet, `false` when none
  is waiting. It never waits.
- `midi.writePacket(packet)` returns `true` when the packet was copied to the
  send ring. It returns `false` when the ring is full, when the host has not
  configured the device, or when the cable or CIN is not one this function may
  send.
- `EspUsbDeviceMidiPacket` holds `header` (cable in the high nibble, Code Index
  Number in the low nibble) and `byte1` to `byte3`.

## Notes

- The library drops received packets with an unknown cable or a reserved CIN
  (0 or 1) and counts them in `receiveErrors()`. So every packet the sketch
  reads is one it may send back.
- MIDI has no byte-stream API: `EspUsbDeviceMidi` is neither `Print` nor
  `Stream`.
- A failed `configure()` or `begin()` is printed on `Serial` (115200) of the
  separate debug connection, with the error name.

## See Also

- [MIDI](../MIDI/) - send notes and log received packets
- [MIDIInterface](../MIDIInterface/) - the same hold-and-retry pattern between
  UART MIDI and USB MIDI
- [CompositeCdcMidi](../CompositeCdcMidi/) - this echo together with a CDC port
- [VendorEcho](../VendorEcho/) - the same pattern for vendor bulk bytes
- [MIDI API](../../docs/classes/midi.md)

## Walkthrough and checks

### Before running

Follow the hardware requirements above and select the board and the example's
profile. [Example workflow](../../docs/example-workflow.md) explains
installation, connectors, USB roles and the difference between startup and a
usable peer.

### Follow the code

`held` means "one packet is waiting to be sent". A new packet is read only when
nothing is held. When the send ring is full, the receive ring fills next, and
the device stops accepting OUT data until the host reads IN. No packet is lost
inside the device.

### API use and adaptation

A MIDI channel and a USB cable are different selectors. When you change the
packet before sending it, keep the cable below `inCableCount()`. When you add
cables, set `inCables` and `outCables` explicitly.

### Verify one change at a time

Run the unchanged example and check the echo of a short message first. Then
send a long burst and check that the count of returned events matches.

## Changes from v2

This example is new in v3. v2 has no corresponding example.

- [Sketch](MidiEcho.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
