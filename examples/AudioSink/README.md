# EspUsbDevice AudioSink

> 日本語: [README.ja.md](README.ja.md)

Creates a USB Audio speaker device and receives PCM playback data from the host.

This example does not drive an I2S DAC or codec. It is a minimal Audio
implementation check that prints received chunk counts, byte counts, and the
latest samples to the Serial monitor. To play real audio, forward PCM from
`onData()` to the application, PCMFlow, PCMFlowDevice, or another output layer.

## Hardware

- USB-device-capable ESP32-S3 or similar Arduino-ESP32 board
- USB host such as a PC
- Serial monitor connection for logs

## Behavior

- Registers `EspUsbDeviceAudioSink` as a 48 kHz / 16-bit / stereo speaker.
- When the host selects it as an audio output device, the speaker streaming
  interface becomes active.
- Receives host PCM chunks through the `onData()` callback.
- Applies host mute / volume state to the sample buffer with `applyVolume()`.
- Prints received chunks, bytes, and recent samples once per second.

## Key API

- `EspUsbDeviceAudioSink audio(device, 48000, ESP_USB_DEVICE_AUDIO_BITS_16, ESP_USB_DEVICE_AUDIO_SPK_STEREO)`
  registers the USB Audio speaker function.
- `audio.onData(callback)` receives Host -> Device speaker PCM.
- `audio.onEvent(callback)` receives volume, mute, sample-rate, and interface
  enable changes.
- `audio.applyVolume(data, length)` applies Audio-class volume / mute state to
  a PCM buffer.
- `audio.writeMic(data, length)` sends Device -> Host PCM when the microphone
  path is enabled.

## Notes

- This is a minimal speaker sink example. It intentionally does not configure
  I2S pins, DMA buffers, or codecs.
- This library owns the USB Audio class and PCM callback boundary. I2S bridging
  and codec/DAC connections belong in output-side libraries such as
  PCMFlowDevice.
- It is based on Arduino Core's USB Audio implementation, so TinyUSB Audio must
  be enabled for the selected board/core configuration.
- It currently targets standalone Audio devices. Composite Audio with HID, CDC,
  MSC, or other classes is not supported yet.
- Use 48 kHz / 16-bit / stereo as the full-speed baseline.
- Serial monitor output is only for logs. Check the audio stream through the USB
  device port.

## Related

- [PCMFlow](https://github.com/tanakamasayuki/PCMFlow) - PCM data flow
- [PCMFlowDevice](https://github.com/tanakamasayuki/PCMFlowDevice) - PCM output device integration
- [MIDI](../MIDI/) - USB MIDI device
- [Serial](../Serial/) - CDC ACM serial device
