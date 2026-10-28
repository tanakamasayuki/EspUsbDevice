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

## Walkthrough and checks

### Before running

Connect USB playback/capture to M5 speaker and microphone hardware. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Verify playback and microphone separately before using both. PCMFlowDevice supplies speaker buffering while completed microphone blocks feed `capture.write()`. Playback channel layout differs from capture in this sketch; interpret each stream using its own `addFormat()` declaration.

### API use and adaptation

Install the example's M5Unified, PCMFlow and PCMFlowDevice dependencies and select a board whose speaker/microphone is supported by that configuration. These dependencies belong to the example, not to every use of EspUsbDevice. Keep the asynchronous mic/speaker buffers alive until the corresponding hardware operation has finished; a local temporary array is not a safe replacement for the ring.

Match the USB PCM format and the hardware player's format explicitly. Test playback and capture separately, then together, and compare accepted bytes with the source block size. The stream-state events tell you when the host is using audio. Receiving USB volume/mute events does not automatically implement the intended gain on M5 hardware.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](AudioHeadsetM5.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
