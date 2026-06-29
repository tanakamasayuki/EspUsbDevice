# EspUsbDevice MIDIInterface

> 日本語版: [README.ja.md](README.ja.md)

Bridge example between UART MIDI 1.0 and USB MIDI 1.0.

It receives 31250-baud serial MIDI, such as DIN MIDI, on `Serial1` and sends it
to the host as USB-MIDI event packets. USB-MIDI event packets received from the
host are written back to `Serial1` as a serial MIDI byte stream.

## Hardware

- ESP32-S3 or another Arduino-ESP32 board with USB device support
- 31250-baud MIDI input/output circuit
- UART MIDI connected to `MIDI_RX_PIN` / `MIDI_TX_PIN`
- A USB MIDI host such as a PC, DAW, or MIDI monitor
- A separate Serial monitor connection for logs when available

## What It Does

- Starts `Serial1` at 31250 baud / 8N1
- Converts serial MIDI channel voice messages to USB-MIDI event packets
- Converts host USB-MIDI event packets back to a serial MIDI byte stream
- Handles Note On / Off, Control Change, Program Change, Pressure, and Pitch
  Bend

## Key APIs

- `MIDI.writePacket(packet)` sends a raw USB-MIDI event packet to the host.
- `MIDI.readPacket(packet)` reads a raw USB-MIDI event packet from the host.

## Limitations

- This example focuses on channel voice messages.
- It is not a complete MIDI parser for SysEx, running status, or real-time
  messages.
- Use a MIDI electrical circuit suitable for a 3.3 V MCU.

## See Also

- [MIDI](../MIDI/) - basic USB MIDI send/receive
- [MIDIController](../MIDIController/) - generate MIDI messages from ADC/button
