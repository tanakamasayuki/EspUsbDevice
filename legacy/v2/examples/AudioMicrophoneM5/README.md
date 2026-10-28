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

## Walkthrough and checks

### Before running

Send M5 microphone PCM as a USB capture stream. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Follow `M5.Mic.record()` and the ring index before `capture.write()`. A previously completed block is transmitted so the asynchronous recorder can own the current block. Select the USB input in a recording application; do not reuse the active recording buffer prematurely.

### API use and adaptation

Install the example's M5Unified, PCMFlow and PCMFlowDevice dependencies and select a board whose speaker/microphone is supported by that configuration. These dependencies belong to the example, not to every use of EspUsbDevice. Keep the asynchronous mic/speaker buffers alive until the corresponding hardware operation has finished; a local temporary array is not a safe replacement for the ring.

Match the USB PCM format and the hardware player's format explicitly. Test playback and capture separately, then together, and compare accepted bytes with the source block size. The stream-state events tell you when the host is using audio. Receiving USB volume/mute events does not automatically implement the intended gain on M5 hardware.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](AudioMicrophoneM5.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
