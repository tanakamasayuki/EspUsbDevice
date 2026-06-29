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
