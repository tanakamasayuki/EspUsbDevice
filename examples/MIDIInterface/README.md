# EspUsbDevice MIDIInterface

> 日本語版: [README.ja.md](README.ja.md)

Bridge example between UART MIDI 1.0 and USB MIDI 1.0.

It receives 31250-baud serial MIDI, such as DIN MIDI, on `Serial1` and sends it
to the host as USB-MIDI event packets. USB-MIDI event packets received from the
host are written back to `Serial1` as a serial MIDI byte stream.

## Hardware

- ESP32-S2 / S3 / P4 board with a native USB OTG port
- 31250-baud MIDI input/output circuit (opto-isolated input, suitable output
  driver for a 3.3 V MCU)
- UART MIDI connected to `MIDI_RX_PIN` / `MIDI_TX_PIN` (GPIO39 / GPIO40)
- A USB MIDI host such as a PC, DAW, or MIDI monitor
- A separate Serial monitor connection for logs

## What It Does

- Starts `Serial1` at 31250 baud / 8N1
- Configures the MIDI function (name `EspUsbDevice MIDI Interface`, 64-event
  receive and send rings)
- Converts serial MIDI channel voice messages to USB-MIDI event packets
- Converts host USB-MIDI event packets back to a serial MIDI byte stream
- Handles Note On / Off, Poly Pressure, Control Change, Program Change, Channel
  Pressure, and Pitch Bend from the UART side; writes every CIN the host sends
  (including SysEx packets and single bytes) to the UART side
- Holds a UART message the USB send ring could not take and sends it on a
  later loop instead of dropping it

## Key APIs

- `midi.writePacket(packet)` sends a raw USB-MIDI event packet to the host.
  `false` means the send ring is full (or the cable/CIN is invalid); nothing
  was queued.
- `midi.readPacket(packet)` reads a raw USB-MIDI event packet from the host.
- `midi.configuration()` / `configure()` set the ring sizes and the name.
- `ESP_USB_DEVICE_MIDI_CIN_*` constants name the Code Index Numbers.

## Limitations

- The UART-to-USB direction handles channel voice messages only.
- It is not a complete MIDI parser for SysEx, running status, or real-time
  messages on the UART side.
- While a message is held for USB, the UART is not read; its driver buffer
  absorbs the bytes meanwhile.
- Use a MIDI electrical circuit suitable for a 3.3 V MCU.

## See Also

- [MIDI](../MIDI/) - basic USB MIDI send/receive
- [MIDIController](../MIDIController/) - generate MIDI messages from ADC/button
- [MidiEcho](../MidiEcho/) - the same hold-and-retry pattern for an echo
- [MIDI API](../../docs/classes/midi.md)

## Walkthrough and checks

### Before running

Bridge basic messages between USB MIDI and a MIDI UART. Follow the hardware requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Serial1 is the MIDI side and Serial is diagnostics; match MIDI_RX_PIN/MIDI_TX_PIN to the board and use an appropriate MIDI electrical interface rather than wiring a DIN socket directly to GPIO. The parser handles selected channel messages with bounded waits, not every SysEx, running-status or real-time interleaving case. `pendingToUsb` holds one converted message until `writePacket()` accepts it; it is dropped only when no host is configured.

### API use and adaptation

A MIDI channel and a USB cable/port are different selectors. Match the API's channel indexing and packet fields to your host application; a MIDI event packet is not the same byte layout as a raw UART message. MIDI transfers control messages, not audio samples.

Always provide the matching release for held notes. Check send results and decide whether to retry or discard when the host is absent. Reset and disconnect can invalidate local note state. Begin with one channel and one cable before adding multiplexing or a full MIDI parser.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or capacity. The demonstration's error logging is not a general retry or recovery policy.

## Changes from v2

- A message the USB send ring refused is held and retried. v2 dropped it
  silently when `writePacket()` returned `false`.
- Ring sizes and the function name are set with `configure()` instead of
  `CFG_TUD_MIDI_*` build flags.
- The object is named `midi`; v2 used `MIDI`.

- [Sketch](MIDIInterface.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
