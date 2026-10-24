# ESP32-P4 High-Speed Device

> 日本語版: [README.ja.md](README.ja.md)

Runs a HID keyboard on the ESP32-P4 high-speed Device controller.

- Device controller: `EspUsbController::HighSpeed`
- TinyUSB port: rhport 1
- PHY: external UTMI high-speed PHY
- Connector: the board USB connector wired to the UTMI PHY

Connector labels, VBUS switches, and USB-C CC handling differ by board. Check
the board schematic. The USB Serial/JTAG connector or GPIO26/GPIO27 FS pair is
not the high-speed Device port.

After enumeration the example types one line. A full-speed host may connect to
this controller, but the resulting link then negotiates full speed.

## Walkthrough and checks

### Before running

Start the P4 HighSpeed controller as a USB keyboard. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

config.controller selects HighSpeed before `begin()`. After `device.ready()`, one greeting is sent; use a scratch editor. The controller selection must match the wired connector/PHY. A mode change alone does not move PCB traces, provide VBUS switching or configure USB-C CC hardware.

### API use and adaptation

Distinguish the selected USB controller, its PHY/pads, the PCB connector and the board's power circuitry. A controller option does not reroute copper traces. Plan a working upload/debug route before testing a change that moves Serial/JTAG; inspect logs through an independent UART if necessary.

Use the example's declared target/profile and follow the board schematic for VBUS and USB-C role circuitry. A runtime PHY selection is different from a permanent chip configuration. Start with one controller and one peer before combining Host and Device on separate ports.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](P4HighSpeedDevice.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
