# EspUsbDevice AudioSinkM5Speaker

> 日本語: [README.ja.md](README.ja.md)

Receives PCM from a PC or another host as a USB Audio speaker and plays it
through an M5Stack-family board's built-in speaker.

This example integrates EspUsbDevice, PCMFlow, PCMFlowDevice, and M5Unified.
The EspUsbDevice library itself does not depend on PCMFlow or PCMFlowDevice.
`EspUsbDeviceAudioSink` receives USB Audio, and PCMFlowDevice's
`M5SpeakerBufferedPlayer` owns the short-lived buffers needed for stable speaker
playback.

## Hardware

- USB-device-capable ESP32-S3 M5Stack-family board
- USB host such as a PC
- Board where M5Unified exposes `M5.Speaker`
- Serial monitor connection for logs

## Behavior

- Appears to the PC as a 48 kHz / 16-bit / stereo USB Audio speaker.
- Receives Host -> Device PCM chunks through `audio.onPcm()`.
- Applies host mute / volume state to the PCM buffer with `audio.applyVolume()`.
- Passes PCM plus format metadata to PCMFlowDevice's
  `M5SpeakerBufferedPlayer::writePcm()`.
- `writePcm()` downmixes stereo 16-bit PCM to mono 16-bit PCM and plays it
  through the built-in M5 speaker.
- Prints received chunks, playback chunks, waits, gaps, and drops once per
  second.

## Data Flow

```text
PC / Host
  -> USB Audio speaker stream
  -> EspUsbDeviceAudioSink::onPcm()
  -> applyVolume()
  -> M5SpeakerBufferedPlayer::writePcm()
  -> M5.Speaker
```

## Key API

- `EspUsbDeviceAudioSink` registers the USB Audio speaker function.
- `audio.onPcm(callback)` receives the PCM buffer plus `sampleRate`,
  `channels`, and `bytesPerSample`.
- `M5SpeakerBufferedPlayer::writePcm(data, bytes, format)` accepts 16-bit mono
  or stereo PCM and downmixes stereo input to mono.
- `M5SpeakerBufferedPlayer` also keeps short-lived buffers for
  `M5.Speaker.playRaw()`.

## Notes

- This example needs `PCMFlow`, `PCMFlowDevice`, `M5Unified`, and `M5GFX`.
- `M5SpeakerBufferedPlayer::writePcm()` accepts same-rate 16-bit mono or stereo
  PCM. If resampling or bit-depth conversion is needed, insert a PCMFlow
  pipeline separately.
- Do not keep the USB callback PCM buffer after the callback returns. Copy it in
  the application if it must outlive the callback.
- The full-speed USB Audio baseline is 48 kHz / 16-bit / stereo.
- If playback underruns on hardware, tune the PCMFlowDevice profile, chunk size,
  or buffer count.

## Related

- [AudioSink](../AudioSink/) - minimal USB Audio speaker sink
- [PCMFlow](https://github.com/tanakamasayuki/PCMFlow) - PCM data flow
- [PCMFlowDevice](https://github.com/tanakamasayuki/PCMFlowDevice) - PCM output device integration
