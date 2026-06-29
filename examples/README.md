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

## Notes

- Connect the USB-device-capable ESP32-S3 or similar board to a USB host.
- Serial monitor output is only for logs. Check HID behavior through the USB
  host connection.
- This library is not designed to run together with Arduino's built-in USB
  device classes.
