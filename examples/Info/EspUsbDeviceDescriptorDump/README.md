# EspUsbDeviceDescriptorDump

> 日本語版: [README.ja.md](README.ja.md)

Prints **every descriptor the library actually built** from the Functions you
registered.

On the host side you read descriptors to find out what a device is. On the
device side you are the one writing them, so the useful question is the reverse:
what did I just declare, and does it fit this controller?

## Hardware

- An Arduino-ESP32 board with USB device support (ESP32-S2 / S3 / P4)
- A Serial monitor connection

No USB host is needed. The descriptors are built inside `device.begin()`, so the
whole dump is available without plugging a cable into a host. With
`DUMP_DESCRIPTORS_ONLY 1` the controller is not even started
(`EspUsbDeviceStartMode::DescriptorsOnly`).

## What It Does

Select the Function set with the `DUMP_ENABLE_*` defines at the top of the
sketch, then read:

- DEVICE descriptor (hex plus decoded fields)
- CONFIGURATION descriptor, full-speed and high-speed variants (hex plus a
  block-by-block walk, including interface and IAD string indices)
- DEVICE QUALIFIER and OTHER SPEED CONFIGURATION
- BOS, WebUSB URL and Microsoft OS 2.0 descriptors, with the MS OS 2.0 vendor
  revision and layout
- The HID report descriptor (several HID Functions are merged into one)
- Every string descriptor that exists, including Function names
- The **endpoint budget**, compared against this target's controller limits,
  and which bulk IN endpoints got a double-buffered FIFO

```c
#define DUMP_ENABLE_KEYBOARD 1
#define DUMP_ENABLE_MOUSE    0
#define DUMP_ENABLE_GAMEPAD  0
#define DUMP_ENABLE_CDC      1
#define DUMP_ENABLE_MIDI     0
#define DUMP_ENABLE_VENDOR   0
#define DUMP_ENABLE_WEBUSB   0
#define DUMP_DESCRIPTORS_ONLY 0
```

Before building a composite device, switch the set here, rebuild, and read the
budget line - it is the fastest way to find out whether the combination fits.
There is no fixed limit on the number of Functions; the endpoint and FIFO
budget decides, and `begin()` reports a set that does not fit.

## Endpoint Budget

Per-controller limits. A configuration that exceeds them is rejected by
`begin()` with `ESP_ERR_INVALID_SIZE`, before the PHY is started.

| Controller | Max endpoint number | Non-control IN | Non-control OUT |
|---|---|---|---|
| ESP32-S2 / S3 | 6 | 4 | 6 |
| ESP32-P4 full-speed port | 6 | 4 | 6 |
| ESP32-P4 high-speed port | 15 | 7 | 15 |

On ESP32-P4 the limits follow `config.controller` (`Auto` selects the
high-speed port), so the same Function set can fit one port and not the other.
The budget printed by this sketch follows `device.config().controller` the same
way.

## Strings

Index 1-3 are manufacturer / product / serial number, 4 is reserved, and
Function names (for example the CDC port's `"Dump CDC"`) and class-specific
strings follow from 5. Only indices that exist are printed; non-ASCII UTF-16
units are shown as `\uXXXX`.

## Cross-Checking Against The Host

The CONFIGURATION hex printed here and the bytes the host received must match
byte for byte.

```sh
# Linux
lsusb -v -d 303a:4051

# Any OS, via PyUSB (the v2 test tool still reads standard descriptors)
# device_inspect.py: https://github.com/tanakamasayuki/EspUsbDevice/tree/3fed50d0d777f600432b71381faccd93adb9a202/tests/manual/device_inspect
uv run --with pyusb python device_inspect.py --pid 0x4051
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
  0000  12 01 00 02 ef 02 01 40 3a 30 51 40 00 01 01 02
  0010  03 01
  bcdUSB=0x0200 class=0xef (Miscellaneous) subclass=0x02 protocol=0x01 ep0_mps=64
  idVendor=0x303a idProduct=0x4051 bcdDevice=0x0100 configurations=1
--- CONFIGURATION descriptor (full-speed) (... bytes) ---
  ...
  0009  INTERFACE  number=0 alt=0 endpoints=1 class=0x03 (HID) subclass=0x01 protocol=0x01 string=0
  0012  HID        bcdHID=0x0111 descriptors=1 report_descriptor=... bytes
  ...
--- string descriptors ---
  [0] langid=0x0409
  [1] "EspUsb"
  [2] "EspUsbDevice DescriptorDump"
  [3] "espusb-descdump"
  [6] "Dump CDC"
--- endpoint budget ---
  controller           S2/S3 full-speed
  highest ep number    3 / 6
  non-control IN       3 / 4
  non-control OUT      1 / 6
  double-buffered IN   mask=0x....
=== end of dump ===
```

The exact sizes and endpoint numbers depend on the selected set; read them from
your own output.

## Related

- [EspUsbDeviceBringUpCheck](../EspUsbDeviceBringUpCheck/) - run this one first
- [docs/usb-device-guide.md](../../../docs/usb-device-guide.md) - how to design the descriptors
- [Core API (v3)](../../../docs/usb-device-advanced.md) - controllers, endpoint limits and FIFO
- [Text descriptors (v3)](../../../docs/usb-device-advanced.md)

## Walkthrough and checks

### Before running

Inspect descriptors generated by the declared USB Functions. Follow the hardware requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Change one DUMP_ENABLE option at a time, rebuild, then compare configuration length, interfaces, endpoints and the HID report descriptor. This inspects what the device offers; it does not prove every endpoint or class has exchanged real data.

### API use and adaptation

Treat startup, enumeration, interface discovery and actual class communication as separate checkpoints. A descriptor says what a device advertises; it does not prove a transfer completed or that a protocol is implemented. Keep received lengths and capacity checks when adapting a descriptor walker. An interface number, endpoint address, USB address and physical port identify different things.

Large dumps are diagnostic work, not a pattern for high-rate callbacks. Preserve the error name and the configuration that produced it.

### Verify one change at a time

Run the unchanged example first and compare its output with the host's view. Reset to the original configuration before changing one Function or setting. The demonstration's error logging is not a general retry or recovery policy.

## Changes from v2

- The endpoint limits follow v3's controller table: endpoint numbers up to 6
  and OUT up to 6 on S2/S3 (v2 listed 5 and 5).
- The four-class limit is gone; the endpoint/FIFO budget decides.
- The HID report descriptor is read with `hidReportDescriptorLength()`, so its
  length no longer has to be parsed from the HID class descriptor.
- Strings are listed by index from the library's layout (Function names from 5)
  instead of a fixed "class" string at index 4.
- BOS is present with a Vendor or DFU Function even without WebUSB (Microsoft
  OS 2.0 binds WinUSB). The dump also shows the WebUSB URL descriptor, the MS OS
  2.0 vendor revision and layout, and the bulk IN double-buffer mask.
- `DUMP_DESCRIPTORS_ONLY` uses `startMode = DescriptorsOnly` to build the
  descriptors without starting the controller.
- The WebUSB URL includes `https://`.

- [Sketch](EspUsbDeviceDescriptorDump.ino)
- [All examples](../../README.md)
- [Example workflow](../../../docs/example-workflow.md)
