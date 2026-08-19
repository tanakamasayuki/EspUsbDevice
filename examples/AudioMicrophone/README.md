# AudioMicrophone

> 日本語版: [README.ja.md](README.ja.md)

This example exposes the default UAC1 Capture function and streams
device-to-host PCM, so the PC sees a recording/input device. It generates a
440 Hz sine tone as a mono 48 kHz / 16-bit source.

```cpp
EspUsbDevice device;
EspUsbAudioFunction audio(device);
auto &capture = audio.addCaptureStream();
capture.addFormat({48000, 1, 2, 16});
```

The format fields match EspUsbHost terminology:

```text
sampleRate, channels, bytesPerSample, bitsPerSample
```

Call `capture.write()` from `loop()` to push PCM toward the host; it feeds the
bounded USB IN FIFO. Real microphone capture and codec input stay the
application's job - replace the sine generator with your source.
`audio.pollEvent()` reports the capture stream starting and stopping
(`StreamStateChanged`) without executing user code on the TinyUSB task.

Pass `EspUsbAudioProtocol::Uac2` as the second `EspUsbAudioFunction`
constructor argument when UAC2 is required.

Device-to-host streaming is covered by the S3 peer test
`usb_audio_microphone`, which verifies the host receives non-silent PCM
(UAC1 / full speed).
