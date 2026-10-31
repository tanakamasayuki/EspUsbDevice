# EspUsbDevice MIDI

> 日本語版: [README.ja.md](README.ja.md)

Creates a USB MIDI 1.0 device that sends note / control-change messages to the
host and logs MIDI packets received from the host.

This sketch does not use Arduino-ESP32's built-in USB MIDI class or TinyUSB's
MIDI FIFO. It registers `EspUsbDeviceMidi` with `EspUsbDevice`, and this library
owns the USB MIDI descriptors and bulk endpoints. Ring sizes and the function
name are set with `configure()`.

## Hardware

- ESP32-S2 / S3 / P4 board with a native USB OTG port
- A USB host such as a PC, DAW, MIDI monitor, or another ESP32 running EspUsbHost
- A separate Serial monitor connection for logs

## What It Does

- Configures the MIDI function (name `EspUsbDevice MIDI`, 32-event receive and
  send rings), then starts `EspUsbDevice`
- While the host has configured the device, sends a C major arpeggio
  `noteOn()` / `noteOff()` every second, with a `controlChange()` (CC 74) on
  each step, and logs whether each event was queued
- Prints the 4-byte USB-MIDI event packets received from the host, split into
  cable, CIN and the three MIDI bytes
- Reports packets the library dropped (unknown cable, reserved CIN, partial
  packet) through `receiveErrors()`

## Usage

1. Flash the sketch and open the regular Serial monitor.
2. Connect the USB device port to the host.
3. Open a MIDI monitor or a DAW and enable the `EspUsbDevice MIDI` input to see
   the notes.
4. Send notes from the host to the device's output port and watch `MIDI_RX`
   lines in the Serial monitor.

## Key APIs

- `EspUsbDeviceMidi midi(device)` registers the USB MIDI function with one
  cable in each direction. `EspUsbDeviceMidi(device, in, out)` sets them
  separately (1..16 each).
- `midi.configuration()` / `configure(EspUsbMidiConfig)` read and set `name`,
  `rxPackets`, `txPackets`, `rxMode` and the other options. Only while stopped.
  Start from `configuration()` to keep the constructor's cable counts.
- `midi.noteOn(channel, note, velocity, cable = 0)` sends Note On.
- `midi.noteOff(channel, note, velocity, cable = 0)` sends Note Off.
- `midi.controlChange(channel, control, value, cable = 0)` sends Control Change.
- `midi.programChange()`, `polyPressure()`, `channelPressure()` and
  `pitchBend()` are also available.
- `midi.writePacket(packet)` sends a raw USB-MIDI event packet.
- `midi.readPacket(packet)` reads a raw USB-MIDI event packet from the host.
- `midi.receiveCapacity()` / `sendCapacity()` report the allocated rings in
  events; `receiveErrors()` counts dropped packets; `startError()` explains a
  MIDI-specific `begin()` failure.

## Notes

- USB MIDI is transferred as 4-byte USB-MIDI event packets. The header's high
  nibble is the cable, the low nibble the Code Index Number (CIN).
- `channel` is `0` to `15`, corresponding to displayed MIDI channels 1 to 16.
  The helpers check their arguments: a channel above 15, a data byte above 127
  or a pitch bend above 16383 returns `false` with `ESP_ERR_INVALID_ARG` and sends
  nothing. Clamping would send a different message (MIDI 1.0 data bytes are
  7 bits).
- A send returns `true` when the event was copied to the send ring, not when the
  host played it. It returns `false` when the ring is full or the cable is not
  one the host was told about.
- `EspUsbDeviceMidi` is neither `Print` nor `Stream`; it works in whole events.
- Serial monitor output is only for logs. Check MIDI I/O through the USB device
  port.
- Some DAWs and OS tools require manually enabling the MIDI input / output port
  after enumeration.

## See Also

- [MIDIController](../MIDIController/) - MIDI from an ADC and a button
- [MIDIInterface](../MIDIInterface/) - UART MIDI <-> USB MIDI bridge
- [MidiEcho](../MidiEcho/) - packet echo with a held packet on backpressure
- [MIDI API](../../docs/classes/midi.md)

## Walkthrough and checks

### Before running

Generate USB MIDI notes and inspect received event packets. Follow the hardware requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

`sendStep()` releases the previous note and starts the next; `loop()` also drains `readPacket()`. Steps are sent only while `device.ready()`, and the sounding-note state is cleared when the host goes away. The API uses zero-based channel indices, and sending Note On without a matching Note Off can leave the instrument sounding.

### API use and adaptation

A MIDI channel and a USB cable/port are different selectors. Match the API's channel indexing and packet fields to your host application; a MIDI event packet is not the same byte layout as a raw UART message. MIDI transfers control messages, not audio samples.

Always provide the matching release for held notes. Check send results and decide whether to retry or discard when the host is absent. Reset and disconnect can invalidate local note state. Begin with one channel and one cable before adding multiplexing or a full MIDI parser.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one note, capacity or cable count. The demonstration's error logging is not a general retry or recovery policy.

## Changes from v2

- Ring sizes and the function name are set with `configure()` instead of
  `CFG_TUD_MIDI_*` build flags. The object is no longer named `MIDI`; there is
  no single global instance.
- Steps are sent only while the host has configured the device, and send results
  are logged.
- The receive log splits out the cable number, and `receiveErrors()` reports
  packets the library rejected (v3 drops unknown cables and reserved CINs instead
  of passing them on).
- A `begin()` failure also prints `midi.startError()`.
- Out-of-range helper arguments now fail with `ESP_ERR_INVALID_ARG` and send
  nothing; v2 clamped data bytes to 7 bits and sent them.

- [Sketch](MIDI.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
