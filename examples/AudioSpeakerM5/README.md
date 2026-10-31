# AudioSpeakerM5

> 日本語版: [README.ja.md](README.ja.md)

This example connects a UAC1 `EspUsbAudioPlaybackStream` to PCMFlowDevice's
`M5SpeakerBufferedPlayer`, so host audio plays on the M5 speaker. The current
USB sample rate is shown on the M5 display.

Select `EspUsbAudioProtocol::Uac2` explicitly when a UAC2 host is available.

- USB format: 48 kHz, stereo, 16-bit PCM
- `audio.configure()` gives the playback ring 480 frames (10 ms)
- `playback.read()` copies PCM in `loop()`
- `M5SpeakerBufferedPlayer::writePcm()` feeds `M5.Speaker`; a chunk it does
  not accept is counted as `unsupported`
- `audio.pollEvent()` reports stream state; the player is stopped when the
  host closes the stream

EspUsbDevice itself does not depend on M5Unified, PCMFlow or PCMFlowDevice.
Those dependencies belong only to this example. USB volume and mute are not
applied automatically; applications may implement gain from the reported
events or from `audio.getVolume()` / `audio.getMute()`.

## Hardware

- An M5Stack ESP32-S3 device with a speaker supported by M5Unified (for
  example CoreS3), connected to the host through its native USB port
- `Serial` (115200) for logs where the board offers a separate route

## Dependencies

`sketch.yaml` pins PCMFlow 0.2.1, PCMFlowDevice 1.0.1, M5Unified 0.2.26 and
M5GFX 0.2.33. Only the `esp32s3` profile is provided.

## Expected output

```
AudioSpeakerM5 ready
speaker 1
chunks=... bytes=192000 speaker=... unsupported=0 usb overruns=0 play_chunks=... waits=... gaps=... drops=...
```

## Walkthrough and checks

### Before running

Install the pinned libraries (the profile does this), select the `esp32s3`
profile, and locate the diagnostic Serial connection.
[Example workflow](../../docs/example-workflow.md) explains installation,
connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

M5Unified starts the speaker, PCMFlowDevice's player is started, and only then
is the USB format registered and `device.begin()` called. Select this USB
output on the PC after the M5 audio initialization succeeds. `bytes=` is what
the sketch read from the USB ring; `speaker=` is what the player queued.

### API use and adaptation

Match the USB PCM format and the hardware player's format explicitly. Keep
the player's buffers alive until the hardware has finished with them. Stream
mute/volume events do not alter the PCM; choose where to apply gain. If the
player drops or waits, compare its counters with `usb overruns` to see which
side fell behind.

### Verify one change at a time

Run the unchanged example first, then change one format or size at a time.
The error printing is not a retry or recovery policy.

## Changes from v2

- The playback ring and event queue are sized with `audio.configure()`; a
  configuration failure is reported instead of starting with build-time sizes.
- The playback ring drops the oldest unread PCM when the sketch falls behind
  (`DropOldest`); USB overruns are added to the per-second log.
- `device.lastErrorName()` is printed on any USB start failure, as before.

Compile-checked with Arduino-ESP32 3.3.12 for the `esp32s3` profile; this
sketch itself has not been run on hardware.

- [Sketch](AudioSpeakerM5.ino)
- [All examples](../README.md)
- [Audio API](../../docs/classes/audio.md)
- [Example workflow](../../docs/example-workflow.md)
