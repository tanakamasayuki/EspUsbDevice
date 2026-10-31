# AudioHeadsetM5

> 日本語版: [README.ja.md](README.ja.md)

This example is a USB Audio headset backed by M5 hardware: host audio plays on
the M5 speaker (host -> device) while the M5 microphone streams back to the
host (device -> host). The shared USB sample rate is shown on the M5 display.

- USB format: 48 kHz / 16-bit, stereo speaker and mono microphone. Speaker and
  microphone share the single USB sample rate, so both register 48 kHz.
- `audio.configure()` gives each direction a 10 ms ring.
- Received PCM goes to `M5.Speaker` through PCMFlowDevice's
  `M5SpeakerBufferedPlayer::writePcm()`.
- `M5.Mic.record()` capture uses a ring of three 10 ms blocks; the oldest
  completed block is pushed with `capture.write()`. Bytes that `write()` does
  not take are counted as `rejected`.

**Known limitation:** running the M5 speaker and microphone at the same time is
not reliably supported. M5Unified drives both through one I2S port installed
TX-only or RX-only, and each `begin()` uninstalls the other's driver, so
full-duplex playback + capture glitches (confirmed on CoreS3 as well; forcing
the speaker onto a second I2S port did not help). This is an M5Unified
property and does not change with EspUsbDevice v3. Treat this example as a
best-effort demo. For reliable audio use the single-direction examples
[AudioSpeakerM5](../AudioSpeakerM5/) (playback) and
[AudioMicrophoneM5](../AudioMicrophoneM5/) (capture); the non-M5
[AudioHeadset](../AudioHeadset/) shows both directions over USB without
touching M5 audio hardware.

EspUsbDevice itself does not depend on M5Unified, PCMFlow or PCMFlowDevice.
Those dependencies belong only to this example.

## Hardware

- An M5Stack ESP32-S3 device with a speaker and microphone supported by
  M5Unified (for example CoreS3), connected to the host through its native
  USB port
- `Serial` (115200) for logs where the board offers a separate route

## Dependencies

`sketch.yaml` pins PCMFlow 0.2.1, PCMFlowDevice 1.0.1, M5Unified 0.2.26 and
M5GFX 0.2.33. Only the `esp32s3` profile is provided.

## Walkthrough and checks

### Before running

Install the pinned libraries (the profile does this), select the `esp32s3`
profile, and locate the diagnostic Serial connection.
[Example workflow](../../docs/example-workflow.md) explains installation,
connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Verify playback and microphone separately before using both. PCMFlowDevice
supplies speaker buffering while completed microphone blocks feed
`capture.write()`. The playback channel layout differs from capture in this
sketch; interpret each stream using its own `addFormat()` declaration.

### API use and adaptation

Keep the asynchronous mic/speaker buffers alive until the corresponding
hardware operation has finished; a local temporary array is not a safe
replacement for the ring. Match the USB PCM format and the hardware player's
format explicitly. The stream-state events tell you when the host is using
each direction. Receiving USB volume/mute events does not implement gain on
the M5 hardware.

### Verify one change at a time

Run the unchanged example first, then change one format or size at a time.
The error printing is not a retry or recovery policy.

## Changes from v2

- Ring and event capacities are set with `audio.configure()`.
- v3's `write()` never overwrites queued PCM and returns what it took; the
  rest of each microphone block is counted as `rejected`.
- Speaker, sent and rejected byte counts are printed once a second.

Compile-checked with Arduino-ESP32 3.3.12 for the `esp32s3` profile; this
sketch itself has not been run on hardware.

- [Sketch](AudioHeadsetM5.ino)
- [All examples](../README.md)
- [Audio API](../../docs/classes/audio.md)
- [Example workflow](../../docs/example-workflow.md)
