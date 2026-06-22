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
