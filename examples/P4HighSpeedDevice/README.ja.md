# ESP32-P4 High-Speed Device

> English: [README.md](README.md)

HID keyboardをESP32-P4のHighSpeed Device controllerで動かします。

- Device controller: `EspUsbController::HighSpeed`
- TinyUSB port: rhport 1
- PHY: external UTMI High-Speed PHY
- 接続先: board上でUTMI PHYへ配線されたUSB connector

connector名、VBUS switch、USB-C CC処理はboardごとに異なるため、回路図を確認してください。
USB Serial/JTAG connectorやGPIO26/GPIO27のFS pairはHigh-Speed Device portではありません。

列挙後に1行だけkeyboard入力します。FS Hostへ接続した場合、このcontrollerを使っていても
link自体はFull Speedでネゴします。
