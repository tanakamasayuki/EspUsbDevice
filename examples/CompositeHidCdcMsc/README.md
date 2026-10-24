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

- This reaches the ESP32-S3 limit of four non-control IN endpoints: keyboard,
  CDC notification, CDC data-in, and MSC bulk-in. Duplex data endpoints share
  one endpoint number between OUT and IN. Adding MIDI or bulk Vendor exceeds
  the S3 IN limit. See
  [../../docs/DESIGN_NOTES.ja.md](../../docs/DESIGN_NOTES.ja.md) ("複合時の
  endpoint 予算の上限"). More endpoints are available on the ESP32-P4.
- USB Audio uses the same composite descriptor builder. UAC1 is the default and
  UAC2 is explicit; both are subject to descriptor capacity and the target's
  endpoint budget.
- USB CDC and the logging Serial monitor are separate paths.
- This library is not designed to run together with Arduino's built-in USB
  device classes.

## See Also

- [Keyboard](../Keyboard/) - HID keyboard device
- [Serial](../Serial/) - CDC ACM serial device
- [MSCFatRamDisk](../MSCFatRamDisk/) - FAT RAM disk mass-storage device

## Walkthrough and checks

### Before running

Combine a CDC command port, keyboard and FAT RAM disk. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

One device owns all three functions. Open its CDC port and use the documented type command while a scratch editor has focus; inspect the separate disk in the file manager. The lineBuffer bounds input and its overflow policy should be reviewed before accepting long commands in an application.

### API use and adaptation

A block is addressed by LBA and sized by the medium; a file needs a filesystem on top. Compare capacity, mount success and file operations separately. Check returned byte counts, close handles, and allow outstanding host writes to finish before removing or reusing storage. Neither USB connection nor a FAT signature guarantees a valid filesystem.

Give the medium one writer at a time. A host-mounted volume and the sketch must not independently edit the same filesystem. RAM examples lose changes on reset; physical-media examples can change persistent data. Read the operation list before using a medium containing files you want to retain.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](CompositeHidCdcMsc.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
