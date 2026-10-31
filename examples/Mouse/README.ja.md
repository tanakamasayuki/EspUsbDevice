# EspUsbDevice Mouse

> English: [README.md](README.md)

USB HID boot mouse を作り、相対移動、wheel、ボタンの状態を USB Host へ送る例です。

## ハードウェア

- ESP32-S3、ESP32-S2、ESP32-P4 の board。native USB OTG の口を Host へつなぐ
- PC、EspUsbHost を動かす別の ESP32、試験治具などの USB Host
- ログ用の `Serial`（115200）の別の debug 接続

## 動作内容

- 独自の VID/PID/文字列で `EspUsbDevice` を HID mouse として開始します。
- Host が構成を済ませた後、1秒ごとに1段ずつ、右・下・左・上（各 30）、wheel +1、左ボタンの押下と解放を送ります。
- ボタンの状態を Device 側で保持するので、press/release の呼び出しが Host 側 event の `buttons` と `previousButtons` に対応します。
- 失敗したときは `device.lastErrorName()` と現在のボタンの bitmask を表示します。

Host が構成を済ませるとすぐに動き始めます。動いても困らない画面で試してください。

## 主要 API

- `mouse.move(x, y)` は相対移動を送ります。`press()` で押したボタンは押したままなので、`press()` の後の `move()` はドラッグになります。
- `mouse.move(x, y, wheel, buttons)` で `buttons` に 0〜255 を渡すと、`press()`/`release()` と同じように押下中のボタンを決めます。
  既定値 `EspUsbDeviceHidMouse::KeepButtons`（-1）は今のボタンを保ちます。-1〜255 以外は `ESP_ERR_INVALID_ARG` で失敗します。
- `mouse.wheel(delta)` は今のボタンを保ったまま wheel を送ります。
- `mouse.press(buttons)` はボタンの bit を立てて新しい状態を送ります。
- `mouse.release(buttons)` はボタンの bit を落として新しい状態を送ります。
- `mouse.releaseAll()` はすべてのボタンを離します。
- `mouse.click(button)` は押下、短い保持、解放を送ります。
- `mouse.buttons()` は Device 側のボタンの bitmask を返します。
- `mouse.sendReport(report)` は `EspUsbDeviceBootMouseReport` をそのまま送ります。`mouse.sendState()` は移動 0 で今のボタンを送り直します。

report protocol では 4 byte（ボタン、X、Y、wheel）、boot protocol では 3 byte の boot 形式に変換して送ります。
X/Y/wheel は送信が成功したら 0 に戻るので、Host の idle 再送で同じ移動が繰り返されません。

## ボタンの bit

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
MOUSE_FAILED ESP_ERR_TIMEOUT buttons=0x00
```

表示するのは失敗だけです。移動とボタンは Host 側で確かめます。

## 確認の手順

- `move()` に渡すのは差分で、画面の座標ではありません。
- `press()` したボタンは `release()` まで押されたままです。この例は押下が失敗しても解放を送りますが、
  汎用の「解放待ち」の仕組みはないので、アプリ側でボタンの状態を回復してください。
- true は「コピーして送信を始めた」という意味で、Host が読んだことは意味しません。

## v2 からの変更点

- `device.ready()` が true の間だけ送ります。Host が無いときに毎秒失敗を表示することはなくなりました。
- 押下が失敗しても解放を送り、失敗の表示に `mouse.buttons()` を加えました。
- `buttons` を省いた `move(x, y)` は、押下中のボタンを保つようになりました。v2 は `buttons` の既定が 0 で、move で押下中のボタンが離れました。離したいときは 0 を明示します。
- `wheel()`、`press()`、`release()`、`click()`、`buttons()`、`sendReport()`/`sendState()` は変わりません。相対値は送信成功後に 0 へ戻るので、Host の idle 再送では移動 0 を送ります。

## 関連

- [KeyboardMouse](../KeyboardMouse/) - keyboard と mouse の複合
- [Keyboard](../Keyboard/) - HID keyboard
- [HID API ガイド](../../docs/classes/hid.ja.md)
- [全サンプル](../README.md)
