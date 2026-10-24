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

## 読み方と確認の手順

### 実行前の準備

軸・hat・ボタンの全状態を生成してゲームパッドへ送る。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

ホストのゲームコントローラ画面を開き、各stepとsend()の引数を比べます。ボタン値は番号ではなく保持中の全ボタンのbitmapです。物理ジョイスティックは使っておらず、追加する場合は範囲の換算・中央校正・中立を定義します。

### APIの使い方と改造時の注意

HID reportは構造化された状態や変化であり、任意のテキスト一行ではありません。report descriptorと合わせて、report ID・usage page/usage・符号・数値範囲を読みます。キーボードはキー位置、マウス移動は相対値、ボタンはbitmapです。実際の操作へつなぐ前に、押下と解放の対、または全状態の比較を確認します。

callbackのreportは借りたデータです。別taskへ渡す場合はbyte列や必要な値を自分の領域へコピーします。callbackでは短い診断に留め、生dumpの連続表示で端末速度が律速になることにも注意します。入力が来なければ、列挙、descriptor・report長、アプリ側の解釈の順に調べ、一度に全部を変えないようにします。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](Gamepad.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
