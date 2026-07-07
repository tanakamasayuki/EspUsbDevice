# EspUsbDevice Composite (HID + CDC + MSC)

> 日本語版: [README.ja.md](README.ja.md)

Creates a single USB device that is a HID keyboard, a CDC ACM serial port, and a
mass-storage FAT RAM disk at the same time.

This sketch does not use Arduino-ESP32's built-in USB classes. It registers
three `EspUsbDevice` functions with one `EspUsbDevice` instance and calls
`begin()` once; this library owns all of the composite USB descriptors and
endpoints.

## Hardware

- ESP32-S3 or another Arduino-ESP32 board with USB device support
- A USB host such as a PC, another ESP32 running EspUsbHost, or a test fixture
- A separate Serial monitor connection for logs when available

## What It Does

- Brings up HID keyboard + CDC serial + MSC FAT RAM disk as one composite device
- Exposes a removable drive containing `README.TXT`
- Echoes each line received over USB CDC as `echo: ...`
- On a `type <text>` line received over CDC, types `<text>` on the HID keyboard
- Sends a `tick=...` heartbeat to USB CDC every 5 seconds

## Usage

1. Flash the sketch and open the regular Serial monitor.
2. Connect the USB device port to the PC or host.
3. A removable drive appears; open `README.TXT`.
4. Open the newly created serial port on the PC.
5. Send `type Hello` and watch `Hello` get typed into whatever window has focus.
6. Send any other line and observe the `echo: ...` reply.

## Key APIs

- Register each function with the **same** `EspUsbDevice`:
  `EspUsbDeviceHidKeyboard keyboard(device)`,
  `EspUsbDeviceCdcSerial UsbSerial(device)`, `EspUsbDeviceMsc msc(device)`.
- Configure each function (layout, MSC identity, FAT disk contents) before
  `device.begin()`.
- A single `device.begin(config)` enumerates all functions together.
- The library assigns interface numbers and endpoints automatically and builds
  the composite configuration descriptor.

## Notes

- This is the richest composite that fits the ESP32-S3 USB endpoint budget:
  three FIFO-consuming IN endpoints (keyboard, CDC data-in, MSC bulk-in). Adding
  a fourth FIFO-IN class (for example MIDI or bulk Vendor) exceeds the S3 FIFO
  budget and the device fails to enumerate. See
  [../../docs/DESIGN_NOTES.ja.md](../../docs/DESIGN_NOTES.ja.md) ("複合時の
  endpoint 予算の上限"). More endpoints are available on the ESP32-P4.
- The USB Audio class (`EspUsbDeviceAudio`) is exclusive and cannot be combined
  with other classes.
- USB CDC and the logging Serial monitor are separate paths.
- This library is not designed to run together with Arduino's built-in USB
  device classes.

## See Also

- [Keyboard](../Keyboard/) - HID keyboard device
- [Serial](../Serial/) - CDC ACM serial device
- [MSCFatRamDisk](../MSCFatRamDisk/) - FAT RAM disk mass-storage device
