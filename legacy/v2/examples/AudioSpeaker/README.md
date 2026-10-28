# AudioSpeaker

> 日本語版: [README.ja.md](README.ja.md)

This example exposes the default UAC1 Playback function and reads
host-to-device PCM from a bounded FIFO. Pass `EspUsbAudioProtocol::Uac2` as the
second `EspUsbAudioFunction` constructor argument when UAC2 is required.

```cpp
EspUsbDevice device;
EspUsbAudioFunction audio(device);
auto &playback = audio.addPlaybackStream();
playback.addFormat({48000, 2, 2, 16});
```

The format fields match EspUsbHost terminology:

```text
sampleRate, channels, bytesPerSample, bitsPerSample
```

Call `playback.read()` from `loop()` and forward the copied PCM to I2S, a codec,
PCMFlow, or another application layer. `audio.pollEvent()` reports stream,
mute, volume, and sample-rate changes without executing user code on the
TinyUSB task. The library does not apply volume or mute DSP to PCM implicitly.

The feature-unit state is also available directly:

```cpp
bool leftMuted;
int16_t rightVolume;
audio.getMute(leftMuted, EspUsbAudioDirection::Playback, 1);
audio.getVolume(rightVolume, EspUsbAudioDirection::Playback, 2);
```

Channels are `0` Master, `1` Left, and `2` Right. Volume uses 1/256 dB units.

UAC1 enumeration, streaming, mute, volume, and rapid control changes are
covered by the S3 peer tests. UAC2 descriptors and class requests are tested,
but UAC2 streaming is deferred until EspUsbHost has matching UAC2 support.

## Walkthrough and checks

### Before running

Receive playback PCM without driving a physical speaker. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

`addPlaybackStream()` describes host-to-device audio. Select it as the PC output and follow `playback.read()` byte counts and `pollEvent()` state. This example consumes PCM for inspection; silence from the board is expected because no DAC, I2S or speaker driver is connected.

### API use and adaptation

USB audio directions are named from the device/host relationship. Confirm direction, sample rate, channels, bytes per sample and valid bits before interpreting PCM. A frame is one sample for every channel; a byte count is not a frame count. Interleaved stereo alternates channels rather than sending two independent byte streams.

Streaming must be enabled by the host. Track stream state and underflow/overflow statistics separately from successful USB enumeration. Buffers and queues are finite, and an example that ignores a write's returned count does not promise lossless audio. Keep USB callbacks short; perform decoding, storage and expensive analysis elsewhere. Start with a known test signal before adding a codec or external hardware.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](AudioSpeaker.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
