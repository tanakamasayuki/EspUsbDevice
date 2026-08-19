# Peer Tests

> 日本語版: [README.ja.md](README.ja.md)

`tests/peer` contains two-board automated tests.

- Host board: ESP32-S3 running EspUsbHost.
- Device board: ESP32-S3 running EspUsbDevice.

The device sketch is controlled by serial commands. This keeps the USB behavior
deterministic and makes host-side assertions stable.

## Hardware

Connect the USB data pins between the host and device boards:

| Host board | Device board |
|------------|--------------|
| GPIO19 (D-) | GPIO19 (D-) |
| GPIO20 (D+) | GPIO20 (D+) |
| GND | GND |

Avoid tying VBUS together when both boards are powered separately from the host
PC.

## Run Notes

After an `EspUsbHost` release upgrade or after switching between `s3_peer_host`
and `s3_peer_local`, stale build cache can look like boot-time crashes or
timeouts. Use `--clean` for release validation.

```sh
uv run --env-file .env pytest peer/ --profile=s3_peer_host --clean
```

## Initial Tests

- `hid_keyboard`: raw boot keyboard report and LED output report. Passing on the two-board S3 setup.
- `hid_mouse`: raw boot mouse report. Move, wheel, left, right, middle, back,
  and forward pass through the mouse callback on the two-board S3 setup.
- `hid_keyboard_mouse`: composite keyboard + mouse device. Passing on the
  two-board S3 setup.
- `hid_keyboard_nkro`: NKRO keyboard driven against an EspUsbHost host.
  Different angle from the EspUsbHost-repo copy: it checks the exact set of held
  keycodes (identity, not just the count) and that high-usage International /
  LANG (JIS) keys at 0x87-0x91 all arrive, proving the bitmap spans the full
  0x00-0xDF range.
- `custom_hid`: fixed custom report descriptor and raw input.
- `hid_vendor`: interrupt IN/OUT and feature report.
- `usb_serial`: CDC ACM serial. Device -> Host, Host -> Device, and line
  coding callbacks pass on the two-board S3 setup.
- `usb_midi`: USB MIDI. Channel voice messages and short Host -> Device SysEx
  packet splitting pass on the two-board S3 setup.
- `usb_msc`: USB Mass Storage. Single-LUN RAM disk capacity, inquiry, read,
  write, and error paths pass on the two-board S3 setup.
- `usb_vendor`: vendor-specific interface. Interface / bulk endpoint
  enumeration, bulk echo, application vendor control IN/OUT, and WebUSB landing
  URL reads pass on the two-board S3 setup.
- `usb_ccid`: USB CCID smart card reader (device) driven by an EspUsbHost host
  (DUT). Covers the class descriptor the host parses (one slot, T=1, short APDU
  exchange level), the three ICC states as the sketch drives the slot, ATR
  delivery on activation, APDU exchanges including a card-level 6D00, escape and
  GetParameters, the ABORT sequence, and the interrupt endpoint's slot change
  notifications - the only part of the device that speaks unprompted.
- `usb_ncm`: USB CDC-NCM network device (device) driven by an EspUsbHost host
  (DUT). Deliberately a different angle from the EspUsbHost-repo copy: it checks
  the enumerated descriptor detail (separate control/data interfaces, active
  alt, three endpoints with correct directions), the transport-layer frame
  counters (a transfer moves frames in both directions with zero TX failures,
  and the DHCP lease is a real client address, not the gateway's .1), and the
  device-side view (the device's own web server reports it served the host's
  request).
- `usb_audio_speaker`: USB Audio speaker sink (host -> device). Host -> Device
  speaker PCM reception passes on the two-board S3 setup (UAC1 / full speed).
  Logical-channel mute/volume capability, SET/GET, range, and Device event
  delivery are also covered.
  `test_usb_audio_speaker_volume_flood`
  reproduces a real-Windows failure mode by blasting a burst of rapid volume /
  mute SET_CUR changes (like dragging the volume slider) and asserts the device
  keeps running without rebooting. UAC2 is covered separately by
  `usb_audio_uac2`.

- `usb_audio_microphone`: USB Audio source / microphone (device -> host). The
  device streams a generated sawtooth to the host; the host starts the input
  stream and verifies device -> host PCM arrives and is non-silent. UAC1 / full
  speed on the two-board S3 setup.

- `usb_audio_headset`: USB Audio headset (speaker + microphone on one device).
  Verifies both directions at once: both an OUT and an IN stream enumerate and
  start, the host sends speaker PCM that the device receives, and the device's
  mic stream reaches the host and is non-silent. UAC1 / full speed on the
  two-board S3 setup.

- `usb_audio_uac2`: USB Audio Class 2.0 headset driven by an EspUsbHost 2.7.1
  UAC2 host. A different angle from the EspUsbHost-repo copy, which asserts what
  the host learned: here the assertions are on the device - the control state the
  host wrote is read back through the device's own getters and events (master and
  logical channel of the Feature Unit), the sample rate is accepted on the Clock
  Source entity rather than a UAC1 endpoint request, exactly two streams exist
  (the asynchronous playback interface's feedback IN endpoint is not a third), and
  both isochronous directions carry PCM while the feedback endpoint paces the
  host. UAC2 declares one rate per direction, so rate switching is not covered.

Audio follow-up work remains for long playback, real speaker-output checks, real
microphone-capture input, and (optionally) a two-board P4 HS peer for high-speed
Audio coverage.
