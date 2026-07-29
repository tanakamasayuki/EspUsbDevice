# AudioSpeaker

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
