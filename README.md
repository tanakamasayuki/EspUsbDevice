# EspUsbDevice

> 日本語版: [README.ja.md](README.ja.md)

EspUsbDevice is a new ESP32 Arduino USB Device library.

The goal is not compatibility with Arduino-ESP32's `USB`, `USBHIDKeyboard`, or
`USBHIDMouse` APIs. The goal is a better, small, explicit USB device library
where port, speed, descriptors, endpoint packet sizes, and raw class reports are
controlled by the sketch.

The first implementation targets `EspUsbHost` peer and loopback tests because
those tests give concrete hardware coverage and expose the low-level behavior
the library must control. Test-oriented features are the starting point, not the
final boundary of the project.

## Requirements

Minimum Arduino-ESP32 core (board package) version:

| Target | Minimum arduino-esp32 |
| --- | --- |
| ESP32-S2 / ESP32-S3 / ESP32-P4 | 3.3.9 |

Older cores are not officially supported. Per-library-version build results
across core versions are published under [`docs/`](docs/) as
`COMPATIBILITY.<version>.md`; current-worktree runs use the stable draft name
`COMPATIBILITY.WORKTREE.md`.
The automatic matrix observes releases from 3.3.0 onward, but results below
3.3.9 are informational and do not indicate official support.

## Library-owned TinyUSB stack

Version 2 no longer uses Arduino-ESP32's prebuilt TinyUSB configuration,
initializer, task, endpoint allocator, or descriptor loader. EspUsbDevice builds
a pinned, selected TinyUSB source set with its own `tusb_config.h`, initializes
the ESP-IDF PHY/controller directly, runs the device task, and serves its own
device, configuration, string, BOS, and class descriptors.

Arduino-ESP32 remains the platform dependency for ESP-IDF SoC, PHY, FreeRTOS,
and board support. This removes the Arduino USB Device integration dependency;
it does not fork the whole core. The selected files, pin, license, update policy,
and byte-for-byte verification procedure are documented in
[TinyUSB provenance](third_party/tinyusb/PROVENANCE.md).

Owning this boundary makes the following possible:

- Select the ESP32-P4 FullSpeed or HighSpeed controller at runtime.
- Return negotiated-speed descriptors with correct FS/HS endpoint packet sizes,
  device qualifier, and other-speed configuration.
- Allocate composite interfaces/endpoints consistently and reject impossible
  controller-specific endpoint combinations before starting the PHY.
- Generate WebUSB and Microsoft OS 2.0 descriptors for the actual allocated
  vendor interface.
- Configure enabled classes and TinyUSB buffers independently of
  Arduino-ESP32's prebuilt `CFG_TUD_*` values.
- Add a class TinyUSB itself does not implement - CCID - through its
  application class-driver hook, without patching the vendored sources.
- Tear down and reinitialize the runtime without falling back to `USB.begin()`
  or `esp32-hal-tinyusb`.

## Release Scope

This release covers HID keyboard / mouse / gamepad / consumer / system / custom /
vendor HID, CDC ACM, USB MIDI, MSC, USBVendor, USB Audio (speaker / microphone),
a CDC-NCM network device, a CCID smart card reader, and multi-function
composite devices.

Typical use cases:

- Send layout-aware keyboard input, raw HID usages, mouse, gamepad, and media keys.
- Communicate with a PC or EspUsbHost over CDC ACM serial or USB MIDI.
- Expose RAM disks, FAT RAM disks, or SD cards as USB MSC devices.
- Build non-HID vendor-specific bulk/control interfaces.
- Read and write validated UAC1 Playback/Capture PCM through bounded FIFOs.
  UAC2 is selectable and is covered end to end by the `peer/usb_audio_uac2`
  two-board test against an EspUsbHost 2.7.1 UAC2 host.
- Present the board as a USB network adapter (CDC-NCM), with optional lwIP/DHCP
  so a PC can reach a page or API on the device over USB.
- Present the board as a USB smart card reader (CCID) whose card is implemented
  by the sketch, and answer APDUs from a PC/SC host.
- Combine several of the above as one composite device.

## Design Goals

- Share direction, PCM-format, and control-value terminology with EspUsbHost
  and use explicit configuration. High-rate I/O uses bounded polling when a
  callback execution context would be unsafe.
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
- UAC1-default Audio Playback/Capture polling I/O, per-channel mute/volume
  state, control events, and stream stats. UAC2 is available by explicit
  selection, with Clock Source rate control, the UAC2 Feature Unit layout, and
  both streaming directions covered by a peer test.
- CDC-NCM network device with raw-frame API and optional lwIP/esp_netif
  integration (DHCP server / client / static address).
- CCID smart card reader with one slot: sketch-supplied ATR, APDU and escape
  callbacks, and slot change notifications.
- Multi-function composite devices (e.g. HID + CDC + MSC on one device).
- Serial command sketches for pytest-embedded peer and loopback tests.

This library owns the USB Audio class and PCM FIFO boundary only. Applications can
forward PCM to PCMFlow, PCMFlowDevice, or another processing/output layer.
Volume and mute are not applied to PCM implicitly.
`hasMute()` / `getMute()` / `setMute()` and the corresponding volume APIs use
the same Master/Left/Right control state exposed to the USB host. Volume values
use the USB Audio wire unit of 1/256 dB.

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

Diagnostic sketches live in `examples/Info/` - start there when something does
not work. How to use them, and the whole bring-up procedure, is in
[docs/usb-device-guide.md](docs/usb-device-guide.md).

- `Info/EspUsbDeviceBringUpCheck`: start, enumeration, speed, and host -> device traffic, in that order. **Run this first.**
- `Info/EspUsbDeviceDescriptorDump`: every descriptor the library built, plus the endpoint budget (no host needed).
- `Info/EspUsbDeviceConsole`: type HID reports and vendor transfers over serial, and watch every request from the host.

By feature:

- `Keyboard`: boot keyboard that sends layout-aware ASCII strings and HID usage IDs.
- `KeyboardNKRO`: N-key rollover keyboard that holds any number of keys at once.
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
- `UsbNetwork`: CDC-NCM network device with a DHCP server and a web page at
  `http://192.168.7.1/` reachable over USB.
- `SmartCardReader`: CCID smart card reader with an emulated card that answers
  Get UID and an echo instruction.
- `CompositeHidCdcMsc`: HID keyboard + CDC serial + MSC FAT RAM disk as one
  composite device.

## HID Keyboard / Mouse APIs

Keyboard:

- `keyboard.setLayout(layout)` uses the same layout IDs and keymap tables as
  EspUsbHost, reversed for device-side ASCII-to-usage conversion.
- `keyboard.write(text)`, `tapKey(key)`, and `pressKey(key)` are the high-level
  text helpers.
- `keyboard.tapUsage()`, `pressUsage()`, `releaseUsage()`, `releaseAll()`, and
  `sendReport()` keep raw HID usage/report control available.
- `keyboard.onOutputReport(callback)` receives host LED output reports.
- `keyboard.ledState()` returns the latest host LED state
  (`EspUsbDeviceHidKeyboardOutputReport`) **by value**. It is updated whether or not
  a callback is installed, so a sketch can read Lock state even when an integration
  layer owns the single `onOutputReport()` slot - lighting an external Caps Lock LED,
  say. LEDs are state rather than an event, so polling is enough and the callback
  stays single-slot. Everything reads false until the host sends its first output
  report, and the state is cleared on bus reset / unplug.
  By value rather than by reference because the TinyUSB device task writes it while
  the sketch reads it from its own task: a reference would hand out an object another
  task mutates. The raw LED byte is stored atomically and the report is built from
  one read, so a torn combination of fields is impossible. EspBle's `ledState()`
  returns by value for the same reason.
- `keyboard.enableNkro()` (before `begin()`) switches to N-key rollover: a bitmap
  report covering usages `0x00`-`0xDF` (International/LANG keys included, so JIS
  layouts work) that holds any number of keys at once, with automatic 6-key boot
  fallback for BIOS. Off by default.
- `keyboard.sendReport(EspUsbDeviceNkroKeyboardReport)` sends the **whole
  held-key state as one report**. Use it to hold seven or more keys, or to write
  the complete state every cycle - the incremental `pressUsage()` /
  `releaseUsage()` API emits one report per changed key, which splits a chord into
  separate presses. Fails unless `enableNkro()` was called.
- `EspUsbDeviceNkroKeyboardReport` carries `modifiers` plus a 28-byte `bitmap` of
  usages `0x00`-`MaxBitmapUsage` (`0xDF`), operated with `clear()` / `press()` /
  `release()` / `isDown()`. Modifier usages `0xE0`-`0xE7` sit outside the bitmap,
  so `press()` / `release()` route them into `modifiers` and callers never have to
  tell the two apart. They return false only for a usage this report cannot
  represent (above `0xDF` and not a modifier).
- Naming rule: **a member holding a bitmap is `bitmap`, a member holding an array
  of usages is `keys`**. The 6KRO `EspUsbDeviceBootKeyboardReport::keys[6]` is a
  usage array, and `keys[0] = 0x04` would mean something different against each
  type while still compiling, so the two names are kept apart. Sibling libraries
  `EspUsbHost` and `EspBle` follow the same rule. Bitmap sizes are deliberately
  **asymmetric**: the host side covers usages `0x00`-`0xFF` (32 bytes) while a
  device is bounded by its own report descriptor (`0x00`-`0xDF`, 28 bytes).
- `keyboard.heldState()` returns the NKRO state the host was last told about, for
  suppressing duplicate sends and for resynchronising without a `releaseAll()`
  (the library never suppresses repeats itself). While the host selected boot
  protocol this is the requested state, not the bytes on the wire, which are
  folded down to 6 keys.

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
- `EspUsbDeviceMidi(device, cableCount)` exposes up to 16 cables, each of which
  the host sees as a separate MIDI port. The default is 1. Cables share one pair
  of bulk endpoints and are addressed by the 0-based `cable` argument on every
  helper, or by the high nibble of `EspUsbDeviceMidiPacket::header`.
- `EspUsbDeviceMidi(device, inCableCount, outCableCount)` gives the two directions
  different cable counts, as many real MIDI interfaces have. Both names are
  host-view, like USB endpoint directions and EspUsbHost's `EspUsbHostMidiPortInfo`:
  IN is device to host (what the device sends), OUT is host to device. `noteOn()`
  and the other send helpers are bounded by `inCableCount()`, so a cable that
  exists only for receiving is refused rather than landing on another port.
- Per-cable names are not implemented; the host names the ports itself.
- Pairing with an ESP-IDF USB host (EspUsbHost) caps the cable count at **5**.
  That host refuses a configuration descriptor longer than its enumeration control
  transfer, and `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE` is 256 in the
  precompiled Arduino libraries with no way to raise it from a sketch. 5 cables is
  229 bytes including the configuration header and enumerates; 6 is 261 and fails
  with `CHECK_SHORT_CONFIG_DESC FAILED`, measured in
  `tests/peer/usb_midi_cables`. All 16 are legal USB and a PC host takes them; the
  limit is the host stack, not this library.

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

## USB Audio APIs

The former card-shaped `EspUsbDeviceAudio` implementation has been removed.
Audio is now one function with independent Playback (host to device) and Capture
(device to host) streams:

```cpp
EspUsbDevice device;
EspUsbAudioFunction audio(device); // UAC1 by default

auto &playback = audio.addPlaybackStream();
playback.addFormat({48000, 2, 2, 16});

auto &capture = audio.addCaptureStream();
capture.addFormat({48000, 1, 2, 16});
```

- `EspUsbAudioPlaybackStream::available()` / `read()` consume speaker PCM.
- `EspUsbAudioCaptureStream::write()` supplies microphone PCM.
- Format fields are `sampleRate`, `channels`, `bytesPerSample`, and
  `bitsPerSample`, matching EspUsbHost terminology.
- `pollEvent()` delivers stream-state, sample-rate, mute, and volume changes
  from a bounded queue. PCM uses polling so high-rate application work, I2S
  writes, and user callbacks never run on or block the TinyUSB task.
- `stats()`, `resetStats()`, and `clearBuffer()` expose transfer, overrun, and
  underrun behavior instead of hiding it inside an audio-card receive task.
- Master/Left/Right state is shared with USB Feature Unit requests through the
  mute/volume APIs. Volume uses signed 1/256 dB wire units.
- The library never applies mute, volume, downmixing, or other DSP implicitly.
  I2S, codecs, microphones, speakers, and DSP remain application or optional
  PCMFlow/PCMFlowDevice responsibilities.

UAC1 is the default and is covered by S3 speaker, microphone, and duplex peer
streaming tests, including control changes and 16/24/32-bit
descriptor/transfer coverage. UAC2 is selected with
`EspUsbAudioFunction(device, EspUsbAudioProtocol::Uac2)`. Its descriptors, Clock
Source sample-rate control, Feature Unit mute/volume (master and per logical
channel), and both isochronous directions - including the asynchronous playback
interface's explicit feedback endpoint - are covered by the `peer/usb_audio_uac2`
two-board test against an EspUsbHost UAC2 host. A UAC2 function declares one
sample rate per direction: the descriptor builder emits a single alternate
setting, so the Clock Source has one rate to report.

The new Audio source was independently designed from USB Audio specifications
and TinyUSB's public driver API. The old Espressif USBAudioCard-derived source
was deleted rather than carried forward. See
[Audio source provenance](docs/V2_AUDIO_PROVENANCE.ja.md) (Japanese).

## CCID (Smart Card Reader) APIs

- `EspUsbDeviceCcid` presents the board as a USB CCID reader with one slot
  (`bInterfaceClass` 0x0b, bulk IN/OUT plus an interrupt IN for slot changes).
  The card behind the slot is the sketch's.
- `insertCard(atr, length)` / `removeCard()` decide whether a card is present
  and what ATR `IccPowerOn` answers with; both notify the host over the
  interrupt endpoint. `cardPresent()` / `cardPowered()` report the slot state.
- `onApdu(callback)` answers each exchange: the callback receives the APDU and
  writes the answer including SW1SW2, so the sketch decides what the card is.
  Without a callback the device answers 6D00 like a card that does not know the
  instruction. `onEscape(callback)` is the same for vendor-specific
  `PC_to_RDR_Escape` traffic, and `onPower(callback)` reports activation.
- The device answers the slot-status, activation, parameter, and abort messages
  itself, so a conforming host needs no help from the sketch for them.
- Callbacks run in the TinyUSB device task: return promptly and do not call back
  into USB from them.

## Network / Composite APIs

USB network (CDC-NCM):

- `EspUsbDeviceNet` presents the board as a USB network adapter. Modern Windows /
  macOS / Linux bind their native NCM driver with no install.
- `onFrame()` / `sendFrame()` expose raw Ethernet frames; skip `beginNetwork()`
  to stay a pure frame transport (useful for PC-side bridging).
- `beginNetwork()` brings up an lwIP/esp_netif interface. Choose the addressing:
  `dhcpServer(true)` (device is the gateway, hands the host an address),
  `dhcpClient(true)` (get an address from a PC-bridged LAN), or `ipConfig(...)`
  for a static address. DHCP is opt-in. The subnet defaults to `192.168.7.0/24`
  (device at `192.168.7.1`); pass `ipConfig(local, gateway, subnet)` before
  `beginNetwork()` to change it — the DHCP server pool follows the configured
  IP/mask automatically.
- The DHCP server does not advertise a gateway/DNS by default (so it never
  black-holes the host's real internet path). `dhcpAdvertiseGateway(true)` /
  `dhcpDns(ip)` opt in when the device actually forwards or has a reachable DNS.
- The USB netif uses a low route priority so a coexisting Wi-Fi STA stays the
  ESP's default route. `defaultRoute(true)` makes the USB host the ESP's uplink
  instead (for a PC that bridges/NATs to the device, with `dhcpClient(true)`).
- The MAC reported to the host defaults to this chip's per-device Ethernet MAC
  (`esp_read_mac` / `ESP_MAC_ETH`), which is unique per board and distinct from
  the Wi-Fi STA/AP and BT MACs, so it never collides with the ESP's own Wi-Fi.
  A single device per host is always fine; connecting two identical boards to the
  same host works because each has a different MAC. Call `macAddress(mac)` before
  `begin()` to pin a specific address (note: two boards forced to the same MAC on
  one host will conflict, and two `dhcpServer(true)` devices default to the same
  `192.168.7.0/24` subnet — give each a different `ipConfig(...)` subnet to run
  several on one host).

Composite:

- Register several classes with one `EspUsbDevice` and call `begin()` once; the
  library assigns interface numbers and endpoints and builds the composite
  descriptor. See `CompositeHidCdcMsc`.

## Limitations

- Do not use this library together with Arduino-ESP32's standard `USB.begin()`,
  `USBHIDKeyboard`, `USBHIDMouse`, or other built-in USB device classes.
- USB Audio uses `EspUsbAudioFunction` for Playback/Capture. UAC1 is the
  compatibility-oriented default; select UAC2 explicitly with
  `EspUsbAudioFunction(device, EspUsbAudioProtocol::Uac2)`. A UAC2 function
  declares exactly one sample rate and one alternate setting per direction, so a
  host cannot switch rates on it. I2S, codecs, DACs, and other audio hardware
  are outside this library's responsibility.
- The network device is CDC-NCM only. CDC-ECM is not enabled in the Arduino-ESP32
  core (it would need a core rebuild); NCM is supported natively by modern hosts.
  A device reaching the internet through the PC needs host-side bridging/NAT and
  is out of scope; use the ESP's own Wi-Fi for that.
- The CCID reader is one slot, T=1, short APDU level exchange. Chaining,
  extended APDUs, PIN pad / secure entry, mechanical slots, and clock / data-rate
  negotiation are out of scope - the class descriptor declares none of them, so a
  conforming host never asks. The largest CCID message is 271 bytes
  (`ESP_USB_DEVICE_CCID_BUFFER_SIZE` raises it).
- Composite devices are bounded by the ESP32-S3 endpoint budget and the
  configuration-descriptor capacity. Audio composite constraints are still
  being validated on the Device side.
- MSC keeps block devices and filesystems separate. Use the FAT RAM disk helper
  or SD card support when the host should mount a normal drive.
- Direct flash / SPIFFS / LittleFS exposure as USB MSC is not a standard goal.
- When an SD card is exposed to the host as MSC, do not use ESP32-side file APIs
  for the same card while the host owns it.
- WebUSB and Microsoft OS 2.0 descriptors are owned by this library. When
  WebUSB is enabled with `USBVendor`, Windows receives a fixed WinUSB
  compatible-ID and device-interface GUID for the allocated vendor interface.
  Custom vendor-code, GUID, and descriptor replacement APIs are not implemented
  yet.

USB device fundamentals, the ESP32-specific constraints, and how to diagnose a
device the host will not accept are covered in
[docs/usb-device-guide.md](docs/usb-device-guide.md).
The relationship with TinyUSB, descriptors byte by byte, callback context, and
implementing your own class are covered in
[docs/usb-device-advanced.md](docs/usb-device-advanced.md).
See [tests/TEST_PLAN.md](tests/TEST_PLAN.md) for the test structure and staged
coverage plan.
Design background and migration notes from existing EspUsbHost tests are in
[docs/DESIGN_NOTES.ja.md](docs/DESIGN_NOTES.ja.md) (Japanese).
Current development policy and remaining work are in
[docs/DEVELOPMENT_PLAN.ja.md](docs/DEVELOPMENT_PLAN.ja.md) (Japanese).
See [docs/RELEASE_CHECKLIST.md](docs/RELEASE_CHECKLIST.md) before cutting a release.
