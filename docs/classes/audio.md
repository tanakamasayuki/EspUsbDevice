# USB Audio (UAC1 / UAC2)

> 日本語版: [audio.ja.md](audio.ja.md)

`EspUsbAudioFunction` is the Function for USB Audio Class speakers and microphones.

- **Playback** (host to device): the PC sees an output device (a speaker).
- **Capture** (device to host): the PC sees an input device (a microphone).
- Select both and one Function is bidirectional (a headset).

PCM goes through a ring buffer ("ring") with `read()` / `write()`.
Volume, mute, sample-rate and stream-start changes from the host arrive as events.
The library uses its own driver, not TinyUSB's Audio class.

## Overview and when to use it

| Use | Select |
| --- | --- |
| Play PC audio on an ESP32 DAC, I2S output or speaker | Playback |
| Let the PC record an ESP32 microphone or generated audio | Capture |
| A call headset, or a loopback test | Playback + Capture |

- UAC1 and UAC2 are both available. UAC1 is the default; select UAC2 in the constructor.
- Volume and mute are reported only. They are never applied to the PCM.
- Codec and I2S setup, and following an external codec clock, are the application's job.

## Basic usage

The order is: create the Device and Function, register streams and formats, `configure()`, then `device.begin()`.

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbAudioFunction audio(device, EspUsbAudioProtocol::Uac2); // Uac1 when omitted
EspUsbAudioPlaybackStream &playback = audio.addPlaybackStream();
uint8_t pcm[192];

void setup() {
  // {sampleRate, channels, bytesPerSample, bitsPerSample}
  if (!playback.addFormat({48000, 2, 2, 16})) return; // reason: device.lastError()

  EspUsbAudioConfig config;
  config.playbackBufferFrames = 480; // 10 ms (480 frames x 4 bytes)
  if (audio.configure(config) != ESP_OK) return;

  if (!device.begin()) { /* check device.lastError() */ }
}

void loop() {
  size_t n = playback.read(pcm, sizeof(pcm)); // bytes, rounded down to whole frames
  // pass n bytes of PCM to the DAC

  EspUsbAudioEvent event;
  while (audio.pollEvent(event)) {
    // SampleRateChanged, MuteChanged, VolumeChanged, StreamStateChanged
  }
}
```

- `device.begin()` / `device.end()` start and stop the Function. The Function's own `begin()` is not public, so calling it is a compile error.
- To change settings: `device.end()`, then `configure()` / `clearFormats()` / `addFormat()`, then `device.begin()`. No rebuild is needed.

## Configuration

### Constructor

| Argument | Default | Meaning |
| --- | --- | --- |
| `device` | — | The `EspUsbDevice` to register with |
| `protocol` | `EspUsbAudioProtocol::Uac1` | `Uac1` or `Uac2` |

### Format (`EspUsbAudioFormat`)

Passed to `addFormat()`, once per stream and format.

| Field | Default | Accepted values |
| --- | --- | --- |
| `sampleRate` | 48000 | 1 or more (Hz) |
| `channels` | 2 | 1 or 2 |
| `bytesPerSample` | 2 | 2, 3 or 4 (bytes per sample; the UAC subslot) |
| `bitsPerSample` | 16 | 16, 24 or 32, and no more than `bytesPerSample × 8` (valid bits) |

- One packet (one interval of PCM plus one frame) must fit the **full-speed limit of 1023 bytes**. The same limit is applied when the link is high speed (a known limitation).
- A "sample frame" ("frame") is one instant of every channel. One frame is `channels × bytesPerSample` bytes.

### `EspUsbAudioConfig`

`configure()` is accepted only while stopped.

| Field | Default | Range and meaning | FS/HS difference |
| --- | --- | --- | --- |
| `eventCapacity` | 8 | Event queue entries. 0 creates no queue | none |
| `playbackBufferFrames` | 256 | Playback ring capacity in frames. 0 creates no ring | none |
| `captureBufferFrames` | 256 | Capture ring capacity in frames. 0 creates no ring | none |
| `playbackBuffer` / `captureBuffer` | `nullptr` | External ring storage. `nullptr` allocates at begin | none |
| `playbackBufferBytes` / `captureBufferBytes` | 0 | Length of the external storage in bytes. Required with external storage | none |
| `playbackOverflowPolicy` | `DropOldest` | Which PCM a full playback ring drops: `DropOldest` or `DropNewest` | none |

- A ring is "largest registered frame size in that direction × bufferFrames" bytes.
- The fields themselves do not depend on speed. Packet size and interval do (see "FS and HS" below).

### FS and HS

ESP32-S3 / S2 connect at full speed (12 Mbps). The ESP32-P4 high-speed controller connects at high speed (480 Mbps).
The descriptor is built for the speed actually negotiated.

| Item | FS | HS |
| --- | --- | --- |
| Data interval | 1 ms (bInterval 1) | 125 µs (bInterval 1) |
| Packet size | `ceil(rate × frame bytes ÷ 1000)` + 1 frame | `ceil(rate × frame bytes ÷ 8000)` + 1 frame |
| Example: 48 kHz stereo 16-bit | 196 bytes | 28 bytes |
| Example: 44.1 kHz stereo 16-bit | 181 bytes | 27 bytes |
| ISO packet limit (USB 2.0 §5.6.3) | 1023 bytes | 1024 bytes (formats are still checked against the FS 1023) |
| UAC2 feedback endpoint | 3 bytes, bInterval 1 (1 ms) | 4 bytes, bInterval 4 (2^(4−1) × 125 µs = 1 ms) |
| Feedback value | 10.14 fixed point: rate × 2^14 ÷ 1000 (samples per 1 ms) | 16.16 fixed point: rate × 2^16 ÷ 8000 (samples per 125 µs) |

- When the rate does not divide evenly into intervals (44.1 kHz, for example), the remainder is spread across intervals.
- A format that full speed cannot carry (192 kHz stereo 32-bit, for example) cannot be registered for high speed either.

## API

### Selecting streams

| Function | Returns / behaviour |
| --- | --- |
| `addPlaybackStream()` / `addCaptureStream()` | The stream reference. While running it returns the existing reference, changes nothing, and sets `device.lastError()` to `ESP_ERR_INVALID_STATE` |
| `hasPlaybackStream()` / `hasCaptureStream()` | `true` once selected |
| `protocol()` | `Uac1` or `Uac2` |

### Registering formats (per stream)

| Function | Returns | Failure (`device.lastError()`) |
| --- | --- | --- |
| `bool addFormat(format)` | `true` on success (`lastError` returns to `ESP_OK`) | Running: `ESP_ERR_INVALID_STATE`. Value out of range, rate 0, or a duplicate format: `ESP_ERR_INVALID_ARG`. Packet over 1023 bytes: `ESP_ERR_INVALID_SIZE`. Storage allocation failed: `ESP_ERR_NO_MEM` |
| `esp_err_t clearFormats()` | `ESP_OK`, also when already empty | Running: `ESP_ERR_INVALID_STATE` (nothing changes) |
| `bool currentFormat(format)` | Copies the shape the host selected and the current rate. `false` when nothing is selected, while stopped or while switching; the argument is left alone | — |

**Rate sets**

- The same PCM shape (same channels, bytesPerSample and bitsPerSample) can be registered several times with different rates. The host picks one of them.
- One Function has one shared clock. **Every shape and both directions must carry the same set of rates.** Registration order does not matter.
- The starting rate is that of the first registered Playback format (or Capture, if there is no Playback).
- A host request for an unregistered rate is refused and the current rate is kept.

**Alternate settings per shape**

- Formats of one PCM shape share one alternate setting. Shapes get 1 to 255 in the order they were first registered. 0 stops the stream.
- Different channel counts in one direction are refused (at `begin()`). The two directions may differ.
- When the host switches alternates, the old DMA is stopped before the endpoint is reopened, and the ring holding PCM of the old shape is discarded. `read()` / `write()` return 0 while switching.
- The application follows the events or `currentFormat()` and adjusts how it handles PCM.

`clearFormats()` removes every format while stopped. The stream selection, the other stream's formats and `EspUsbAudioConfig` stay.
`begin()` fails while a selected stream has no format, so register again before starting.

### Configuring and starting

| Function | Returns / errors |
| --- | --- |
| `esp_err_t configure(config)` | Running: `ESP_ERR_INVALID_STATE`. Only one of pointer and length, external storage with 0 frames, or an unknown policy: `ESP_ERR_INVALID_ARG`. Byte-count overflow: `ESP_ERR_INVALID_SIZE`. On failure the previous settings stay |
| `configuration()` | The settings as given |
| `startError()` | Why the last begin failed |

`device.lastError()` when `device.begin()` fails:

| Value | Cause |
| --- | --- |
| `ESP_ERR_INVALID_ARG` | Settings that disagree: no stream, no format, rate sets that differ between shapes or directions, different channel counts in one direction, external rings of the two directions that overlap |
| `ESP_ERR_INVALID_SIZE` | Not enough room: an external ring shorter than needed, more than 82 rates in one UAC1 list, more than 5461 UAC2 rates, too few endpoints or too little descriptor space, or the IN endpoints of the whole configuration do not fit the controller's transmit FIFO |
| `ESP_ERR_NO_MEM` | Allocation failed |
| `ESP_ERR_INVALID_STATE` | For example, the Function is not registered with this Device |

Check `configure()` failures by its return value. Do not rely on `configure()` updating `device.lastError()`.

### Reading and writing PCM

| Function | Returns |
| --- | --- |
| Playback `size_t read(data, length)` | Bytes read, rounded down to whole frames |
| Playback `int available()` | Unread bytes |
| Capture `size_t write(data, length)` | Bytes placed in the ring. What does not fit is not written |
| Capture `size_t availableForWrite()` | Free bytes, rounded down to whole frames |
| `size_t capacity()` | Ring capacity, rounded down to the current frame size. 0 before begin and after end |
| `size_t packetCapacity()` | Size of the packet DMA area. 0 before begin and after end |
| `bool clearBuffer()` | Discards unread (unsent) PCM |
| `stats()` / `resetStats()` | Read and reset the counters |

- **Capture `write()` is a partial write.** It never overwrites old data. What did not fit is counted in `overrunCount` / `overrunBytes`. Always check the return value.
- If the host asks for capture PCM and the ring runs short, the gap is filled with silence and counted in `underrunCount` / `underrunBytes`.
- Capture PCM can be written while the stream is in alternate 0; it is sent when the stream first starts. Stopping or re-selecting an alternate clears the ring.
- `clearBuffer()` clears the ring only. Packets already handed to USB are not withdrawn.
- With a ring of 0 frames, Playback drops everything it receives (counted as overrun) and Capture sends silence (counted as underrun).
- `stats().transferredBytes` counts bytes moved over USB, not what the application read.
- The counters are `uint32_t`; allow for wrap-around in long runs. They reset to 0 at begin.

### Playback overflow policy

`playbackOverflowPolicy` decides which PCM a full playback ring drops.

| Policy | Behaviour | Suits |
| --- | --- | --- |
| `DropOldest` (default) | Pushes out the oldest unread PCM and keeps the newest whole frames | Live playback: after falling behind, old audio is not played on |
| `DropNewest` | Keeps the unread PCM and drops the part of the new packet that does not fit | Processing where read order matters most |

Example (letters are frames):

| State | Arrives | DropNewest | DropOldest |
| --- | --- | --- | --- |
| Capacity 4, unread A/B/C | D/E/F | A/B/C/D | C/D/E/F |
| Capacity 2, empty | A/B/C | A/B | B/C |

- Each packet that loses data adds 1 to `overrunCount` and the dropped bytes to `overrunBytes`. With DropOldest the pushed-out PCM counts too.
- An incomplete frame at the end of a packet is dropped under both policies. No hidden buffer joins it with the next packet.
- Neither policy guarantees a gap-free recording.

### Volume and mute

| Function | Description |
| --- | --- |
| `hasMute(direction, channel)` / `hasVolume(direction, channel)` | Whether the control exists |
| `getMute(mute, direction, channel)` / `setMute(mute, direction, channel)` | Read / set mute |
| `getVolume(volume, direction, channel)` / `setVolume(volume, direction, channel)` | Volume (`int16_t`, 1/256 dB) |
| `getVolumeRange(range, direction, channel)` | The volume range |
| `currentSampleRate()` | The current sample rate |

- `direction` defaults to Playback and `channel` to 0 (master). 1 and up are the channels (1 = left, 2 = right).
- Volume runs from −90 dB to 0 dB in 1 dB steps (values −23040 to 0, step 256). Values out of range or off the step return `false`.
- A change produces an event, including a change made by the application.

### Events

| Function | Description |
| --- | --- |
| `bool pollEvent(event)` | Takes one event. `false` when empty |
| `size_t pendingEvents()` | Unread events |
| `uint32_t droppedEvents()` | Events dropped because the queue was full |
| `void clearEvents()` | Clears unread events and the drop count |
| `size_t eventCapacity()` | Allocated entries. 0 before begin and after end |

`EspUsbAudioEvent::type` is `SampleRateChanged`, `MuteChanged`, `VolumeChanged` or `StreamStateChanged`.
It also carries `target` (`Function` / `Playback` / `Capture`), `channel`, `alternateSetting`, `sampleRate`, `volumeDb256`, `muted` and `enabled`.
When the queue is full, new events are dropped and `droppedEvents()` increases.

### Calling context

| Operation | Context |
| --- | --- |
| `addFormat`, `clearFormats`, `configure` | An application task, while stopped (before `device.begin()` or after `device.end()`) |
| `read`, `write`, `pollEvent`, volume and mute | An application task. Internally protected by a lock |
| Host requests and DMA | The library's USB worker task. No application callback is called |

- Destroying a running Function stops the Device and frees the storage only after the USB worker and DMA have stopped.
- Not yet verified as safe: `setMute` / `setVolume` racing a host request, and `device.end()` racing `read()` / `write()`.

## Buffers and memory

| Area | Placement | Allocated |
| --- | --- | --- |
| Packet DMA area (one per direction) | DMA-capable internal RAM, 64-byte aligned | At begin, sized for the largest FS packet of every format |
| UAC2 feedback area | DMA-capable internal RAM | At begin (UAC2 with Playback) |
| UAC2 RANGE reply (`2 + 12 × rates` bytes) | Ordinary `malloc` | At begin (UAC2 with two or more rates) |
| Ring (per direction) | Ordinary `malloc` when automatic, or external storage | At begin, or passed in `configure()` |
| Event queue | Ordinary `malloc` | At begin (when `eventCapacity` is 1 or more) |
| Format list (8 bytes each) | Ordinary heap | On each `addFormat()` |

- The packet DMA area is sized for the full-speed maximum packet even on a high-speed link, because the speed is not known at begin.
- One packet area and one ring are allocated, for the largest shape. Adding alternates adds no memory, and switching allocates nothing.
- The ring is only copied by the CPU and needs no DMA capability or alignment. In principle **PSRAM can be used**. A ring in PSRAM has not been verified on hardware.
- An external ring needs "largest frame size × bufferFrames" bytes. For example, stereo 16-bit (4 bytes) plus 32-bit (8 bytes) with 65 frames needs 520 bytes.
- Keep an external ring valid until `device.end()`, and do not write to it from the application meanwhile. If it is too short, begin fails without writing into it.
- If allocation fails, begin fails with `ESP_ERR_NO_MEM` and everything allocated so far is released.

## Host OS

| OS | Built-in driver | Verified with this library |
| --- | --- | --- |
| Linux | `snd-usb-audio` (UAC1 / UAC2) | not yet |
| Windows | `usbaudio.sys` (UAC1), `usbaudio2.sys` (UAC2) | not yet |
| macOS | Built-in audio driver (UAC1 / UAC2) | not yet |

- Operation has been confirmed with ESP32 hosts (two S3 boards at full speed, one P4 in loopback, and P4 to P4X high-speed links). It has not yet been confirmed against a PC operating system.
- The Windows UAC2 driver expects correct answers to the Clock Source sample-rate RANGE request ([Microsoft's description](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/usb-2-0-audio-drivers)). This library answers with the registered rates.
- The OS volume control arrives as events and is not applied to the PCM. Apply gain in the application if you need it.
- A host may remember an old configuration under the same VID/PID ([troubleshooting](../troubleshooting.md)).

## Standards and design rationale

### UAC1 and UAC2

| Item | UAC1 ([Audio 1.0](https://www.usb.org/sites/default/files/audio10.pdf)) | UAC2 (Audio 2.0) |
| --- | --- | --- |
| Playback synchronization (USB 2.0 §5.12.4.1) | Adaptive, no feedback endpoint | Asynchronous + feedback endpoint |
| Capture synchronization | Asynchronous | Asynchronous |
| How rates are advertised | Rate list in the Type I format descriptor (at most 82, because the descriptor length is one byte) | Clock Source CUR / RANGE requests (at most 5461 subranges, because the request length is 16 bits) |
| Changing the rate | SET_CUR to the endpoint (Sampling Frequency Control) | SET_CUR to the Clock Source |
| Volume and mute | Feature Unit (master and each channel) | Feature Unit (master and each channel) |

### Feedback format

- Following USB 2.0 §5.12.4.2, full speed sends 10.14 in 3 bytes and high speed sends 16.16 in 4 bytes.
- The value is a constant computed from the nominal USB clock. It does not follow an external codec clock. When running from a codec clock, absorb the drift in the application by watching the ring level.

### Rationale

- **Capacities are chosen at run time.** v2 had build-flag-sized FIFOs and one global instance. v3 sizes each Function with `configure()` and allocates at begin only what the selected directions and formats need. The queue can be omitted when events are not wanted, sizes change without a rebuild, and a failed start is visible in the return value.
- **The packet area is sized for full speed.** It is allocated before the speed is known, so it is large enough for either speed. The flip side is that formats are checked against the full-speed limit.
- **Capture is a partial write.** Old PCM is never overwritten and the accepted amount is returned, so data is never lost silently.
- **Playback defaults to DropOldest**, so that live playback does not keep playing stale audio after the reader falls behind.
- **Endpoint numbers.** IN and OUT are numbered independently, and opposite directions can share a number. Later Functions' endpoint numbers move accordingly; a host should read addresses from the descriptor.

### Main changes from v2

- The `CFG_TUD_AUDIO` build flags are gone (defining them is a compile error). Formats come from `addFormat()`, capacities from `configure()`.
- Capture `write()` is now a partial write. Code that relied on overwriting must check the return value.
- A full playback ring defaults to `DropOldest`.
- One direction can carry several rates and several PCM shapes.

Not supported: changing the channel count within one direction, different rate sets per shape, feedback that follows an external codec clock, and callback or borrowed-buffer PCM APIs.

## Related examples

| Example | What it shows |
| --- | --- |
| [AudioSpeaker](../../examples/AudioSpeaker/) | UAC1 Playback; reads PCM and prints events and counters |
| [AudioMicrophone](../../examples/AudioMicrophone/) | UAC1 Capture; sends a 440 Hz sine tone to the PC |
| [AudioHeadset](../../examples/AudioHeadset/) | Playback and Capture loopback |
| [AudioPlayback](../../examples/AudioPlayback/) | UAC2 with two PCM shapes × two rates and `currentFormat()` |
| [AudioSpeakerM5](../../examples/AudioSpeakerM5/) | Plays on an M5Stack speaker |
| [AudioMicrophoneM5](../../examples/AudioMicrophoneM5/) | Sends an M5Stack microphone to the PC |
| [AudioHeadsetM5](../../examples/AudioHeadsetM5/) | Headset with an M5Stack speaker and microphone |

Related documents: [USB device development guide](../usb-device-guide.md), [troubleshooting](../troubleshooting.md), [migrating from v2](../migrating-from-v2.md)
