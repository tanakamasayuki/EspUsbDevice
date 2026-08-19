# AudioMicrophoneM5

> 日本語版: [README.ja.md](README.ja.md)

This example streams the M5 built-in microphone (`M5.Mic`) to the host as a
USB Audio recording device (device -> host).

- USB format: 16 kHz, mono, 16-bit PCM - M5 PDM microphones run comfortably at
  this rate.
- `M5.Mic.record()` is asynchronous and double-buffered, so the sketch keeps a
  small ring of blocks and sends the oldest completed block with
  `capture.write()`.
- `audio.pollEvent()` reports the capture stream state; the current USB sample
  rate is shown on the M5 display.

EspUsbDevice itself does not depend on M5Unified. That dependency belongs only
to this example, which uses it for microphone capture.
