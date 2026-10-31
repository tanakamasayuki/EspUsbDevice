# AudioHeadset

> 日本語版: [README.ja.md](README.ja.md)

This example is one USB Audio device that is a speaker (host -> device) and a
microphone (device -> host) at the same time: a loopback headset. PCM read
with `playback.read()` is echoed back to the host with `capture.write()`, so
whatever the PC plays comes back on its recording input.

```cpp
EspUsbDevice device;
EspUsbAudioFunction audio(device);
auto &playback = audio.addPlaybackStream();
auto &capture = audio.addCaptureStream();

playback.addFormat({48000, 1, 2, 16});
capture.addFormat({48000, 1, 2, 16});
EspUsbAudioConfig config;
config.playbackBufferFrames = 480;   // 10 ms each way
config.captureBufferFrames = 480;
audio.configure(config);
device.begin(identity);
```

The host sees one playback and one recording device. Both directions use mono
48 kHz / 16-bit. One `EspUsbAudioFunction` has one clock, so both directions
must register the same set of sample rates; `device.begin()` rejects a
mismatch with `ESP_ERR_INVALID_ARG`. `audio.pollEvent()` reports each stream's
state separately (`event.target` is `Playback` or `Capture`).

The loop reads only as much playback PCM as `capture.availableForWrite()` can
take, so every byte read is echoed. If the host is not recording, the capture
ring fills, playback is no longer read, and the playback ring drops its oldest
PCM (`DropOldest`, counted in `overrunCount`).

Pass `EspUsbAudioProtocol::Uac2` as the second `EspUsbAudioFunction`
constructor argument when UAC2 is required.

## Hardware

- ESP32-S3, ESP32-S2 or ESP32-P4 board with its native USB OTG port connected
  to the host
- A separate debug connection for `Serial` (115200)

## Expected output

```
Headset ready: UAC1, 48000 Hz, 16-bit, mono
playback enabled=1 alt=1
capture enabled=1 alt=1
echoed=96000 playback overruns=0 capture underruns=0
```

## Walkthrough and checks

### Before running

Select the board's profile (`esp32s3` is the default; `esp32s2`, `esp32p4`
for P4 v1.x and `esp32p4x` for P4 v3.1 or newer; v3.0 is not supported), and locate the
diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md)
explains installation, connectors, USB roles and the difference between
startup and a usable peer.

### Follow the code

Both formats and both rings are set before `device.begin()`. Select the device
as both output and input in the PC audio application; play something and
record at the same time. Both directions need host activity.

### API use and adaptation

USB audio directions are named from the device/host relationship. Confirm
direction, sample rate, channels, bytes per sample and valid bits before
interpreting PCM. Streaming must be enabled by the host for each direction;
track stream state and overrun/underrun statistics separately from successful
enumeration. Keep USB work out of callbacks and do decoding or analysis in the
sketch task.

### Verify one change at a time

Run the unchanged example first, then change one format or size at a time.
The error printing is not a retry or recovery policy.

## Changes from v2

- Ring and event capacities are set with `audio.configure()`.
- v2 passed every byte read to `capture.write()` and ignored its result. v3's
  `write()` is partial, so this sketch reads only what capture can take and
  nothing read is lost.
- Echoed bytes, playback overruns and capture underruns are printed once a
  second.

Compile-checked with Arduino-ESP32 3.3.12 for every profile in
`sketch.yaml`; this sketch itself has not been run on hardware.

- [Sketch](AudioHeadset.ino)
- [All examples](../README.md)
- [Audio API](../../docs/classes/audio.md)
- [Example workflow](../../docs/example-workflow.md)
