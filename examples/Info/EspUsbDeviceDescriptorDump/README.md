# EspUsbDeviceDescriptorDump

> 日本語版: [README.ja.md](README.ja.md)

Prints **every descriptor the library actually built** from the classes you
registered.

On the host side you read descriptors to find out what a device is. On the
device side you are the one writing them, so the useful question is the reverse:
what did I just declare, and does it fit this controller?

## Hardware

- An Arduino-ESP32 board with USB device support (ESP32-S2 / S3 / P4)
- A Serial monitor connection

No USB host is needed. The descriptors are built inside `device.begin()`, so the
whole dump is available without plugging a cable into a host.

## What It Does

Select the class set with the `DUMP_ENABLE_*` defines at the top of the sketch,
then read:

- DEVICE descriptor (hex plus decoded fields)
- CONFIGURATION descriptor, full-speed and high-speed variants (hex plus a
  block-by-block walk)
- DEVICE QUALIFIER and OTHER SPEED CONFIGURATION
- BOS and Microsoft OS 2.0 descriptors (when WebUSB is enabled)
- HID report descriptors
- String descriptors
- The **endpoint budget**, compared against this target's controller limits

```c
#define DUMP_ENABLE_KEYBOARD 1
#define DUMP_ENABLE_MOUSE    0
#define DUMP_ENABLE_GAMEPAD  0
#define DUMP_ENABLE_CDC      1
#define DUMP_ENABLE_MIDI     0
#define DUMP_ENABLE_VENDOR   0
#define DUMP_ENABLE_WEBUSB   0
```

Before building a composite device, switch the set here, rebuild, and read the
budget line - it is the fastest way to find out whether the combination fits.
`EspUsbDevice` holds at most four classes; a fifth object's constructor calls
`addClass()`, which fails and **registers nothing, silently**.

## Endpoint Budget

Per-controller limits. A configuration that exceeds them is rejected by
`begin()` with `ESP_ERR_INVALID_SIZE`, before the PHY is started.

| Controller | Max endpoint number | Non-control IN | Non-control OUT |
|---|---|---|---|
| ESP32-S2 / S3 | 5 | 4 | 5 |
| ESP32-P4 rhport 0 (FullSpeed) | 6 | 4 | 6 |
| ESP32-P4 rhport 1 (HighSpeed) | 15 | 7 | 15 |

On ESP32-P4 the limits follow `config.controller`, so the same class set can fit
one port and not the other. The budget printed by this sketch follows
`device.config().controller` the same way.

## Cross-Checking Against The Host

The CONFIGURATION hex printed here and the bytes the host received must match
byte for byte.

```sh
# Linux
lsusb -v -d 303a:4051

# Any OS, via PyUSB
cd tests && uv run --with pyusb python manual/device_inspect/device_inspect.py --pid 0x4051
```

If they differ - or if the host shows nothing at all - the descriptor never
reached the bus, and the problem is below this layer (enumeration, electrical,
controller selection). Go back to
[EspUsbDeviceBringUpCheck](../EspUsbDeviceBringUpCheck/).

## Expected Serial Output

```text
=== EspUsbDevice descriptor dump ===
BEGIN ok error=ESP_OK
--- DEVICE descriptor (18 bytes) ---
  0000  12 01 00 02 00 00 00 40 3a 30 51 40 00 01 01 02
  0010  03 01
  bcdUSB=0x0200 class=0x00 (per-interface) subclass=0x00 protocol=0x00 ep0_mps=64
  idVendor=0x303a idProduct=0x4051 bcdDevice=0x0100 configurations=1
--- CONFIGURATION descriptor (full-speed) (98 bytes) ---
  ...
  0000  CONFIGURATION total=98 interfaces=3 value=1 attributes=0x80 power=100mA
  0009  INTERFACE  number=0 alt=0 endpoints=1 class=0x03 (HID) subclass=0x01 protocol=0x01
  0012  HID        bcdHID=0x0111 descriptors=1 report_descriptor=65 bytes
  ...
--- endpoint budget ---
  controller           S2/S3 full-speed
  highest ep number    3 / 5
  non-control IN       3 / 4
  non-control OUT      1 / 5
=== end of dump ===
```

## Related

- [EspUsbDeviceBringUpCheck](../EspUsbDeviceBringUpCheck/) - run this one first
- [tests/manual/device_inspect](../../../tests/manual/device_inspect/) - the same descriptors as the host sees them
- [docs/usb-device-guide.md](../../../docs/usb-device-guide.md) - how to design the descriptors
