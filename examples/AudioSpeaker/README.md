# AudioSpeaker

This example exposes a UAC2 Playback function and reads host-to-device PCM from
a bounded FIFO.

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

Detailed streaming validation is deferred until EspUsbHost supports UAC2.
