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
```

The host sees one playback and one recording device. Both directions share the
mono 48 kHz / 16-bit format - a USB Audio device in this stack uses one sample
rate for both directions. `audio.pollEvent()` reports each stream's state
separately (`event.target` is `Playback` or `Capture`).

Pass `EspUsbAudioProtocol::Uac2` as the second `EspUsbAudioFunction`
constructor argument when UAC2 is required.

Both directions at once are covered by the S3 peer test `usb_audio_headset`:
an OUT and an IN stream enumerate and start, the host's speaker PCM reaches
the device, and the device's mic PCM reaches the host and is non-silent
(UAC1 / full speed).

## Walkthrough and checks

### Before running

Loop received playback PCM back into a USB capture stream. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Playback and capture formats are configured before `device.begin()`. Select both functions in the PC audio application: received `playback.read()` bytes are passed to `capture.write()`. Both directions need host activity, and this simple echo does not retain every byte rejected by `capture.write()`.

### API use and adaptation

USB audio directions are named from the device/host relationship. Confirm direction, sample rate, channels, bytes per sample and valid bits before interpreting PCM. A frame is one sample for every channel; a byte count is not a frame count. Interleaved stereo alternates channels rather than sending two independent byte streams.

Streaming must be enabled by the host. Track stream state and underflow/overflow statistics separately from successful USB enumeration. Buffers and queues are finite, and an example that ignores a write's returned count does not promise lossless audio. Keep USB callbacks short; perform decoding, storage and expensive analysis elsewhere. Start with a known test signal before adding a codec or external hardware.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](AudioHeadset.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
