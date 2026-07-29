# AudioSpeakerM5

This example connects a UAC2 `EspUsbAudioPlaybackStream` to
PCMFlowDevice's `M5SpeakerBufferedPlayer`.

- USB format: 48 kHz, stereo, 16-bit PCM
- `playback.read()` copies PCM in `loop()`
- `M5SpeakerBufferedPlayer::writePcm()` feeds `M5.Speaker`
- `audio.pollEvent()` reports stream state

EspUsbDevice itself does not depend on M5Unified, PCMFlow, or PCMFlowDevice.
Those dependencies belong only to this example. USB volume and mute are not
applied automatically; applications may implement DSP from the reported
events.
