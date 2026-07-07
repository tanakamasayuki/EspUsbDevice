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
vendor HID, CDC ACM, USB MIDI, MSC, USBVendor, USB Audio (speaker / microphone),
a CDC-NCM network device, and multi-function composite devices.

Typical use cases:

- Send layout-aware keyboard input, raw HID usages, mouse, gamepad, and media keys.
- Communicate with a PC or EspUsbHost over CDC ACM serial or USB MIDI.
- Expose RAM disks, FAT RAM disks, or SD cards as USB MSC devices.
- Build non-HID vendor-specific bulk/control interfaces.
- Send/receive USB Audio speaker and microphone PCM through callbacks.
- Present the board as a USB network adapter (CDC-NCM), with optional lwIP/DHCP
  so a PC can reach a page or API on the device over USB.
- Combine several of the above as one composite device.

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
- USB Audio speaker and microphone PCM callbacks.
- CDC-NCM network device with raw-frame API and optional lwIP/esp_netif
  integration (DHCP server / client / static address).
- Multi-function composite devices (e.g. HID + CDC + MSC on one device).
- Serial command sketches for pytest-embedded peer and loopback tests.

USB Audio is a standalone speaker / microphone device. This library owns the
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
- `UsbNetwork`: CDC-NCM network device with a DHCP server and a web page at
  `http://192.168.7.1/` reachable over USB.
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
- USB Audio (`EspUsbDeviceAudio`) is a standalone speaker / microphone device
  and is exclusive: it cannot be combined with other classes in a composite.
  I2S, codecs, DACs, and other audio hardware are outside this library's
  responsibility.
- The network device is CDC-NCM only. CDC-ECM is not enabled in the Arduino-ESP32
  core (it would need a core rebuild); NCM is supported natively by modern hosts.
  A device reaching the internet through the PC needs host-side bridging/NAT and
  is out of scope; use the ESP's own Wi-Fi for that.
- Composite devices are bounded by the ESP32-S3 USB endpoint budget (about three
  FIFO-consuming IN endpoints); a fourth needs the ESP32-P4. USB Audio cannot be
  part of a composite.
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
