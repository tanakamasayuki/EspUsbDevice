# AudioSpeaker

> 日本語版: [README.ja.md](README.ja.md)

This example exposes a UAC1 Playback function and reads host-to-device PCM
from a bounded ring. The PC sees an output (speaker) device. Pass
`EspUsbAudioProtocol::Uac2` as the second `EspUsbAudioFunction` constructor
argument when UAC2 is required.

```cpp
EspUsbDevice device;
EspUsbAudioFunction audio(device);
auto &playback = audio.addPlaybackStream();

playback.addFormat({48000, 2, 2, 16});   // false: see device.lastError()
EspUsbAudioConfig config;
config.eventCapacity = 16;
config.playbackBufferFrames = 480;       // 10 ms of 48 kHz stereo 16-bit
audio.configure(config);                 // ESP_OK or the reason
device.begin(identity);
```

The format fields match EspUsbHost terminology:

```text
sampleRate, channels, bytesPerSample, bitsPerSample
```

Call `playback.read()` from `loop()` and forward the copied PCM to I2S, a
codec, PCMFlow or another application layer. The ring holds
`playbackBufferFrames` frames; when the sketch reads too slowly the default
`DropOldest` policy discards the oldest unread PCM and counts it in
`stats().overrunCount` / `overrunBytes`.

`audio.pollEvent()` reports stream, sample-rate, mute and volume changes on
the sketch task; the USB worker never runs sketch code for them. The library
does not apply volume or mute to the PCM.

The feature-unit state is also available directly:

```cpp
bool leftMuted;
int16_t rightVolume;
audio.getMute(leftMuted, EspUsbAudioDirection::Playback, 1);
audio.getVolume(rightVolume, EspUsbAudioDirection::Playback, 2);
```

Channels are `0` Master, `1` Left and `2` Right. Volume uses 1/256 dB units
(-90 dB to 0 dB in 1 dB steps). The sketch prints the master values once a
second together with the byte counts.

## Hardware

- ESP32-S3, ESP32-S2 or ESP32-P4 board with its native USB OTG port connected
  to the host
- A separate debug connection for `Serial` (115200)

No DAC, I2S or speaker is driven: silence from the board is expected.

## Expected output

```
Speaker ready: UAC1, 48000 Hz, 16-bit, stereo
stream enabled=1 alt=1
read=192000 usb=192000 overruns=0/0 bytes mute=0 volume=0/256 dB dropped events=0
```

## Walkthrough and checks

### Before running

Select the board's profile (`esp32s3` is the default; `esp32s2`, `esp32p4`
for P4 v1.x and `esp32p4x` for P4 v3.1 or newer; v3.0 is not supported), and locate the
diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md)
explains installation, connectors, USB roles and the difference between
startup and a usable peer.

### Follow the code

The format and ring size are set before `device.begin()`. Select the device as
the PC output and follow the `read=` byte count and the events. `usb=` is what
arrived over USB; `read=` is what the sketch took from the ring.

### API use and adaptation

Confirm direction, sample rate, channels, bytes per sample and valid bits
before interpreting PCM. A frame is one sample for every channel; `read()`
returns bytes, rounded down to whole frames. Streaming starts only when the
host selects the alternate setting, so check stream state separately from
enumeration. More formats and rates can be added with further `addFormat()`
calls; see [AudioPlayback](../AudioPlayback/AudioPlayback.ino).

### Verify one change at a time

Run the unchanged example first and compare its log with the host's audio
settings. Change one format or size at a time. The error printing is not a
retry or recovery policy.

## Changes from v2

- The ring and the event queue are sized with `audio.configure()` instead of
  build flags; a failure is returned by `configure()` or `device.begin()`.
- When the playback ring is full, the oldest unread PCM is dropped by default
  (`DropOldest`). Set
  `config.playbackOverflowPolicy = EspUsbAudioOverflowPolicy::DropNewest` to
  keep the unread PCM and drop the new arrivals instead.
- `SampleRateChanged` events and `overrunBytes` are printed as well.

Compile-checked with Arduino-ESP32 3.3.12 for every profile in
`sketch.yaml`; this sketch itself has not been run on hardware.

- [Sketch](AudioSpeaker.ino)
- [All examples](../README.md)
- [Audio API](../../docs/classes/audio.md)
- [Example workflow](../../docs/example-workflow.md)
