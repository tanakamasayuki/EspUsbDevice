# ESP32-P4 Full-Speed Device

> 日本語版: [README.ja.md](README.ja.md)

Runs a HID keyboard on the ESP32-P4 internal full-speed Device controller.

- Device controller: `EspUsbController::FullSpeed`
- TinyUSB port: rhport 0
- Default USB OTG FS pins: GPIO26=D-, GPIO27=D+
- Optional swapped pins: GPIO24=D-, GPIO25=D+

The sketch contains the optional routing call in commented-out form:

```cpp
// usb_wrap_ll_phy_select(&USB_WRAP, 0);
```

Uncomment it before `device.begin()` to move USB OTG FS to GPIO24/GPIO25.
USB Serial/JTAG moves to GPIO26/GPIO27, so the original Serial/JTAG connection
may disappear.

This call only routes D-/D+. It does not configure VBUS power/sensing or USB-C
CC circuitry. Check the board schematic before wiring or powering the port.

## Walkthrough and checks

### Before running

Start the P4 FullSpeed controller as a USB keyboard. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

config.controller selects FullSpeed before `begin()`. After `device.ready()`, one greeting is sent; use a scratch editor. The controller selection must match the wired connector/PHY. A mode change alone does not move PCB traces, provide VBUS switching or configure USB-C CC hardware.

### API use and adaptation

Distinguish the selected USB controller, its PHY/pads, the PCB connector and the board's power circuitry. A controller option does not reroute copper traces. Plan a working upload/debug route before testing a change that moves Serial/JTAG; inspect logs through an independent UART if necessary.

Use the example's declared target/profile and follow the board schematic for VBUS and USB-C role circuitry. A runtime PHY selection is different from a permanent chip configuration. Start with one controller and one peer before combining Host and Device on separate ports.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](P4FullSpeedDevice.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
