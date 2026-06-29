# EspUsbDevice

EspUsbDevice is a new ESP32 Arduino USB Device library.

The goal is not compatibility with Arduino-ESP32's `USB`, `USBHIDKeyboard`, or
`USBHIDMouse` APIs. The goal is a better, small, explicit USB device library
where port, speed, descriptors, endpoint packet sizes, and raw class reports are
controlled by the sketch.

The first implementation targets `EspUsbHost` peer and loopback tests because
those tests give concrete hardware coverage and expose the low-level behavior
the library must control. Test-oriented features are the starting point, not the
final boundary of the project.

## Design Goals

- Use EspUsbHost-style explicit configuration and callback APIs.
- Own USB descriptors in this library instead of relying on Arduino USB class
  descriptors.
- Treat HID usage IDs and raw reports as the primary API.
- Support ESP32-S3 two-board peer tests and ESP32-P4 one-board loopback tests as
  early validation targets.
- Keep Arduino-ESP32's standard USB device stack mutually exclusive. Sketches
  using this library must not call `USB.begin()`.

## Current Scope

The first milestone is replacing existing EspUsbHost peer devices and validating
the core API on real hardware. The project started with the HID MVP and now
covers CDC ACM, USB MIDI, and MSC through peer and loopback tests where
available:

- Device port/speed/VID/PID/string/power configuration.
- Speed-aware descriptor generation and endpoint MPS selection.
- HID boot keyboard raw report sending.
- HID keyboard output report callback for LED state.
- HID boot mouse raw report sending.
- HID consumer / system / gamepad / custom / vendor reports.
- CDC ACM serial.
- USB MIDI event packets and note/control-change helpers.
- USB MSC block device and SCSI callbacks.
- Serial command sketches for pytest-embedded peer and loopback tests.

## Examples

User-facing sketches are documented in [examples/README.md](examples/README.md).

- `Keyboard`: boot keyboard that sends layout-aware ASCII strings and HID usage IDs.
- `Mouse`: boot mouse that sends movement, wheel, and buttons.
- `KeyboardMouse`: composite keyboard + mouse HID.
- `Serial`: CDC ACM serial for text communication with a PC or host.
- `MIDI`: USB MIDI device for note / control-change send and receive.
- `MSC`: Mass Storage Class device that exposes a RAM buffer as a block device.
- `MSCFatRamDisk`: Mass Storage Class device that exchanges files through a RAM
  FAT12 disk.
- `MSCSdCard`: Mass Storage Class device that exposes an SPI SD card as USB
  storage.

## HID Keyboard / Mouse APIs

Keyboard:

- `keyboard.setLayout(layout)` uses the same layout IDs and keymap tables as
  EspUsbHost, reversed for device-side ASCII-to-usage conversion.
- `keyboard.write(text)`, `tapKey(key)`, and `pressKey(key)` are the high-level
  text helpers.
- `keyboard.tapUsage()`, `pressUsage()`, `releaseUsage()`, `releaseAll()`, and
  `sendReport()` keep raw HID usage/report control available.
- `keyboard.onOutputReport(callback)` receives host LED output reports.

Mouse:

- `mouse.move(x, y)`, `wheel(delta)`, and `sendReport(report)` send movement and
  raw reports.
- `mouse.press(buttons)`, `release(buttons)`, `releaseAll()`, `click(button)`,
  and `buttons()` maintain device-side button state.

## CDC / MIDI / MSC APIs

CDC ACM:

- `EspUsbDeviceCdcSerial` provides USB serial read/write callbacks and helpers.
- It supports Arduino-style `available()`, `read()`, `write()`, and `print()`
  usage as well as raw callbacks.

USB MIDI:

- `EspUsbDeviceMidi` sends 4-byte USB-MIDI event packets.
- Use helpers such as `noteOn()`, `noteOff()`, and `controlChange()` together
  with raw `writePacket()`.

MSC:

- `EspUsbDeviceMsc` handles inquiry strings, media state, capacity, and
  read/write callbacks.
- `EspUsbDeviceMscRamDisk` wraps an external RAM buffer as a block device.
- `EspUsbDeviceMscFatRamDisk` creates a small FAT12 image in RAM for temporary
  host/device file handoff.
- `EspUsbDeviceMscSdCard` connects Arduino `SD` raw sector I/O to MSC.
- MSC separates the block device from the filesystem. To make a drive mountable
  by an OS, provide a valid FAT image or connect the read/write callbacks to
  real storage such as an SD card.
- Direct flash / SPIFFS / LittleFS exposure is not the standard direction.
  Persistent storage should use SD card first, and temporary file handoff should
  use RAM disk plus a FAT helper.

See [tests/TEST_PLAN.md](tests/TEST_PLAN.md) for the test structure and staged
coverage plan.
Design background and migration notes from existing EspUsbHost tests are in
[docs/DESIGN_NOTES.ja.md](docs/DESIGN_NOTES.ja.md).
The development order based on the currently verified hardware and tool
environment is in [docs/DEVELOPMENT_PLAN.ja.md](docs/DEVELOPMENT_PLAN.ja.md).
