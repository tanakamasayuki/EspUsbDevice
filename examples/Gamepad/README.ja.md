# EspUsbDevice Gamepad

> English: [README.md](README.md)

USB HID gamepad を作り、軸、hat switch、ボタンの状態を Host へ送る例です。

## ハードウェア

- ESP32-S3、ESP32-S2、ESP32-P4 の board。native USB OTG の口を Host へつなぐ
- PC、EspUsbHost を動かす別の ESP32、試験治具などの USB Host
- ログ用の `Serial`（115200）の別の debug 接続

## 動作内容

- 独自の VID/PID/文字列で `EspUsbDevice` を HID gamepad として開始します。
- Host が構成を済ませている間、500 ms ごとに次を送ります。
  - 軸を正弦波に近い表で変える（`rx`/`ry` は `x`/`y` の符号を反転）
  - hat switch を中央と8方向で順に回す
  - ボタン 1〜8 を1つずつ順に押す
- 送った report の要約を表示します。

## 主要 API

- `EspUsbDeviceHidGamepad gamepad(device)` は HID gamepad の Function を登録します。
- `gamepad.send(x, y, z, rz, rx, ry, hat, buttons)` は軸、hat、ボタンの bitmask をまとめて送ります。
- `gamepad.sendReport(report)` は `EspUsbDeviceGamepadReport` をそのまま送ります。
- `gamepad.releaseAll()` は軸、hat、ボタンを中立に戻します。

## Report

- 軸：`x`、`y`、`z`、`rz`、`rx`、`ry`
- 軸の範囲：`-127`〜`127`
- hat：`ESP_USB_DEVICE_GAMEPAD_HAT_CENTER`（方向なし）または8方向
- ボタン：押しているボタン全部の 32 bit の bitmask。ボタン番号ではありません
- report ID：他の HID Function と組み合わせたときは `ESP_USB_DEVICE_HID_REPORT_ID_GAMEPAD`

## hat の定数

| 定数 | 値 | 方向 |
|------|----|------|
| `ESP_USB_DEVICE_GAMEPAD_HAT_CENTER` | `0` | なし（null 状態） |
| `ESP_USB_DEVICE_GAMEPAD_HAT_UP` | `1` | 0° |
| `ESP_USB_DEVICE_GAMEPAD_HAT_UP_RIGHT` | `2` | 45° |
| `ESP_USB_DEVICE_GAMEPAD_HAT_RIGHT` | `3` | 90° |
| `ESP_USB_DEVICE_GAMEPAD_HAT_DOWN_RIGHT` | `4` | 135° |
| `ESP_USB_DEVICE_GAMEPAD_HAT_DOWN` | `5` | 180° |
| `ESP_USB_DEVICE_GAMEPAD_HAT_DOWN_LEFT` | `6` | 225° |
| `ESP_USB_DEVICE_GAMEPAD_HAT_LEFT` | `7` | 270° |
| `ESP_USB_DEVICE_GAMEPAD_HAT_UP_LEFT` | `8` | 315° |

## 想定 Serial 出力

```text
USB gamepad ready
GAMEPAD_TX x=0 y=127 z=0 rz=-127 hat=0 buttons=0x00000001
GAMEPAD_TX x=64 y=64 z=-64 rz=-64 hat=1 buttons=0x00000002
```

## 確認方法

- PC のゲームコントローラー設定画面で軸、hat、ボタンを確かめます。
- EspUsbHost では `onGamepad()` callback で値を確かめます。
- 表示した1段分と `send()` の引数を比べます。

この demo には実物の joystick はありません。つなぐ場合は、値の換算、中央の較正、明示的な中立状態（`releaseAll()`）が必要です。

## v2 からの変更点

- `device.ready()` が true の間だけ送ります。
- API（`send()`、`sendReport()`、`releaseAll()`）と hat の定数は変わらず、wire 上の report の大きさも同じです。
- report descriptor を直しました。hat の logical range を 1〜8 にして null 状態を持たせたので、`HAT_CENTER`（0）は「方向なし」になります。
  v2 では範囲が 0〜8 で、中央が方向として扱われ、8方向も 0〜315° に対応していませんでした（UP が約 39°）。
  それを補正していた Host 側のソフトは確かめ直してください。

## 関連

- [KeyboardMouse](../KeyboardMouse/) - keyboard と mouse の複合 HID
- [MediaKeys](../MediaKeys/) - Consumer Control と System Control
- [HID API ガイド](../../docs/classes/hid.ja.md)
- [全サンプル](../README.md)
