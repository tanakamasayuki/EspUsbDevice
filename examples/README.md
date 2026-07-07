# Examples

Arduino sketches that show the basic `EspUsbDevice` usage. Sketches using this
library should not call Arduino-ESP32's `USB.begin()`, `USBHIDKeyboard`, or
`USBHIDMouse`.

## Keyboard

HID boot keyboard device example.
See [Keyboard/README.md](Keyboard/README.md) for details.

- Configure port, speed, VID/PID, and string descriptors with
  `EspUsbDeviceConfig`.
- Send US ASCII strings with `EspUsbDeviceHidKeyboard::write()`.
- Use `tapKey()` for one character and `tapUsage()` / `pressUsage()` for raw HID
  usage IDs.
- Select the same layout IDs as EspUsbHost with `setLayout()` to change
  character-to-usage mapping.
- Receive NumLock, CapsLock, ScrollLock, and other LED state with
  `onOutputReport()`.

The string wrapper uses the same keymap tables as EspUsbHost in reverse and uses
the same layout IDs.

## Mouse

HID boot mouse device example.
See [Mouse/README.md](Mouse/README.md) for details.

- Send movement, wheel, and button state with `EspUsbDeviceHidMouse::move()`.
- Use `click()` for a press/release pair.
- Left, right, middle, back, and forward are exposed as raw button bits.

## KeyboardMouse

Composite keyboard + mouse HID device example. The current composite HID
implementation uses one HID interface with report IDs.
See [KeyboardMouse/README.md](KeyboardMouse/README.md) for details.

- Keyboard report ID: `1`
- Mouse report ID: `2`
- Composite HID endpoint MPS: `16 bytes`

## Gamepad

HID gamepad device example.
See [Gamepad/README.md](Gamepad/README.md) for details.

- Send axes, hat switch, and button bitmask with `EspUsbDeviceHidGamepad`.
- Use `send()` to send 6 axes, hat, and 32 buttons together.
- Check behavior through a PC game controller settings screen or EspUsbHost's
  `onGamepad()`.

## MediaKeys

HID consumer control / system control device example.
See [MediaKeys/README.md](MediaKeys/README.md) for details.

- Send volume, mute, play/pause, and track controls with
  `EspUsbDeviceHidConsumerControl`.
- Handle power / standby / wake usages with `EspUsbDeviceHidSystemControl`.
- System control keys can affect host power state, so this example does not send
  them automatically.

## VendorHID

Vendor-defined HID device example.
See [VendorHID/README.md](VendorHID/README.md) for details.

- Exchange 63-byte Input / Output / Feature report payloads with
  `EspUsbDeviceHidVendor`.
- Send periodic Input reports from device to host.
- Receive host Output / Feature reports in callbacks and print them to Serial
  monitor.
- Useful for small custom protocols with a host application or EspUsbHost.

## USBVendor

Non-HID vendor-specific USB interface example.
See [USBVendor/README.md](USBVendor/README.md) for details.

- Use `EspUsbDeviceVendor` for custom traffic over bulk IN / OUT endpoints.
- Use the stream-like `available()` / `read()` / `write()` / `flush()` API.
- Receive EP0 vendor requests with `onControlRequest()`.
- Configure a WebUSB landing URL through `EspUsbDeviceConfig`.

## CustomHID

Custom HID device example using a sketch-provided HID report descriptor.
See [CustomHID/README.md](CustomHID/README.md) for details.

- Define `REPORT_DESCRIPTOR` in the sketch.
- Pass the descriptor and input report size to `EspUsbDeviceHidCustom`.
- Send an 8-byte Input report every second.
- Useful for validating custom descriptors or prototyping small fixed-size HID
  reports.

## Serial

USB CDC ACM serial device example.
See [Serial/README.md](Serial/README.md) for details.

- Use `EspUsbDeviceCdcSerial` to exchange text with a PC or host.
- Use `available()` / `read()` / `write()` / `print()` / `printf()`.
- Receive host line coding and DTR / RTS state through callbacks.
- Keep USB CDC separate from the logging Serial monitor.

## MIDI

USB MIDI device example.
See [MIDI/README.md](MIDI/README.md) for details.

- Send Note On / Off, Control Change, and related messages with
  `EspUsbDeviceMidi`.
- Send and receive raw 4-byte USB-MIDI event packets.
- Print MIDI packets received from the host to Serial monitor.
- Test with a DAW, MIDI monitor, EspUsbHost, or another USB host.

## MIDIController

Example that turns ADC and button input into USB MIDI messages.
See [MIDIController/README.md](MIDIController/README.md) for details.

- Convert analog input to MIDI CC.
- Convert button press / release to Note On / Off.
- Build a simple MIDI controller with a potentiometer or BOOT button.

## MIDIInterface

Bridge example between UART MIDI 1.0 and USB MIDI 1.0.
See [MIDIInterface/README.md](MIDIInterface/README.md) for details.

- Convert 31250-baud serial MIDI to USB-MIDI event packets.
- Convert host USB-MIDI event packets back to serial MIDI byte stream.
- Use it as a bridge between DIN MIDI devices and a USB MIDI host.

## AudioSpeaker

USB Audio speaker sink device example.
See [AudioSpeaker/README.md](AudioSpeaker/README.md) for details.

- Receive speaker PCM from the host with `EspUsbDeviceAudio`.
- Receive PCM chunks and format metadata through the `onPcm()` callback.
- Receive volume, mute, sample-rate, and interface-enable changes through
  `onEvent()`.
- I2S bridging and codec setup are outside this library's responsibility. The
  received PCM can be forwarded to the application, PCMFlow, PCMFlowDevice, or
  another output layer.

## AudioMicrophone

USB Audio source (microphone) device example: the device sends PCM to the host.

- Expose a mono 48 kHz / 16-bit recording device with `EspUsbDeviceAudio`
  configured as speaker `NONE` + microphone `MONO`.
- Push PCM toward the host with `writeMic()`; here a generated 440 Hz sine tone.
- The same class covers both directions (speaker for host -> device, microphone
  for device -> host); audio capture/codec input is the application's job.

## AudioHeadset

USB Audio headset example: one device that is both a speaker (host -> device)
and a microphone (device -> host) at the same time.

- Configure `EspUsbDeviceAudio` with both a speaker and a microphone channel
  layout (mono 48 kHz / 16-bit here); the host sees one playback and one
  recording device.
- Loopback headset: received speaker PCM is echoed straight back to the
  microphone with `writeMic()` from the `onData()` callback.
- Shows that a single `EspUsbDeviceAudio` instance carries both directions
  simultaneously.

## AudioSpeakerM5

Example that connects the USB Audio speaker sink to PCMFlowDevice's M5 speaker
helper.
See [AudioSpeakerM5/README.md](AudioSpeakerM5/README.md) for details.

- Receives PCM from the PC as a 48 kHz / 16-bit / stereo USB Audio speaker.
- Applies host mute / volume with `audio.applyVolume()`.
- Uses PCMFlowDevice's `M5SpeakerBufferedPlayer::writePcm()` to downmix stereo
  input and feed `M5.Speaker` safely.
- EspUsbDevice itself does not depend on PCMFlow or PCMFlowDevice; only this
  example uses them as optional integration libraries.

## AudioMicrophoneM5

USB Audio microphone backed by the M5 built-in microphone (device -> host).

- Captures mono 16 kHz / 16-bit PCM from `M5.Mic` and streams it to the host
  with `writeMic()`; the PC sees a recording device.
- Uses `M5.Mic.record()` (asynchronous, double-buffered) with a small ring, and
  sends the oldest completed block.
- Only depends on M5Unified for capture; EspUsbDevice provides the USB side.

## AudioHeadsetM5

USB Audio headset backed by M5 hardware: M5 speaker playback (host -> device)
and M5 microphone capture (device -> host).

- Plays received PCM on `M5.Speaker` via PCMFlowDevice's `M5SpeakerBufferedPlayer`
  and streams `M5.Mic` capture back to the host, at 48 kHz / 16-bit (stereo
  speaker, mono microphone); the shared USB sample rate applies to both.
- **Known limitation:** running the M5 speaker and mic at the same time is not
  reliably supported. M5Unified drives both through one I2S port installed
  TX-only / RX-only and each `begin()` uninstalls the other, so full duplex
  glitches — confirmed on CoreS3 too (a second I2S port did not help). This is a
  best-effort demo. Use `AudioSpeakerM5` or `AudioMicrophoneM5` for reliable
  single-direction audio, or the non-M5 `AudioHeadset` for a both-directions
  demo over USB.

## MSC

USB Mass Storage Class device example.
See [MSC/README.md](MSC/README.md) for details.

- Configure SCSI inquiry strings, media state, and writability with
  `EspUsbDeviceMsc`.
- Expose a RAM buffer as a 512-byte block device with `EspUsbDeviceMscRamDisk`.
- Use `disk.attach(msc)` to install read/write callbacks and call `msc.begin()`.
- This example validates SCSI / block I/O transport. It is not a FAT-formatted
  USB flash drive.

MSC separates the block device from the filesystem. To make host-mountable
storage, provide a valid FAT image or back the callbacks with real block storage
such as an SD card. Direct flash / SPIFFS / LittleFS exposure is not planned for
standard examples. Practical persistent storage should use SD first, while RAM
disk can support temporary firmware, configuration, or Wi-Fi handoff files once
a FAT helper is added.

## MSCFatRamDisk

MSC device example that creates a small FAT12 image in RAM.
See [MSCFatRamDisk/README.md](MSCFatRamDisk/README.md) for details.

- Use `EspUsbDeviceMscFatRamDisk` to create a host-mountable RAM disk.
- Add `README.TXT` as an initial file.
- Let the host copy `CONFIG.TXT`, eject the drive, then scan/read the file on the
  device side.
- This is the basic pattern for temporary firmware, configuration, and Wi-Fi
  handoff files.

## MSCSdCard

MSC example that exposes an SPI-connected SD card as a block device.
See [MSCSdCard/README.md](MSCSdCard/README.md) for details.

- Connect Arduino `SD` raw sector I/O to MSC with `EspUsbDeviceMscSdCard`.
- Let the host read/write the SD card as ordinary USB storage.
- Do not use ESP32-side file APIs such as `SD.open()` while the host owns the SD
  card.
- This is the basic practical persistent-storage example.

## CompositeHidCdcMsc

All-in-one composite device: HID keyboard + CDC serial + MSC FAT RAM disk on one
`EspUsbDevice`.
See [CompositeHidCdcMsc/README.md](CompositeHidCdcMsc/README.md) for details.

- Register several functions with the same `EspUsbDevice` and call `begin()`
  once; the library builds the composite descriptor and assigns interfaces and
  endpoints.
- Send `type <text>` over the CDC port to type it on the HID keyboard.
- The richest composite that fits the ESP32-S3 endpoint budget (three FIFO-IN
  endpoints); a fourth FIFO-IN class needs the ESP32-P4.
- The USB Audio class is exclusive and cannot be combined with other classes.

## Notes

- Connect the USB-device-capable ESP32-S3 or similar board to a USB host.
- Serial monitor output is only for logs. Check HID behavior through the USB
  host connection.
- This library is not designed to run together with Arduino's built-in USB
  device classes.
