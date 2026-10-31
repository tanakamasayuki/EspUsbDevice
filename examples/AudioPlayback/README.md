# EspUsbDevice AudioPlayback

> 日本語版: [README.ja.md](README.ja.md)

A USB Audio Class 2.0 (UAC2) speaker that offers two PCM shapes at two sample
rates, and reports which one the host chose.

The formats are registered at run time with `addFormat()`. The host picks the
shape (an alternate setting) and the rate (the Clock Source); the sketch only
reads PCM and asks `currentFormat()` what it is. No rebuild or `build_opt.h` is
involved.

## Hardware

- ESP32-S2 / S3 / P4 board with a native USB OTG port
- A USB host that plays audio (a PC, or another ESP32 running EspUsbHost)
- A Serial monitor connection for the format report
- No codec, I2S or DAC is needed. The sketch discards the PCM after counting it.

## What It Does

- Creates a UAC2 function with one playback stream
- Registers four formats, all stereo:

  | Shape | Rates | Frame | Alternate |
  | --- | --- | --- | --- |
  | 16-bit in 2 bytes | 24000, 16000 Hz | 4 bytes | 1 |
  | 24-bit in 3 bytes | 24000, 16000 Hz | 6 bytes | 2 |

  Both shapes carry the same rate set, because one Function has one shared
  clock. The starting rate is 24000 Hz, the first one registered.
- Configures no event queue (`eventCapacity = 0`) and a 32-frame playback ring
  (32 × 6 bytes = 192 bytes)
- In `loop()`, reads PCM in up to 192-byte chunks and, once per second while
  the host is streaming, prints the current format and the bytes read in that
  second

## Checking From the Host

On Linux with ALSA, find the card with `aplay -l`, then play a test tone in
each format and watch the Serial monitor:

```sh
speaker-test -D hw:1,0 -c 2 -r 24000 -F S16_LE  -t sine   # alternate 1
speaker-test -D hw:1,0 -c 2 -r 16000 -F S24_3LE -t sine   # alternate 2
```

Expected output, one line per second:

```text
24000 Hz, 2 channels, 16 bits in 2 bytes: 96000 bytes read
16000 Hz, 2 channels, 24 bits in 3 bytes: 96000 bytes read
```

The byte count is rate × frame size. It varies slightly from second to second.
Nothing is printed while the host is not streaming (alternate 0).

On Windows and macOS, choose the format in the sound settings of the output
device. Operation with this library has been confirmed with ESP32 hosts. It has
not been confirmed with a PC operating system yet, including the commands above.

## Key APIs

- `EspUsbAudioFunction audio(device, EspUsbAudioProtocol::Uac2)` registers a
  UAC2 function. The default protocol is UAC1.
- `audio.addPlaybackStream()` selects the host-to-device direction and returns
  its stream.
- `playback.addFormat({rate, channels, bytesPerSample, bitsPerSample})`
  registers one format. It returns `false` and sets `device.lastError()`
  (`ESP_ERR_INVALID_ARG` for a bad or duplicate value, `ESP_ERR_INVALID_SIZE`
  when one packet would exceed 1023 bytes).
- `audio.configure(EspUsbAudioConfig)` sets `eventCapacity`,
  `playbackBufferFrames` and the other options. Only while stopped. Check its
  return value.
- `device.begin()` fails with `ESP_ERR_INVALID_ARG` when the settings disagree:
  no format, rate sets that differ between shapes, or different channel counts
  in one direction.
- `playback.read(data, length)` returns whole frames only and never waits.
- `playback.currentFormat(format)` copies the shape and rate in use. It returns
  `false` while the stream is idle or switching.

## Notes

- UAC2 playback is asynchronous with a feedback endpoint (USB 2.0 §5.12.4.1).
  The feedback is 3 bytes in 10.14 format at full speed and 4 bytes in 16.16
  format at high speed (USB 2.0 §5.12.4.2). It is a constant from the nominal
  rate; it does not follow an external codec clock.
- Packet sizes for this example: at full speed 100 / 150 bytes per 1 ms at
  24 kHz; at high speed 16 / 24 bytes per 125 µs.
- The ring holds about 1.3 ms of 24-bit audio at 24 kHz. That is enough for a
  loop that only counts bytes every millisecond. A codec path needs a larger
  ring; watch `playback.stats().overrunCount`. A full ring drops the oldest PCM
  (`DropOldest`, the default).
- When the host switches shape, the ring is cleared and `read()` returns 0
  until the new stream starts.
- Volume and mute from the host are not applied to the PCM. With
  `eventCapacity = 0` they are not queued either; see
  [AudioSpeaker](../AudioSpeaker/) for events.

## See Also

- [AudioSpeaker](../AudioSpeaker/) - UAC1 playback with events and counters
- [AudioHeadset](../AudioHeadset/) - playback and capture in one Function
- [AudioSpeakerM5](../AudioSpeakerM5/) - playback to an M5Stack speaker
- [Audio API](../../docs/classes/audio.md)

## Walkthrough and checks

### Before running

Follow the hardware requirements above and select the board and the example's
profile. [Example workflow](../../docs/example-workflow.md) explains
installation, connectors, USB roles and the difference between startup and a
usable peer.

### Follow the code

`setup()` registers formats, configures and starts, and prints the error code
of the first step that fails. `loop()` never assumes a format: it asks
`currentFormat()` each time it reports.

### API use and adaptation

When you connect a codec, reconfigure it whenever `currentFormat()` changes,
and size the ring for the largest frame you registered. Add a rate to both
shapes or to neither.

### Verify one change at a time

Check each of the four formats first. Then add or remove one format, or change
the ring size, and check again.

## Changes from v2

This example is new in v3. v2 has no corresponding example.

- [Sketch](AudioPlayback.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
