# ESP32-P4 Full-Speed Device

> 日本語版: [README.ja.md](README.ja.md)

Runs a HID keyboard on the ESP32-P4 internal full-speed Device controller.

- Device controller: `EspUsbController::FullSpeed` (the P4 FS port: endpoints
  0-6, four non-control IN endpoints)
- PHY: internal full-speed PHY
- Default USB OTG FS pins: GPIO26=D-, GPIO27=D+
- Optional swapped pins: GPIO24=D-, GPIO25=D+

```cpp
EspUsbDeviceConfig config;
config.controller = EspUsbController::FullSpeed;  // Auto would pick HS on P4
config.vbusSensing = false;                       // see "VBUS" below
device.begin(config);
```

On P4, `EspUsbController::Auto` (the default) selects the high-speed
controller, so this example names the full-speed one. `begin()` fails with
`ESP_ERR_NOT_SUPPORTED` if the requested controller does not exist.

## Optional pin swap

The sketch contains the optional routing call in commented-out form:

```cpp
// usb_wrap_ll_phy_select(&USB_WRAP, 0);
```

Uncomment it before `device.begin()` to move USB OTG FS to GPIO24/GPIO25.
USB Serial/JTAG moves to GPIO26/GPIO27, so the original Serial/JTAG connection
may disappear. Plan another upload/debug route (a UART bridge or the BOOT
button) before trying it.

This is an ESP-IDF HAL call (`hal/usb_wrap_ll.h`), not an EspUsbDevice API, and
it only routes D-/D+. It does not configure VBUS power/sensing or USB-C CC
circuitry. Check the board schematic before wiring or powering the port.

## VBUS

`config.vbusSensing = false` (the default) makes the controller assume VBUS is
present, which suits boards without a sensing circuit. Set it to `true` only
when the board routes a VBUS-detect signal to this controller; the option does
not add a GPIO or a circuit. Power the port as the board schematic describes;
a USB-C connector needs its CC resistors to receive VBUS from a host.

## Operation

After enumeration the example types one line, `Hello from the ESP32-P4 FS
device`, into whatever has focus on the host. Use a scratch editor.

## Walkthrough and checks

### Before running

Select `esp32p4` (ChipVariant=prev3) for chip revision v1.x or `esp32p4x`
(ChipVariant=postv3) for v3.1 or newer; v3.0 is not supported (both set `USBMode=default`, the OTG controller). Locate the diagnostic
Serial connection: on many P4 boards it is the USB Serial/JTAG port, which the
pin swap above moves. [Example workflow](../../docs/example-workflow.md)
explains installation, connectors, USB roles and the difference between
startup and a usable peer.

### Follow the code

`config.controller` selects FullSpeed before `begin()`. After `device.ready()`,
one greeting is sent. The controller selection must match the wired
connector/PHY. A mode change alone does not move PCB traces, provide VBUS
switching or configure USB-C CC hardware.

### API use and adaptation

Distinguish the selected USB controller, its PHY/pads, the PCB connector and
the board's power circuitry. A controller option does not reroute copper
traces. Use the example's declared target/profile and follow the board
schematic for VBUS and USB-C role circuitry. A runtime PHY selection is
different from a permanent chip configuration. Start with one controller and
one peer before combining Host and Device on separate ports.

### Verify one change at a time

Run the unchanged example first and compare its log with the host. Test the
peer absent and then connected. Change one pin or setting at a time. The error
printing is not a retry or recovery policy.

## Changes from v2

- No TinyUSB port number (rhport 0) is involved; the controller is chosen only
  by `EspUsbDeviceConfig::controller`.
- `config.vbusSensing` is shown explicitly. It replaces the old
  `CFG_TUD_VBUS_DETECT_HW` build flag, which is now a compile error.
- The profiles cover both P4 generations: `esp32p4` (ChipVariant=prev3) and
  `esp32p4x` (ChipVariant=postv3).
- The pin-swap comment in the sketch is complete again (the v2 sketch had a
  truncated line there).

Compile-checked with Arduino-ESP32 3.3.12 for both profiles, with the swap
call commented out and, separately, enabled. Neither this sketch nor the pin
swap under v3 has been run on hardware.

- [Sketch](P4FullSpeedDevice.ino)
- [P4HighSpeedDevice](../P4HighSpeedDevice/)
- [All examples](../README.md)
- [USB device guide](../../docs/usb-device-guide.md) (ESP32-specific notes:
  choosing HS or FS on ESP32-P4, older P4 and P4X)
- [Example workflow](../../docs/example-workflow.md)
