# AudioMicrophone

> 日本語版: [README.ja.md](README.ja.md)

This example exposes a UAC1 Capture function and streams device-to-host PCM,
so the PC sees a recording (input) device. It generates a 440 Hz sine tone as
a mono 48 kHz / 16-bit source.

```cpp
EspUsbDevice device;
EspUsbAudioFunction audio(device);
auto &capture = audio.addCaptureStream();

capture.addFormat({48000, 1, 2, 16});
EspUsbAudioConfig config;
config.captureBufferFrames = 480;   // 10 ms of 48 kHz mono 16-bit
audio.configure(config);
device.begin(identity);
```

The format fields match EspUsbHost terminology:

```text
sampleRate, channels, bytesPerSample, bitsPerSample
```

`capture.write()` copies PCM into a ring that the USB worker drains toward
the host. It takes only what fits and returns that byte count; it never
overwrites queued PCM. The sketch therefore generates the next 1 ms block
only when `capture.availableForWrite()` can take all of it, so no generated
sample is lost and the tone stays continuous. When the ring runs empty the
host receives silence, counted in `stats().underrunCount` / `underrunBytes`.

Real microphone capture and codec input stay the application's job: replace
`fillBlock()` with your source. `audio.pollEvent()` reports the capture stream
starting and stopping (`StreamStateChanged`) on the sketch task.

Pass `EspUsbAudioProtocol::Uac2` as the second `EspUsbAudioFunction`
constructor argument when UAC2 is required.

## Hardware

- ESP32-S3, ESP32-S2 or ESP32-P4 board with its native USB OTG port connected
  to the host
- A separate debug connection for `Serial` (115200)

## Expected output

```
Microphone ready: UAC1, 48000 Hz, 16-bit, mono
capture enabled=1 alt=1
written=96000 usb=96000 underruns=0/0 bytes
```

Record from the new input device and a steady 440 Hz tone is heard.

## Walkthrough and checks

### Before running

Select the board's profile (`esp32s3` is the default; `esp32s2`, `esp32p4`
for P4 v1.x and `esp32p4x` for P4 v3.1 or newer; v3.0 is not supported), and locate the
diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md)
explains installation, connectors, USB roles and the difference between
startup and a usable peer.

### Follow the code

`addCaptureStream()` is device-to-host. `loop()` fills signed PCM blocks while
the ring has room, and the USB worker sends them as the host asks. PCM written
before the host starts recording is kept for the first stream start; stopping
the stream clears the ring.

### API use and adaptation

Confirm direction, sample rate, channels, bytes per sample and valid bits
before producing PCM. A frame is one sample for every channel; `write()`
accepts whole frames only. Always use the returned count: a source that cannot
wait (a microphone) must decide what to do with the bytes that did not fit,
as [AudioMicrophoneM5](../AudioMicrophoneM5/) does.

### Verify one change at a time

Run the unchanged example first and compare its log with what the recording
application shows. Change one format or size at a time. The error printing is
not a retry or recovery policy.

## Changes from v2

- The capture ring is sized with `audio.configure()` instead of build flags.
- `write()` is a partial write: it no longer overwrites old PCM, and returns
  how much it took. The v2 sketch ignored the result; this one generates only
  what fits.
- Written bytes, USB bytes and underruns are printed once a second.

Compile-checked with Arduino-ESP32 3.3.12 for every profile in
`sketch.yaml`; this sketch itself has not been run on hardware.

- [Sketch](AudioMicrophone.ino)
- [All examples](../README.md)
- [Audio API](../../docs/classes/audio.md)
- [Example workflow](../../docs/example-workflow.md)
