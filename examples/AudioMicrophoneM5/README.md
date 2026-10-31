# AudioMicrophoneM5

> 日本語版: [README.ja.md](README.ja.md)

This example streams the M5 built-in microphone (`M5.Mic`) to the host as a
USB Audio recording device (device -> host). The current USB sample rate is
shown on the M5 display.

- USB format: 16 kHz, mono, 16-bit PCM. M5 PDM microphones run comfortably at
  this rate.
- `M5.Mic.record()` is asynchronous and double-buffered, so the sketch keeps a
  ring of three 15 ms blocks and sends the oldest completed block with
  `capture.write()`.
- `audio.configure()` gives the capture ring two blocks (480 frames).
- `capture.write()` takes only what fits. A microphone cannot wait, so the
  bytes it does not take are dropped and counted as `rejected`.
- `audio.pollEvent()` reports the capture stream state; blocks are sent only
  while the host records.

EspUsbDevice itself does not depend on M5Unified. That dependency belongs only
to this example, which uses it for microphone capture.

## Hardware

- An M5Stack ESP32-S3 device with a microphone supported by M5Unified (for
  example CoreS3), connected to the host through its native USB port
- `Serial` (115200) for logs where the board offers a separate route

## Dependencies

`sketch.yaml` pins M5Unified 0.2.26 and M5GFX 0.2.33. Only the `esp32s3`
profile is provided.

## Expected output

```
AudioMicrophoneM5 ready
mic 1
mic enabled=1 sent=32000 rejected=0 underruns=0
```

## Walkthrough and checks

### Before running

Install the pinned libraries (the profile does this), select the `esp32s3`
profile, and locate the diagnostic Serial connection.
[Example workflow](../../docs/example-workflow.md) explains installation,
connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Follow `M5.Mic.record()` and the ring index before `capture.write()`. A
previously completed block is transmitted so the asynchronous recorder can
own the current block. Select the USB input in a recording application.

### API use and adaptation

Keep the asynchronous microphone blocks alive until the recorder has
finished with them; a local temporary array is not a safe replacement for the
ring. Compare `sent` with the source rate (32000 bytes/s here); a steady
`rejected` count means the host is reading slower than the microphone
produces, and `underruns` means it is reading faster.

### Verify one change at a time

Run the unchanged example first, then change one format or size at a time.
The error printing is not a retry or recovery policy.

## Changes from v2

- The capture ring is sized with `audio.configure()`.
- v3's `write()` never overwrites queued PCM and returns what it took; the
  bytes not taken are now counted (`rejected`) instead of being ignored.
- Capture underruns from `capture.stats()` are added to the log.

Compile-checked with Arduino-ESP32 3.3.12 for the `esp32s3` profile; this
sketch itself has not been run on hardware.

- [Sketch](AudioMicrophoneM5.ino)
- [All examples](../README.md)
- [Audio API](../../docs/classes/audio.md)
- [Example workflow](../../docs/example-workflow.md)
