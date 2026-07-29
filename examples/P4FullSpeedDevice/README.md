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
