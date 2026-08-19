# AudioHeadsetM5

> 日本語版: [README.ja.md](README.ja.md)

This example is a USB Audio headset backed by M5 hardware: host audio plays on
the M5 speaker (host -> device) while the M5 microphone streams back to the
host (device -> host).

- USB format: 48 kHz / 16-bit, stereo speaker and mono microphone. Speaker and
  microphone share the single USB sample rate.
- Received PCM goes to `M5.Speaker` through PCMFlowDevice's
  `M5SpeakerBufferedPlayer::writePcm()`.
- `M5.Mic.record()` capture uses a small ring of blocks; the oldest completed
  block is pushed with `capture.write()`.

**Known limitation:** running the M5 speaker and microphone at the same time is
not reliably supported. M5Unified drives both through one I2S port installed
TX-only or RX-only, and each `begin()` uninstalls the other's driver, so
full-duplex playback + capture glitches (confirmed on CoreS3 as well; forcing
the speaker onto a second I2S port did not help). Treat this example as a
best-effort demo. For reliable audio use the single-direction examples
`AudioSpeakerM5` (playback) and `AudioMicrophoneM5` (capture); the non-M5
`AudioHeadset` shows both directions over USB without touching M5 audio
hardware.

EspUsbDevice itself does not depend on M5Unified, PCMFlow, or PCMFlowDevice.
Those dependencies belong only to this example.
