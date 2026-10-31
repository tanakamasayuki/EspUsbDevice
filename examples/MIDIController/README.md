# EspUsbDevice MIDIController

> 日本語版: [README.ja.md](README.ja.md)

Turns ADC and button input into USB MIDI messages.

## Hardware

- ESP32-S2 / S3 / P4 board with a native USB OTG port
- A potentiometer or other analog input on `CONTROLLER_PIN` (GPIO5; GPIO16 on
  ESP32-P4). It must be an ADC-capable pin
- A button on `BUTTON_PIN` (GPIO0, the BOOT button on most S2/S3 boards; GPIO35
  on ESP32-P4). Check both pins against your board's schematic
- A USB MIDI host such as a PC, DAW, or MIDI monitor
- A separate Serial monitor connection for logs

## What It Does

- Smooths analog input and sends it as MIDI CC 74 when the value changes
- Sends C4 Note On on button press and Note Off on release
- Enumerates as a USB MIDI device named `EspUsbDevice MIDI Controller`
- Retries a send the send ring refused, so a release is never lost; after the
  host disconnects, it resends the current knob position on the next
  configuration
- Accepts packets the host sends and discards them in a callback, without
  allocating a receive ring

## Key APIs

- `midi.controlChange(channel, control, value)` sends Control Change.
- `midi.noteOn(channel, note, velocity)` sends Note On.
- `midi.noteOff(channel, note, velocity)` sends Note Off.
- All three return `false` when the event could not be queued; the sketch
  updates its "sent" state only on `true`.
- `EspUsbMidiConfig::rxMode = EspUsbMidiRxMode::Callback` with `rxPackets = 0`
  and `onPacket()` delivers received events to a callback instead of a ring.
- `device.ready()` tells whether the host has configured the device.

`channel` is `0` to `15`, corresponding to displayed MIDI channels 1 to 16.
Control and note values are `0` to `127`; a value out of range fails with
`ESP_ERR_INVALID_ARG` and is not sent, so keep the ADC mapping inside that range.

## Adjustments

- `CONTROLLER_PIN`: analog input pin
- `BUTTON_PIN`: button input pin
- `MIDI_CC_CUTOFF`: control number to send
- `MIDI_NOTE_C4`: note number assigned to the button
- `midiConfig.txPackets`: how many events can wait in the send ring

## See Also

- [MIDI](../MIDI/) - basic USB MIDI send/receive
- [MIDIInterface](../MIDIInterface/) - UART MIDI <-> USB MIDI bridge
- [MIDI API](../../docs/classes/midi.md)

## Walkthrough and checks

### Before running

Convert an analog control and a switch to MIDI messages. Follow the hardware requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Wire the potentiometer wiper to CONTROLLER_PIN and a normally open switch from BUTTON_PIN to GND, with compatible ADC voltage and common ground. Trace smoothing and 0–127 mapping before `controlChange()`. `sentCcValue` and `sentPressed` hold what the host has been sent, so a refused send is tried again on the next loop. The button is not debounced; add that for a real controller.

### API use and adaptation

A MIDI channel and a USB cable/port are different selectors. Match the API's channel indexing and packet fields to your host application; a MIDI event packet is not the same byte layout as a raw UART message. MIDI transfers control messages, not audio samples.

Always provide the matching release for held notes. Check send results and decide whether to retry or discard when the host is absent. Reset and disconnect can invalidate local note state. Begin with one channel and one cable before adding multiplexing or a full MIDI parser.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, control number or note. The demonstration's error logging is not a general retry or recovery policy.

## Changes from v2

- A refused `controlChange()` / `noteOn()` / `noteOff()` is retried. In v2 the
  new value was recorded before the send, so a failed send (for example a
  release while the ring was full) was lost.
- Sends happen only while `device.ready()`; after a disconnect the knob
  position is resent and the note state is reset.
- The receive side uses callback mode without a ring. v2 allocated a FIFO it
  never read.
- ESP32-P4 uses its own default pins (GPIO16 ADC, GPIO35 button); v2 used
  GPIO5/GPIO0 on every target.
- The function name is set with `configure()`.

- [Sketch](MIDIController.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
