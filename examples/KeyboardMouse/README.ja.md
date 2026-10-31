# EspUsbDevice KeyboardMouse

> English: [README.md](README.md)

1つの ESP32 で keyboard と mouse の report を公開する、複合 USB HID device の例です。

## ハードウェア

- ESP32-S3、ESP32-S2、ESP32-P4 の board。native USB OTG の口を Host へつなぐ
- PC、EspUsbHost を動かす別の ESP32、試験治具などの USB Host
- `Serial`（115200）用の別の debug 接続。入力とログの両方に使う

## 動作内容

- `EspUsbDeviceHidKeyboard` と `EspUsbDeviceHidMouse` の両方で `EspUsbDevice` を開始します。
- `begin()` が割り当てた report ID と、最大の Input report の長さを表示します。
- debug port で `k` を受けると、`K` を入力し、ポインタを右へ 40 動かし、左クリックを送ります。

`k` を送る前に Host 側で試し書き用の editor を選んでください。

## 複合 HID の形

1つの Device の複数の HID Function は、1つの HID interface と1つにまとめた report descriptor を共有し、report ID で区別されます。

| Report | Report ID |
|--------|-----------|
| Keyboard | `1`（`ESP_USB_DEVICE_HID_REPORT_ID_KEYBOARD`） |
| Mouse | `2`（`ESP_USB_DEVICE_HID_REPORT_ID_MOUSE`） |

これは希望の ID です。実際に割り当てた ID は `keyboard.compositeReportId()` と `mouse.compositeReportId()` で読めます
（同じ種類の HID を複数登録すると別の ID になります）。まとめた descriptor では各 Function の部分を Push/Pop で囲むので、
単位や範囲が次の Function へ引き継がれません。送信関数が report ID を付けます。

## 主要 API

- すべての Function を同じ `EspUsbDevice` で、`begin()` の前に作ります。
  `EspUsbDeviceHidKeyboard keyboard(device);`
  `EspUsbDeviceHidMouse mouse(device);`
- Function をすべて作ってから、`device.begin(config)` を1回呼びます。
- `compositeReportId()` は割り当てた ID を返します（単独のときと停止中は 0）。
- `inputReportCapacity()` は共有 interface の最大の Input report の長さを、report ID の 1 byte 込みで返します（`begin()` 前は 0）。
- keyboard の API は [Keyboard](../Keyboard/)、mouse の API は [Mouse](../Mouse/) を参照してください。

## 想定 Serial 出力

```text
USB keyboard + mouse ready (keyboard id=1 mouse id=2 input=9 bytes)
Focus a scratch editor on the Host, then send k on this port
```

その後は失敗だけを表示します（例：`KEY_FAILED ESP_ERR_TIMEOUT`）。keyboard と mouse の動作は Host 側で確かめます。

## 確認の手順

- 表示した report ID と、HID parser が示す descriptor を比べます。
- Function を増やすと report の形が変わります（report ID の 1 byte が付く）。単独の raw HID 用 client の前提を使い回さないでください。
- HID report は状態です。キーの位置、mouse の相対移動、ボタンの bitmap。押下と解放を対にします。

## v2 からの変更点

- キーと mouse の操作は、3秒ごとではなく `k` を送ったときに送ります。
- 割り当てた report ID は決め打ちせず、新しい `compositeReportId()` で読みます。新しい `inputReportCapacity()` で最大の Input report も表示します。
- v2 は Arduino-ESP32 の TinyUSB HID の中で複合の interrupt packet を 16 byte に固定していました。v3 は library 独自の HID driver を使い、
  `begin()` でまとめた descriptor から領域を決めます。共有する interrupt IN の packet 長は、各 Function が求める値の最大で、16 byte 以上です
  （この例では 16。NKRO の keyboard や大きな Custom/Vendor HID を加えると大きくなる）。Host が bus を reset した後は `keyboard.inputPacketSize()` で読めます。
  keyboard と mouse のスケッチのコードは変わりません。

## 関連

- [Keyboard](../Keyboard/) - HID keyboard
- [Mouse](../Mouse/) - HID mouse
- [HID API ガイド](../../docs/classes/hid.ja.md)
- [全サンプル](../README.md)
