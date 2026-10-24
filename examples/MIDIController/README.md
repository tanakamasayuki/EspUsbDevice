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

## Walkthrough and checks

### Before running

Convert an analog control and a switch to MIDI messages. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Wire the potentiometer wiper to CONTROLLER_PIN and a normally open switch from BUTTON_PIN to GND, with compatible ADC voltage and common ground. Trace smoothing and 0–127 mapping before `controlChange()`. The button example detects changes but does not debounce contacts or retain failed sends; add those policies for a real controller.

### API use and adaptation

A MIDI channel and a USB cable/port are different selectors. Match the API's channel indexing and packet fields to your host application; a MIDI event packet is not the same byte layout as a raw UART message. MIDI transfers control messages, not audio samples.

Always provide the matching release for held notes. Check send results and decide whether to retry or discard when the host is absent. Reset and disconnect can invalidate local note state. Begin with one channel and one port before adding multiplexing or a full MIDI parser.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](MIDIController.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
