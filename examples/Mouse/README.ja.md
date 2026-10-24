# EspUsbDevice Mouse

> English: [README.md](README.md)

USB HID boot mouse device を作り、相対移動、wheel、button 状態を USB host へ送信する例です。

## ハードウェア

- USB device 対応の ESP32-S3 など Arduino-ESP32 board
- PC、EspUsbHost を動かす別 ESP32、またはテスト治具などの USB host
- ログ確認用の Serial monitor 接続

## 動作内容

- `EspUsbDevice` を full-speed HID mouse として起動します。
- 相対 `x` / `y` 移動量を送信します。
- wheel 移動量を送信します。
- Device 側で mouse button 状態を保持し、Host 側の `buttons` / `previousButtons`
  event field と対応しやすい press/release API を使います。
- left button の press / release を送信します。

## 主要 API

- `mouse.move(x, y)` は相対移動を送信します。
- `mouse.move(x, y, wheel, buttons)` は boot mouse report 全体を送信し、
  現在の button 状態を置き換えます。
- `mouse.wheel(delta)` は現在の button 状態を維持して wheel 移動を送信します。
- `mouse.press(buttons)` は button bit を立てて新しい状態を送信します。
- `mouse.release(buttons)` は button bit を下げて新しい状態を送信します。
- `mouse.releaseAll()` はすべての button bit を下げます。
- `mouse.click(button)` は press、delay、release を送信します。
- `mouse.buttons()` は Device 側が保持している現在の button bitmask を返します。
- `mouse.sendReport(report)` は raw boot mouse report を送信します。

## Button Bits

| 定数 | bit |
|------|-----|
| `ESP_USB_DEVICE_MOUSE_LEFT` | `0x01` |
| `ESP_USB_DEVICE_MOUSE_RIGHT` | `0x02` |
| `ESP_USB_DEVICE_MOUSE_MIDDLE` | `0x04` |
| `ESP_USB_DEVICE_MOUSE_BACK` | `0x08` |
| `ESP_USB_DEVICE_MOUSE_FORWARD` | `0x10` |

## 想定 Serial 出力

```text
USB mouse ready
MOUSE_FAILED error=ESP_ERR_TIMEOUT
```

この example は失敗時だけ Serial に出力します。移動や button の挙動は USB host 側で確認してください。

## 関連

- [KeyboardMouse](../KeyboardMouse/) - keyboard と mouse の composite device
- [Keyboard](../Keyboard/) - HID keyboard device

## 読み方と確認の手順

### 実行前の準備

ポインタの相対移動・wheel・ボタンの変化を送る。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

移動が自動で始まるため試験用の領域で使います。`move()`は画面座標ではなく移動差分で、`press()`した状態はrelease()まで続きます。この例は失敗を表示しますが汎用の解放待ちqueueは持たないため、実用途では現在のボタン状態を明示的に回復させます。

### APIの使い方と改造時の注意

HID reportは構造化された状態や変化であり、任意のテキスト一行ではありません。report descriptorと合わせて、report ID・usage page/usage・符号・数値範囲を読みます。キーボードはキー位置、マウス移動は相対値、ボタンはbitmapです。実際の操作へつなぐ前に、押下と解放の対、または全状態の比較を確認します。

callbackのreportは借りたデータです。別taskへ渡す場合はbyte列や必要な値を自分の領域へコピーします。callbackでは短い診断に留め、生dumpの連続表示で端末速度が律速になることにも注意します。入力が来なければ、列挙、descriptor・report長、アプリ側の解釈の順に調べ、一度に全部を変えないようにします。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](Mouse.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
