# AudioSpeakerM5

> 日本語版: [README.ja.md](README.ja.md)

This example connects the default UAC1 `EspUsbAudioPlaybackStream` to
PCMFlowDevice's `M5SpeakerBufferedPlayer`.

Select `EspUsbAudioProtocol::Uac2` explicitly when a UAC2 host is available.

- USB format: 48 kHz, stereo, 16-bit PCM
- `playback.read()` copies PCM in `loop()`
- `M5SpeakerBufferedPlayer::writePcm()` feeds `M5.Speaker`
- `audio.pollEvent()` reports stream state

EspUsbDevice itself does not depend on M5Unified, PCMFlow, or PCMFlowDevice.
Those dependencies belong only to this example. USB volume and mute are not
applied automatically; applications may implement DSP from the reported
events.

## Walkthrough and checks

### Before running

Play received USB PCM through an M5 speaker. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

M5Unified, PCMFlow and PCMFlowDevice connect `playback.read()` to M5SpeakerBufferedPlayer. Select this USB output on the PC after the M5 audio initialization succeeds. Stream mute/volume events do not automatically alter the PCM; choose where to apply gain explicitly.

### API use and adaptation

Install the example's M5Unified, PCMFlow and PCMFlowDevice dependencies and select a board whose speaker/microphone is supported by that configuration. These dependencies belong to the example, not to every use of EspUsbDevice. Keep the asynchronous mic/speaker buffers alive until the corresponding hardware operation has finished; a local temporary array is not a safe replacement for the ring.

Match the USB PCM format and the hardware player's format explicitly. Test playback and capture separately, then together, and compare accepted bytes with the source block size. The stream-state events tell you when the host is using audio. Receiving USB volume/mute events does not automatically implement the intended gain on M5 hardware.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](AudioSpeakerM5.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
