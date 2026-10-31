# USB MIDI 1.0

[日本語](midi.ja.md)

## Overview and when to use it

`EspUsbDeviceMidi` is a USB-MIDI 1.0 Function. It sends and receives one MIDI
event at a time (a 4-byte USB-MIDI event packet). Every OS has a standard driver,
and DAWs and music applications see it as a MIDI port.

When to use it:

- Building a MIDI controller (keys, knobs, pads).
- Receiving MIDI from a music application on the Host to drive a sound module or lights.
- One interface carries up to 16 virtual cables (MIDI ports). Several MIDI Functions can also be registered.

MIDI 2.0 (UMP) is out of scope.

Operation has been checked over a full-speed link between two ESP32-S3 boards
and over a high-speed link between ESP32-P4/P4X boards (both with EspUsbHost as
the Host). Interoperation with music applications on each OS has not been
checked.

## Basic usage

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceMidi midi(device);  // one cable (both directions)

void setup() {
  EspUsbMidiConfig config = midi.configuration();  // keeps the constructor's cable counts
  config.name = "ESP MIDI";
  if (midi.configure(config) != ESP_OK) return;
  device.begin();
}

void loop() {
  EspUsbDeviceMidiPacket packet;
  while (midi.readPacket(packet)) {
    // handle one received event
  }
  midi.noteOn(0, 60, 100);   // channel 0 (0-15), note 60, velocity 100, cable 0
  delay(500);
  midi.noteOff(0, 60, 0);
  delay(500);
}
```

Directions are named from the Host's point of view, as USB does: **IN = Device
→ Host = what this device sends**, **OUT = Host → Device = what this device
receives**. The MIDI class specification names embedded jacks from the
Device's side, which is the other way round; keep that in mind.

## Settings

The constructor has two forms.

```cpp
EspUsbDeviceMidi midi(device, 2);       // 2 cables in both directions
EspUsbDeviceMidi midi(device, 4, 1);    // 4 sending (IN), 1 receiving (OUT)
```

Every field of `EspUsbMidiConfig`:

| Item | Default | Range / meaning | FS/HS difference |
| --- | --- | --- | --- |
| `inCables` / `outCables` | 0 / 0 (keep current) | Cables for sending / receiving. 1-16 each; 0 keeps the constructor's (or previous) count | none |
| `name` | `nullptr` | Function name (UTF-8). Copied at begin and published as the interface name | none |
| `rxPackets` | 128 | Receive ring size (events; one event = 4 bytes). 0 unless `Queue` | none |
| `txPackets` | 128 | Transmit ring size (events). 0 disables sending | none |
| `transferPackets` | 0 (automatic) | Most events per send transfer. 0 means one bus packet. Up to 16383 | Automatic value: 16 events (64 bytes) at FS, 128 events (512 bytes) at HS |
| `rxMode` | `Queue` | Receive mode: `Queue` / `Callback` / `Disabled` | none |
| `autoFlush` | `true` | With `false`, nothing is sent until `flush()` | none |
| `streamTermination` | `FunctionDefault` | MIDI's default is `EndWithShortPacket` (a ZLP when a send ends on a multiple of the MPS and the ring is empty) | ZLP on multiples of 64 at FS, 512 at HS |
| `rxBuffer` / `txBuffer` | `nullptr` | Your own ring arrays (`EspUsbDeviceMidiPacket`). `nullptr` allocates at begin | none |

- `configure()` replaces every field, except that a cable count of 0 (the default) keeps the current count: the constructor's, or the previous `configure()`'s.
- `configure()` and `onPacket()` succeed only while stopped. While running they return `ESP_ERR_INVALID_STATE` and keep the previous settings.
- To change settings, call `device.end()` → `configure()` → `device.begin()`.
- `configuration()` returns the values as requested. `transferPackets = 0` stays 0.

`configure()` returns:

| Return value | Condition |
| --- | --- |
| `ESP_OK` | Accepted |
| `ESP_ERR_INVALID_STATE` | Running |
| `ESP_ERR_INVALID_ARG` | A cable count of 0 or 17 and above, `rxMode` out of range, `rxPackets` or `rxBuffer` given with `Callback` / `Disabled`, capacity too large, or `rxBuffer` and `txBuffer` overlapping |
| `ESP_ERR_INVALID_SIZE` | `transferPackets` above 16383 (one transfer would not fit a 16-bit length) |

Cable counts are rejected, never rounded.

If MIDI is not running after begin, `midi.startError()` tells why:
`ESP_ERR_INVALID_ARG` for `rxMode = Callback` without `onPacket()`,
`ESP_ERR_NO_MEM` when memory ran out.

## API

### Sending

| Function | Return value |
| --- | --- |
| `writePacket(packet)` | true if the event was copied into the transmit ring |
| `noteOn` / `noteOff` / `controlChange` / `programChange` / `polyPressure` / `channelPressure` / `pitchBend` | Same. The last argument `cable` is 0-based, default 0 |
| `flush()` | Starts sending queued events (with `autoFlush = false`) |
| `availableForWritePackets()` | Events that can be written now |
| `sendCapacity()` | Events in the allocated transmit ring (0 while stopped) |
| `transferCapacity()` | Actual events per send transfer (0 while stopped) |

- Sending works in 4-byte units. With no room in the ring it returns false; part of an event is never accepted.
- true means "copied into the ring". It does not mean the Host received it.
- Sending from several tasks at once never mixes the bytes of two events.
- It returns false when the cable is `inCables` or above, or when the CIN (Code Index Number, the low 4 bits of the header) is the reserved 0 or 1. Nothing is silently sent to a cable the Host was never told about.
- The helpers take a channel of 0-15 and notes and values of 0-127 (7 bits). `pitchBend` takes 0-16383 (centre 8192). An out-of-range value is not clamped: the call returns false with `device.lastError()` = `ESP_ERR_INVALID_ARG` and sends nothing, because a clamped value would be a different message (MIDI 1.0 data bytes are 7 bits).
- For SysEx and real-time messages, build an `EspUsbDeviceMidiPacket` with the CIN the specification gives (0x4-0x7, 0xF and so on) and send it with `writePacket()`.
- Send functions never wait. They can be called from USB worker callbacks.

### Receiving

| Function | Return value |
| --- | --- |
| `readPacket(packet)` | true if one event was read |
| `availablePackets()` | Events ready to read |
| `receiveCapacity()` | Events in the allocated receive ring (0 while stopped) |
| `receiveErrors()` | Running count of dropped invalid events and fragments. Reset to 0 at begin |
| `onPacket(callback)` | Target for `rxMode = Callback`. `ESP_OK` only while stopped |

- From each received USB packet, events whose cable is `outCables` or above, events with CIN 0 or 1, and trailing fragments shorter than 4 bytes are dropped, and `receiveErrors()` is incremented. Valid events in the same packet are delivered in order.
- The content of MIDI messages and SysEx is not checked. Valid events are passed through unchanged.
- If a USB packet larger than the receive ring arrives, the rest stays in the DMA region and moves into the ring as it drains. The next packet is not accepted until all of it has been read (the Host gets NAKs and waits). So even a ring of 1 loses no events.

### Receiving with a callback

Set `rxMode = Callback` and `rxPackets = 0`, and register `onPacket()` while stopped.

```cpp
EspUsbMidiConfig config = midi.configuration();
config.rxMode = EspUsbMidiRxMode::Callback;
config.rxPackets = 0;
midi.configure(config);
midi.onPacket([](const EspUsbDeviceMidiPacket &packet) {
  // Runs on the USB worker. packet is valid only during this call
});
```

- The callback gets a const reference to one event. Copy the 4 bytes to keep them.
- The callback runs on the USB worker. Do not block, call `device.end()` or change settings there.
- `writePacket()` inside the callback also returns without waiting; false if there is no room.

### One direction only

- Send only: `rxMode = Disabled`, `rxPackets = 0`. No receive ring or receive DMA region is allocated.
- Receive only: `txPackets = 0`. Nothing is allocated for sending.
- The descriptor still lists both bulk IN and OUT, so the endpoint budget must cover both.

## FS and HS differences

| Item | FS (S3, P4 full-speed controller) | HS (P4/P4X high-speed controller) |
| --- | --- | --- |
| Bulk MPS | 64 bytes | 512 bytes |
| Actual value of `transferPackets = 0` | 16 events | 128 events |
| `transferPackets` below the MPS | accepted | accepted |

- The automatic value depends on whether the selected controller supports HS (on P4, any `controller` other than `FullSpeed` gives the HS value).
- One MIDI Function uses one non-control IN endpoint. More cables do not need more endpoints (cables are told apart by the number in the packet header).

## Buffers and memory

| Region | Where it can live |
| --- | --- |
| `rxBuffer` / `txBuffer` (rings) | Internal RAM or PSRAM. DMA does not touch them |
| Automatically allocated rings | Plain `malloc` |
| DMA packet regions | Allocated by the library in DMA-capable internal RAM |

- Keep an array you pass valid, at its full capacity (events × 4 bytes), from begin to end. The library neither frees nor grows it.
- Rings in PSRAM have been checked with Vendor. MIDI rings in PSRAM have not been checked on hardware.
- A program that does not use MIDI does not link its implementation.

## Host OS side

USB MIDI 1.0 works with each OS's standard driver. The table describes how
each OS generally handles it. Interoperation of this version of the library with
PCs has not been checked.

| OS | Standard driver | Appears as |
| --- | --- | --- |
| Windows | usbaudio.sys (USB Audio 1.0 MIDI) | MIDI input and output ports |
| Linux | snd-usb-audio | ALSA rawmidi / sequencer ports (check with `aconnect -l`, `amidi -l`) |
| macOS | CoreMIDI's standard driver | The MIDI Studio window of Audio MIDI Setup |

- Each cable appears as one MIDI port. How ports are named differs between operating systems and applications. `name` is published as the interface name; per-cable names (the jacks' iJack) are not.
- The MIDI interfaces are an Audio Control + MIDI Streaming pair without an IAD (the MIDI 1.0 form). How each OS groups them in a composite device where other Functions use IADs has not been checked.
- Whether one OS application can handle a second MIDI Function depends on the application. EspUsbHost's standard MIDI API handles only the first interface.
- The MIDI part of the descriptor alone is 60 + 16 × (sending cables + receiving cables) bytes (92 bytes for 1/1, 572 for 16/16). A Host with a limit on how long a configuration descriptor it accepts fails to enumerate once there are too many cables.

## Standards and design rationale

- **Descriptors**: following USB-MIDI 1.0, the Function emits an Audio Control interface (subclass 1, header 1.00) and a MIDI Streaming interface (subclass 3, header 1.00). Each cable pairs an embedded jack with an external jack, and each bulk endpoint has a 9-byte standard endpoint descriptor plus the class-specific MS endpoint descriptor (the list of associated jacks).
- **At most 16 cables**: the cable number in the USB-MIDI 1.0 event packet header is 4 bits (USB-MIDI 1.0 §4).
- **Rejecting CIN 0 and 1**: CIN 0x0 and 0x1 are reserved for future extensions (USB-MIDI 1.0 §4, Table 4-1).
- **Why the API is event-based**: so the type guarantees that an event is never split. MIDI is neither `Print` nor `Stream`. Byte-level `write()` / `read()` are not public, and a wrong conversion is rejected at compile time.
- **Benefits of run-time configuration**: reconfiguration without build settings, several MIDI Functions, per-direction savings, small queues. The cost is dynamic allocation at begin and some migration work because invalid cables/CINs are rejected.

## Changes from v2

- `CFG_TUD_MIDI` and `CFG_TUD_MIDI_*` are gone. Set capacities in `EspUsbMidiConfig`.
- Code that called TinyUSB's `tud_midi_*` directly must move to `readPacket` / `writePacket` and friends.
- Cable counts of 0 or 17 and above are rejected.
- `readPacket` / `writePacket` / `noteOn` and the rest are called as in v2.

See [Migrating from v2](../migrating-from-v2.md) for details.

## Related examples

| Example | Content |
| --- | --- |
| [MIDI](../../examples/MIDI/) | Sending notes and control changes, logging what arrives |
| [MIDIController](../../examples/MIDIController/) | Turning ADC and button input into MIDI messages |
| [MIDIInterface](../../examples/MIDIInterface/) | Bridging UART (DIN) MIDI and USB MIDI |

Specification: [USB Device Class Definition for MIDI Devices 1.0](https://www.usb.org/sites/default/files/midi10.pdf)
