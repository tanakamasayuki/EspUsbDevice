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

## Walkthrough and checks

### Before running

Generate a synthetic tone as USB microphone input. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

`addCaptureStream()` is device-to-host. The loop generates signed PCM samples and `capture.write()` supplies bytes to the stream. Record from this input and inspect the tone; this is not a physical microphone, and requested bytes can exceed what the bounded audio buffer accepts.

### API use and adaptation

USB audio directions are named from the device/host relationship. Confirm direction, sample rate, channels, bytes per sample and valid bits before interpreting PCM. A frame is one sample for every channel; a byte count is not a frame count. Interleaved stereo alternates channels rather than sending two independent byte streams.

Streaming must be enabled by the host. Track stream state and underflow/overflow statistics separately from successful USB enumeration. Buffers and queues are finite, and an example that ignores a write's returned count does not promise lossless audio. Keep USB callbacks short; perform decoding, storage and expensive analysis elsewhere. Start with a known test signal before adding a codec or external hardware.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](AudioMicrophone.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
