# EspUsbDevice KeyboardMouse

> English: [README.md](README.md)

1台の ESP32 から keyboard report と mouse report を出す composite USB HID device の例です。

## ハードウェア

- USB device 対応の ESP32-S3 など Arduino-ESP32 board
- PC、EspUsbHost を動かす別 ESP32、またはテスト治具などの USB host
- ログ確認用の Serial monitor 接続

## 動作内容

- `EspUsbDeviceHidKeyboard` と `EspUsbDeviceHidMouse` を同じ `EspUsbDevice` に登録します。
- layout 対応 keyboard wrapper で文字を送信します。
- mouse movement と left click を送信します。
- このライブラリの composite HID descriptor 構成を確認できます。

## Composite HID の形

現時点の composite HID は、単一 HID interface と report ID で構成します。
これは、このライブラリが使う Arduino-ESP32 TinyUSB runtime で安定して列挙するための構成です。

| Report | Report ID |
|--------|-----------|
| Keyboard | `1` |
| Mouse | `2` |

report ID 付き keyboard report は 9 bytes になるため、composite HID interrupt endpoint
MPS は `16 bytes` です。

## 主要 API

- 複数 class object を同じ `EspUsbDevice` instance で作ります。
  `EspUsbDeviceHidKeyboard keyboard(device);`
  `EspUsbDeviceHidMouse mouse(device);`
- すべての class object を作った後、`device.begin(config)` を1回呼びます。
- keyboard API は [Keyboard](../Keyboard/) を参照してください。
- mouse API は [Mouse](../Mouse/) を参照してください。

## 想定 Serial 出力

```text
USB keyboard + mouse ready
KEY_FAILED error=ESP_ERR_TIMEOUT
MOVE_FAILED error=ESP_ERR_TIMEOUT
CLICK_FAILED error=ESP_ERR_TIMEOUT
```

起動後、この example は失敗時だけ Serial に出力します。keyboard と mouse の挙動は USB host 側で確認してください。

## 関連

- [Keyboard](../Keyboard/) - HID keyboard device
- [Mouse](../Mouse/) - HID mouse device
