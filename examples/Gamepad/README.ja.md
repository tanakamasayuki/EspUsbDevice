# EspUsbDevice Gamepad

> English: [README.md](README.md)

USB HID gamepad device を作り、axes、hat switch、button 状態を Host へ送信する例です。

## ハードウェア

- USB device 対応の ESP32-S3 など Arduino-ESP32 board
- PC、EspUsbHost を動かす別 ESP32、またはテスト治具などの USB host
- ログ確認用の Serial monitor 接続

## 動作内容

- `EspUsbDevice` を full-speed HID gamepad device として起動します。
- 500 ms ごとに axes の値を変化させます。
- hat switch を center / 8 方向に順番に変化させます。
- button 1 から 8 を順番に押した状態として送信します。

## 主要 API

- `EspUsbDeviceHidGamepad gamepad(device)` は HID gamepad function を登録します。
- `gamepad.send(x, y, z, rz, rx, ry, hat, buttons)` は axes、hat、button bitmask をまとめて送信します。
- `gamepad.sendReport(report)` は raw `EspUsbDeviceGamepadReport` を送信します。
- `gamepad.releaseAll()` は axes、hat、button を neutral 状態へ戻します。

## Report

- axes: `x`、`y`、`z`、`rz`、`rx`、`ry`
- axis range: `-127` から `127`
- hat: `ESP_USB_DEVICE_GAMEPAD_HAT_CENTER` または 8 方向
- buttons: 32 bit bitmask
- report ID: `ESP_USB_DEVICE_HID_REPORT_ID_GAMEPAD`

## Hat Constants

| 定数 | 値 |
|------|----|
| `ESP_USB_DEVICE_GAMEPAD_HAT_CENTER` | `0` |
| `ESP_USB_DEVICE_GAMEPAD_HAT_UP` | `1` |
| `ESP_USB_DEVICE_GAMEPAD_HAT_UP_RIGHT` | `2` |
| `ESP_USB_DEVICE_GAMEPAD_HAT_RIGHT` | `3` |
| `ESP_USB_DEVICE_GAMEPAD_HAT_DOWN_RIGHT` | `4` |
| `ESP_USB_DEVICE_GAMEPAD_HAT_DOWN` | `5` |
| `ESP_USB_DEVICE_GAMEPAD_HAT_DOWN_LEFT` | `6` |
| `ESP_USB_DEVICE_GAMEPAD_HAT_LEFT` | `7` |
| `ESP_USB_DEVICE_GAMEPAD_HAT_UP_LEFT` | `8` |

## 確認方法

- PC の game controller 設定画面で axes / hat / buttons を確認します。
- EspUsbHost では `onGamepad()` callback で値を確認できます。
- Serial monitor には送信した report の概要が出ます。

## 関連

- [KeyboardMouse](../KeyboardMouse/) - keyboard と mouse の composite HID
- [Mouse](../Mouse/) - HID mouse device
