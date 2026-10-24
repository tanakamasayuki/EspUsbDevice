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

## Walkthrough and checks

### Before running

Bridge basic messages between USB MIDI and a MIDI UART. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Serial1 is the MIDI side and Serial is diagnostics; match MIDI_RX_PIN/MIDI_TX_PIN to the board and use an appropriate MIDI electrical interface rather than wiring a DIN socket directly to GPIO. The parser handles selected channel messages with bounded waits, not every SysEx, running-status or real-time interleaving case.

### API use and adaptation

A MIDI channel and a USB cable/port are different selectors. Match the API's channel indexing and packet fields to your host application; a MIDI event packet is not the same byte layout as a raw UART message. MIDI transfers control messages, not audio samples.

Always provide the matching release for held notes. Check send results and decide whether to retry or discard when the host is absent. Reset and disconnect can invalidate local note state. Begin with one channel and one port before adding multiplexing or a full MIDI parser.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](MIDIInterface.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
