# EspUsbDevice Composite (HID + CDC + MSC)

> 日本語版: [README.ja.md](README.ja.md)

Creates a single USB device that is a HID keyboard, a CDC ACM serial port and a
mass-storage FAT RAM disk at the same time.

This sketch does not use Arduino-ESP32's built-in USB classes. It registers
three Functions with one `EspUsbDevice`, configures each one, and calls
`begin()` once; this library owns all of the composite USB descriptors and
endpoints.

```cpp
EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceCdcSerial console(device, "Composite console");
EspUsbDeviceMsc msc(device);

console.configure(serialConfig);   // receive / transmit ring sizes
msc.configure(mscConfig);          // transfer staging size
disk.attach(msc);                  // FAT RAM disk: geometry and callbacks
device.begin(identity);            // one begin() for all three
```

## Hardware

- ESP32-S3 or another Arduino-ESP32 board with USB device support (ESP32-S2,
  ESP32-P4)
- A USB host such as a PC, another ESP32 running EspUsbHost, or a test fixture
- A separate Serial monitor connection for logs when available

## What It Does

- Brings up HID keyboard + CDC serial + MSC FAT RAM disk as one composite device
- Exposes a 48 KiB removable drive containing `README.TXT`
- Echoes each line received over USB CDC as `echo: ...`
- On a `type <text>` line received over CDC, types `<text>` on the HID keyboard
- Sends a `tick=...` heartbeat to USB CDC every 5 seconds while a terminal has
  the port open
- Prints the CDC DTR/RTS changes to the debug Serial

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
  `EspUsbDeviceCdcSerial console(device, name)`, `EspUsbDeviceMsc msc(device)`.
- Configure each function before `device.begin()`: the keyboard layout, the CDC
  ring sizes with `console.configure()`, the MSC identity, staging size with
  `msc.configure()` and the FAT disk contents.
- `EspUsbDeviceMscFatRamDisk::format()` / `addTextFile()` build the volume;
  `attach(msc)` installs its geometry and callbacks. `mediaPresent(true)` and
  `isWritable(true)` are set explicitly.
- A single `device.begin(config)` enumerates all functions together. The
  library assigns interface numbers and endpoints and builds the composite
  configuration descriptor.
- `console.onLineState()` reports DTR/RTS; `console.connected()` is true while
  DTR is set.

## Notes

- This reaches the ESP32-S2/S3 limit of four non-control IN endpoints:
  keyboard, CDC notification, CDC data-in and MSC bulk-in. Duplex data
  endpoints share one endpoint number between OUT and IN. Adding MIDI or bulk
  Vendor exceeds the IN limit, and `device.begin()` then fails with
  `ESP_ERR_INVALID_SIZE`. The ESP32-P4 high-speed port has seven. See
  the controller table in the
  [advanced guide](../../docs/usb-device-advanced.md) (endpoint numbers and FIFOs).
- MSC `transferBytes` must be a multiple of the bulk packet: 512 works at full
  and high speed; 64 or 256 fails on the P4 high-speed port.
- The 48 KiB disk keeps the static RAM within the ESP32-S2 budget. Its contents
  are lost on reset. The host and the sketch must not edit the filesystem at
  the same time.
- USB CDC and the logging Serial monitor are separate paths.
- This library is not designed to run together with Arduino's built-in USB
  device classes.

## See Also

- [Keyboard](../Keyboard/) - HID keyboard device
- [CdcConsole](../CdcConsole/) - CDC ACM serial device
- [MscCdcBlocks](../MscCdcBlocks/) - raw RAM blocks with MSC and CDC
- [MSCFatRamDisk](../MSCFatRamDisk/) -
  the v2 FAT RAM disk example

## Walkthrough and checks

### Before running

Select the board's profile (`esp32s3` is the default; `esp32s2`, `esp32p4`
for P4 v1.x and `esp32p4x` for P4 v3.1 or newer; v3.0 is not supported), and locate the
diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md)
explains installation, connectors, USB roles and the difference between
startup and a usable peer.

### Follow the code

One device owns all three functions. Open its CDC port and use the `type`
command while a scratch editor has focus; inspect the disk in the file
manager. `line` bounds the input and silently cuts longer lines; review that
policy before accepting long commands in an application.

### API use and adaptation

A block is addressed by LBA and sized by the medium; a file needs a filesystem
on top. Compare capacity, mount success and file operations separately. Allow
outstanding host writes to finish before removing or reusing storage. Neither
USB connection nor a FAT signature guarantees a valid filesystem.

Give the medium one writer at a time. A host-mounted volume and the sketch must
not independently edit the same filesystem. RAM examples lose changes on reset.

### Verify one change at a time

Run the unchanged example first, then change one Function or size at a time.
The error printing is not a retry or recovery policy.

## Changes from v2

- The CDC rings and the MSC staging buffer are sized per Function with
  `configure()` instead of build flags.
- `configure()` and `onLineState()` return `esp_err_t`; the sketch checks them
  and prints the reason, and prints `msc.startError()` if `attach()` fails.
- The FAT disk's `addTextFile()` result is checked as well.
- CDC `print()`/`printf()` and `flush()` wait up to the TX timeout (1000 ms by
  default) for room and completion; `console.setTxTimeoutMs(0)` makes them
  return at once with what fitted.

Compile-checked with Arduino-ESP32 3.3.12 for every profile in
`sketch.yaml`; this sketch itself has not been run on hardware.

- [Sketch](CompositeHidCdcMsc.ino)
- [All examples](../README.md)
- [HID](../../docs/classes/hid.md), [CDC](../../docs/classes/cdc.md),
  [MSC](../../docs/classes/msc.md) API
- [Example workflow](../../docs/example-workflow.md)
