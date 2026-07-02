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

## Release Scope

This release covers HID keyboard / mouse / gamepad / consumer / system / custom /
vendor HID, CDC ACM, USB MIDI, MSC, USBVendor, and a minimal USB Audio sink.

Typical use cases:

- Send layout-aware keyboard input, raw HID usages, mouse, gamepad, and media keys.
- Communicate with a PC or EspUsbHost over CDC ACM serial or USB MIDI.
- Expose RAM disks, FAT RAM disks, or SD cards as USB MSC devices.
- Build non-HID vendor-specific bulk/control interfaces.
- Receive USB Audio speaker PCM from the host through a callback.

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
- USBVendor bulk IN/OUT, control requests, and WebUSB landing URL.
- USB Audio speaker sink callback.
- Serial command sketches for pytest-embedded peer and loopback tests.

USB Audio starts with a standalone minimal speaker sink. This library owns the
USB Audio class and PCM callback boundary only. Applications can forward PCM to
PCMFlow, PCMFlowDevice, or any other processing/output layer.

- PCMFlow: https://github.com/tanakamasayuki/PCMFlow
- PCMFlowDevice: https://github.com/tanakamasayuki/PCMFlowDevice

## Minimal Examples

Keyboard:

```cpp
#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

void setup()
{
  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4001;
  config.product = "EspUsbDevice Keyboard";
  device.begin(config);
}

void loop()
{
  if (device.ready())
  {
    keyboard.write("hello");
    delay(1000);
  }
}
```

CDC ACM serial:

```cpp
#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceCdcSerial SerialUSB(device);

void setup()
{
  EspUsbDeviceConfig config;
  config.product = "EspUsbDevice Serial";
  device.begin(config);
}

void loop()
{
  if (SerialUSB.connected())
  {
    SerialUSB.println("hello");
    delay(1000);
  }
}
```

## Examples

User-facing sketches are documented in [examples/README.md](examples/README.md).

- `Keyboard`: boot keyboard that sends layout-aware ASCII strings and HID usage IDs.
- `Mouse`: boot mouse that sends movement, wheel, and buttons.
- `KeyboardMouse`: composite keyboard + mouse HID.
- `Gamepad`: HID gamepad that sends axes, hat, and buttons.
- `MediaKeys`: HID media keys for volume, playback, and system control usages.
- `VendorHID`: vendor-defined HID for custom 63-byte report exchange.
- `USBVendor`: vendor-specific interface with bulk IN/OUT and control requests.
- `CustomHID`: custom HID with a sketch-defined HID report descriptor.
- `Serial`: CDC ACM serial for text communication with a PC or host.
- `MIDI`: USB MIDI device for note / control-change send and receive.
- `MIDIController`: controller that turns ADC / button input into MIDI CC / notes.
- `MIDIInterface`: bridge between UART MIDI 1.0 and USB MIDI 1.0.
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

## Limitations

- Do not use this library together with Arduino-ESP32's standard `USB.begin()`,
  `USBHIDKeyboard`, `USBHIDMouse`, or other built-in USB device classes.
- USB Audio is a standalone minimal speaker sink. Composite Audio devices are
  not supported yet. I2S, codecs, DACs, and other output hardware are outside
  this library's responsibility.
- MSC keeps block devices and filesystems separate. Use the FAT RAM disk helper
  or SD card support when the host should mount a normal drive.
- Direct flash / SPIFFS / LittleFS exposure as USB MSC is not a standard goal.
- When an SD card is exposed to the host as MSC, do not use ESP32-side file APIs
  for the same card while the host owns it.
- WebUSB / Microsoft OS 2.0 descriptor basics are provided by the Arduino-ESP32
  TinyUSB core. Custom vendor code, GUID, and Microsoft OS 2.0 descriptor
  replacement APIs are not implemented yet.

See [tests/TEST_PLAN.md](tests/TEST_PLAN.md) for the test structure and staged
coverage plan.
Design background and migration notes from existing EspUsbHost tests are in
[docs/DESIGN_NOTES.ja.md](docs/DESIGN_NOTES.ja.md).
Current development policy and remaining work are in
[docs/DEVELOPMENT_PLAN.ja.md](docs/DEVELOPMENT_PLAN.ja.md).
See [docs/RELEASE_CHECKLIST.md](docs/RELEASE_CHECKLIST.md) before cutting a release.
