# ESP32-P4 Full-Speed Device

HID keyboardをESP32-P4のinternal FullSpeed Device controllerで動かします。

- Device controller: `EspUsbController::FullSpeed`
- TinyUSB port: rhport 0
- USB OTG FS既定pin: GPIO26=D-, GPIO27=D+
- swap後のpin: GPIO24=D-, GPIO25=D+

sketchには任意のrouting callをコメントアウトした状態で記載しています。

```cpp
// usb_wrap_ll_phy_select(&USB_WRAP, 0);
```

`device.begin()`より前でコメントを外すとUSB OTG FSがGPIO24/GPIO25へ移り、
USB Serial/JTAGはGPIO26/GPIO27へ移ります。そのため、GPIO24/GPIO25で使用中の
Serial/JTAG接続は切断されることがあります。

このcallが切り替えるのはD-/D+だけです。VBUS給電・検出やUSB-C CC回路は設定しないため、
配線・給電前にboardの回路図を確認してください。
