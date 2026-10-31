# ESP32-P4 High-Speed Device

> 日本語版: [README.ja.md](README.ja.md)

Runs a HID keyboard on the ESP32-P4 high-speed Device controller.

- Device controller: `EspUsbController::HighSpeed` (the P4 HS port: endpoints
  0-15, seven non-control IN endpoints, 480 Mbps)
- PHY: UTMI high-speed PHY
- Connector: the board USB connector wired to the UTMI PHY

```cpp
EspUsbDeviceConfig config;
config.controller = EspUsbController::HighSpeed;  // Auto selects it on P4 too
config.vbusSensing = false;                       // see "VBUS" below
device.begin(config);
```

Connector labels, VBUS switches and USB-C CC handling differ by board. Check
the board schematic. The USB Serial/JTAG connector or the GPIO26/GPIO27 FS pair
is not the high-speed Device port.

After enumeration the example types one line and prints the negotiated speed
from `device.speed()` (`EspUsbBusSpeed::High` or `Full`). A full-speed host or
hub may connect to this controller, but the link then negotiates full speed,
and `device.speed()` reports `Full`.

## What changes at high speed

High speed raises bulk packets from 64 to 512 bytes and interrupt endpoints to
1024 bytes. A configuration that starts at full speed can fail at `begin()`
on this controller, so check `begin()` and `device.lastErrorName()`:

| Function | High-speed requirement |
| --- | --- |
| MSC `transferBytes` | a multiple of 512 (64 or 256 fails with `ESP_ERR_INVALID_SIZE`) |
| CCID `messageBytes` | automatic (0) gives 512; an explicit value below 512 fails |
| Borrowed receive (`readDirect`) capacity | a multiple of 512 |
| CDC / Vendor / MIDI `transferBytes` | smaller than 512 is accepted |
| Transmit FIFO (4 KiB, shared) | `begin()` checks every IN endpoint against it; five bulk Vendor Functions at 512 bytes fit, a sixth fails with `ESP_ERR_INVALID_SIZE` |

## VBUS

`config.vbusSensing = false` (the default) makes the controller assume VBUS is
present, which suits boards without a sensing circuit. Set it to `true` only
when the board routes a VBUS-detect signal to this controller; the option does
not add a GPIO or a circuit. On P4 v1.x the session-valid bit of this
port did not reflect VBUS in v3's bench tests, and on the bench one board's HS
port powered the other: follow the board schematic before connecting two
boards port to port.

## Walkthrough and checks

### Before running

Select `esp32p4` (ChipVariant=prev3) for chip revision v1.x or `esp32p4x`
(ChipVariant=postv3) for v3.1 or newer; v3.0 is not supported (both set `USBMode=default`, the OTG controller). Locate the diagnostic
Serial connection. [Example workflow](../../docs/example-workflow.md) explains
installation, connectors, USB roles and the difference between startup and a
usable peer.

### Follow the code

`config.controller` selects HighSpeed before `begin()`. After `device.ready()`,
one greeting is sent; use a scratch editor. The controller selection must
match the wired connector/PHY. A mode change alone does not move PCB traces,
provide VBUS switching or configure USB-C CC hardware.

### API use and adaptation

Distinguish the selected USB controller, its PHY/pads, the PCB connector and
the board's power circuitry. A controller option does not reroute copper
traces. Use the example's declared target/profile and follow the board
schematic for VBUS and USB-C role circuitry. Start with one controller and one
peer before combining Host and Device on separate ports.

### Verify one change at a time

Run the unchanged example first and compare its log with the host. Test the
peer absent and then connected. The error printing is not a retry or recovery
policy.

## Changes from v2

- No TinyUSB port number (rhport 1) is involved; the controller is chosen only
  by `EspUsbDeviceConfig::controller`.
- `config.vbusSensing` is shown explicitly. It replaces the old
  `CFG_TUD_VBUS_DETECT_HW` build flag, which is now a compile error.
- The profiles cover both P4 generations: `esp32p4` (ChipVariant=prev3) and
  `esp32p4x` (ChipVariant=postv3).
- The README lists the high-speed size requirements that v3 checks at
  `begin()`.
- The sketch prints the negotiated speed with the new `device.speed()`.

Compile-checked with Arduino-ESP32 3.3.12 for both profiles; this sketch itself
has not been run on hardware.

- [Sketch](P4HighSpeedDevice.ino)
- [P4FullSpeedDevice](../P4FullSpeedDevice/)
- [All examples](../README.md)
- [USB device guide](../../docs/usb-device-guide.md) (ESP32-specific notes:
  choosing HS or FS on ESP32-P4, what changes at HS, older P4 and P4X)
- [Example workflow](../../docs/example-workflow.md)
