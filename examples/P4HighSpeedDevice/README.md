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
