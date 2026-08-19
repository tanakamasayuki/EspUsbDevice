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
