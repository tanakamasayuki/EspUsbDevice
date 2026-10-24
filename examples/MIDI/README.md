# EspUsbDevice MIDI

> 日本語版: [README.ja.md](README.ja.md)

Creates a USB MIDI device that sends note / control-change messages to the host
and logs MIDI packets received from the host.

This sketch does not use Arduino-ESP32's built-in USB MIDI class. It registers
`EspUsbDeviceMidi` with `EspUsbDevice`, and this library owns the USB MIDI class
descriptors and bulk endpoints.

## Hardware

- ESP32-S3 or another Arduino-ESP32 board with USB device support
- A USB host such as a PC, DAW, MIDI monitor, or another ESP32 running EspUsbHost
- A separate Serial monitor connection for logs when available

## What It Does

- Starts `EspUsbDevice` as a full-speed USB MIDI device
- Sends C major arpeggio `noteOn()` / `noteOff()` messages every second
- Sends a `controlChange()` message with each step
- Prints 4-byte USB-MIDI event packets received from the host to Serial monitor

## Key APIs

- `EspUsbDeviceMidi MIDI(device)` registers the USB MIDI function.
- `MIDI.noteOn(channel, note, velocity)` sends Note On.
- `MIDI.noteOff(channel, note, velocity)` sends Note Off.
- `MIDI.controlChange(channel, control, value)` sends Control Change.
- `MIDI.programChange()`, `polyPressure()`, `channelPressure()`, and
  `pitchBend()` are also available.
- `MIDI.writePacket(packet)` sends a raw USB-MIDI event packet.
- `MIDI.readPacket(packet)` reads a raw USB-MIDI event packet from the host.

## Notes

- USB MIDI is transferred as 4-byte USB-MIDI event packets.
- `channel` is `0` to `15`, corresponding to displayed MIDI channels 1 to 16.
- Serial monitor output is only for logs. Check MIDI I/O through the USB device
  port.
- Some DAWs and OS tools require manually enabling the MIDI input / output port
  after enumeration.

## See Also

- [Serial](../Serial/) - CDC ACM serial device
- [Keyboard](../Keyboard/) - HID keyboard device

## Walkthrough and checks

### Before running

Generate USB MIDI notes and inspect received event packets. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

`sendStep()` releases the previous note and starts the next; `loop()` also drains `readPacket()`. Open a MIDI monitor or instrument and select this port. The API uses zero-based channel indices, and sending Note On without a matching Note Off can leave the instrument sounding.

### API use and adaptation

A MIDI channel and a USB cable/port are different selectors. Match the API's channel indexing and packet fields to your host application; a MIDI event packet is not the same byte layout as a raw UART message. MIDI transfers control messages, not audio samples.

Always provide the matching release for held notes. Check send results and decide whether to retry or discard when the host is absent. Reset and disconnect can invalidate local note state. Begin with one channel and one port before adding multiplexing or a full MIDI parser.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](MIDI.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
