# EspUsbDevice MIDIController

> 日本語版: [README.ja.md](README.ja.md)

Turns ADC and button input into USB MIDI messages.

## Hardware

- ESP32-S3 or another Arduino-ESP32 board with USB device support
- A potentiometer or other analog input on `CONTROLLER_PIN`
- A button on `BUTTON_PIN`; default is GPIO0 for the BOOT button
- A USB MIDI host such as a PC, DAW, or MIDI monitor
- A separate Serial monitor connection for logs when available

## What It Does

- Smooths analog input and sends it as MIDI CC 74
- Sends C4 Note On on button press and Note Off on release
- Enumerates as a USB MIDI device on the host

## Key APIs

- `MIDI.controlChange(channel, control, value)` sends Control Change.
- `MIDI.noteOn(channel, note, velocity)` sends Note On.
- `MIDI.noteOff(channel, note, velocity)` sends Note Off.

`channel` is `0` to `15`, corresponding to displayed MIDI channels 1 to 16.

## Adjustments

- `CONTROLLER_PIN`: analog input pin
- `BUTTON_PIN`: button input pin
- `MIDI_CC_CUTOFF`: control number to send
- `MIDI_NOTE_C4`: note number assigned to the button

## See Also

- [MIDI](../MIDI/) - basic USB MIDI send/receive
