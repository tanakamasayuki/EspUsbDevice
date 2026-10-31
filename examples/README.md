# EspUsbDevice examples

> 日本語: [README.ja.md](README.ja.md)

Every example is a complete sketch with its own README and a `sketch.yaml` that
pins Arduino-ESP32 3.3.12. Start with the README of the example closest to what
you want to build; each one says what it shows, what you need and how to check
it from the host.

## Building

Each `sketch.yaml` declares the boards it supports:

| Profile | Board |
| --- | --- |
| `esp32s3` | ESP32-S3, USB Mode "USB-OTG (TinyUSB)" (`USBMode=default`) |
| `esp32s2` | ESP32-S2 |
| `esp32p4` | ESP32-P4 revision v1.x (`ChipVariant=prev3`) |
| `esp32p4x` | ESP32-P4 revision v3.1 or later, "P4X" (`ChipVariant=postv3`) |

```sh
arduino-cli compile --profile esp32s3 examples/Serial
arduino-cli upload  --profile esp32s3 --port <port> examples/Serial
python3 tools/build_check.py esp32s3        # every example declaring a profile
```

Plug the board's USB OTG connector into the host. Examples that print a log use
`Serial` on a separate debug connection (USB-UART or USB Serial/JTAG), so the
USB device under test and the log never share a cable. On ESP32-P4 the
examples use the high-speed controller unless they say otherwise; see
[P4HighSpeedDevice](P4HighSpeedDevice/) and [P4FullSpeedDevice](P4FullSpeedDevice/).

## HID

| Example | What it shows | Class guide |
| --- | --- | --- |
| [Keyboard](Keyboard/) | Boot keyboard: typing text, layouts, raw usages, combined keys, LED state | [HID](../docs/classes/hid.md) |
| [KeyboardNKRO](KeyboardNKRO/) | N-key rollover with a boot-protocol fallback | [HID](../docs/classes/hid.md) |
| [Mouse](Mouse/) | Boot mouse: movement, wheel, buttons, dragging | [HID](../docs/classes/hid.md) |
| [KeyboardMouse](KeyboardMouse/) | Keyboard and mouse merged into one composite HID interface | [HID](../docs/classes/hid.md) |
| [Gamepad](Gamepad/) | Axes, hat switch and buttons | [HID](../docs/classes/hid.md) |
| [MediaKeys](MediaKeys/) | Consumer Control and System Control | [HID](../docs/classes/hid.md) |
| [CustomHID](CustomHID/) | A sketch-defined report descriptor | [HID](../docs/classes/hid.md) |
| [VendorHID](VendorHID/) | Vendor-defined Input, Output and Feature reports | [HID](../docs/classes/hid.md) |

## Serial, MIDI and vendor bulk

| Example | What it shows | Class guide |
| --- | --- | --- |
| [Serial](Serial/) | CDC ACM with line coding, DTR/RTS and break | [CDC](../docs/classes/cdc.md) |
| [CdcConsole](CdcConsole/) | Passing the CDC port to code that takes a `Stream&` | [CDC](../docs/classes/cdc.md) |
| [SerialMulti](SerialMulti/) | Several CDC ports on one device | [CDC](../docs/classes/cdc.md) |
| [MIDI](MIDI/) | Sending notes and control changes, decoding received packets | [MIDI](../docs/classes/midi.md) |
| [MidiEcho](MidiEcho/) | Packet echo that keeps a packet the host has not taken yet | [MIDI](../docs/classes/midi.md) |
| [MIDIController](MIDIController/) | ADC and buttons to MIDI | [MIDI](../docs/classes/midi.md) |
| [MIDIInterface](MIDIInterface/) | UART MIDI (31250 baud) to USB MIDI bridge | [MIDI](../docs/classes/midi.md) |
| [USBVendor](USBVendor/) | Vendor bulk interface, control requests, WebUSB, WinUSB | [Vendor](../docs/classes/vendor.md) |
| [VendorEcho](VendorEcho/) | Minimal bulk echo that keeps a partial write | [Vendor](../docs/classes/vendor.md) |
| [ReconfigureVendor](ReconfigureVendor/) | Changing buffer sizes at run time with `end()`, `configure()`, `begin()` | [Vendor](../docs/classes/vendor.md) |

## Storage and firmware update

| Example | What it shows | Class guide |
| --- | --- | --- |
| [MSC](MSC/) | Raw block device in RAM | [MSC](../docs/classes/msc.md) |
| [MSCFatRamDisk](MSCFatRamDisk/) | FAT12 RAM disk with files placed by the sketch | [MSC](../docs/classes/msc.md) |
| [MSCSdCard](MSCSdCard/) | SD card over SPI as a USB drive | [MSC](../docs/classes/msc.md) |
| [FirmwareMSC](FirmwareMSC/) | Update by copying a `.bin` or `.uf2` onto a drive | [OTA over USB](../docs/ota-over-usb.md) |
| [FirmwareDFU](FirmwareDFU/) | Update with `dfu-util` while the sketch keeps running | [DFU](../docs/classes/dfu.md) |
| [FirmwareHTTP](FirmwareHTTP/) | Update from a browser over a USB network link | [OTA over USB](../docs/ota-over-usb.md) |
| [FirmwareCDC](FirmwareCDC/) | Update over a serial port with a small host script | [OTA over USB](../docs/ota-over-usb.md) |
| [FirmwareVendor](FirmwareVendor/) | Update over a vendor interface with a host script | [OTA over USB](../docs/ota-over-usb.md) |
| [FirmwareBootMode](FirmwareBootMode/) | Entering the ROM loader or ROM DFU from the sketch | [OTA over USB](../docs/ota-over-usb.md) |

## Audio, video and other classes

| Example | What it shows | Class guide |
| --- | --- | --- |
| [AudioSpeaker](AudioSpeaker/) | UAC1 playback, rate/mute/volume events | [Audio](../docs/classes/audio.md) |
| [AudioPlayback](AudioPlayback/) | UAC2 playback with several formats and rates | [Audio](../docs/classes/audio.md) |
| [AudioMicrophone](AudioMicrophone/) | UAC1 capture of a generated tone | [Audio](../docs/classes/audio.md) |
| [AudioHeadset](AudioHeadset/) | Playback and capture in one function | [Audio](../docs/classes/audio.md) |
| [AudioSpeakerM5](AudioSpeakerM5/), [AudioMicrophoneM5](AudioMicrophoneM5/), [AudioHeadsetM5](AudioHeadsetM5/) | The same with M5Stack speaker and microphone | [Audio](../docs/classes/audio.md) |
| [VideoCamera](VideoCamera/) | UVC camera streaming a generated test pattern | [Video](../docs/classes/video.md) |
| [UsbNetwork](UsbNetwork/) | CDC-NCM network device with DHCP and a web server | [NCM](../docs/classes/ncm.md) |
| [SmartCardReader](SmartCardReader/) | CCID reader with a card implemented in the sketch | [CCID](../docs/classes/ccid.md) |

## Composite devices and controllers

| Example | What it shows |
| --- | --- |
| [CompositeHidCdcMsc](CompositeHidCdcMsc/) | Keyboard, serial port and FAT disk in one device |
| [CompositeCdcMidi](CompositeCdcMidi/) | Serial port and MIDI in one device |
| [MscCdcBlocks](MscCdcBlocks/) | Block device and serial port, the staging size valid at full and high speed |
| [P4HighSpeedDevice](P4HighSpeedDevice/) | The ESP32-P4 high-speed (480 Mbps) controller |
| [P4FullSpeedDevice](P4FullSpeedDevice/) | The ESP32-P4 full-speed controller and its pins |
| [Info](Info/) | Bring-up check, interactive console and descriptor dump |

## What has been checked

Every example compiles for each profile its `sketch.yaml` declares. The library
behaviour the examples rely on is tested on hardware by the separate
`validation/` probes (ESP32-S3 at full speed, ESP32-P4 and P4X at full and high
speed, a P4X against a Linux PC); see [testing](../docs/development/testing.ja.md).
The examples themselves have not each been run against Windows or macOS hosts.

Coming from EspUsbDevice 2.x? Every 2.x example exists here under the same name;
see [Migrating from v2](../docs/migrating-from-v2.md).
