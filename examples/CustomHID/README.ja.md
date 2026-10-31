# EspUsbDevice CustomHID

> English: [README.md](README.md)

スケッチで書いた HID report descriptor から、独自の HID device を作る例です。

vendor 定義の usage page `0xff00` で、8 byte の Input report を1秒ごとに送ります。
内蔵の Keyboard、Mouse、Gamepad、VendorHID に当てはまらない小さな HID device の出発点です。

## ハードウェア

- ESP32-S3、ESP32-S2、ESP32-P4 の board。native USB OTG の口を Host へつなぐ
- EspUsbHost、HID parser、独自の Host アプリなどの USB Host
- ログ用の `Serial`（115200）の別の debug 接続

## 動作内容

- スケッチの中で `reportDescriptor` を定義します。
- descriptor と、interrupt IN の packet 長（8 byte）を `EspUsbDeviceHidCustom` に渡します。
- `begin()` が descriptor から求めた Input / Output / Feature report の長さを表示します。
- Host が構成を済ませている間、8 byte の report を1秒ごとに送ります。
- 送った counter を表示します。

## 主要 API

- `EspUsbDeviceHidCustom customHid(device, descriptor, descriptorLength, inputReportSize)` は独自 HID の Function を登録します。
  `inputReportSize`（既定 64）は、宣言する interrupt IN の `wMaxPacketSize` です。report ID の 1 byte を含みます。
  full speed では 64 まで（大きい値は 64 になる）、high speed では 1024 までです。
  これより長い report も、1回の転送の複数の packet で送れます。report の領域はこの値ではなく descriptor から決まります。
- 他の HID Function と1つの HID interface を共有するときは、その interface の packet 長は、各 Function が求める値の最大（16 以上）になります。
- `customHid.inputPacketSize()` は、Host と決まった速度での interrupt IN の `wMaxPacketSize` を返します（bus reset 前と `end()` 後は 0）。
- `customHid.sendReport(data, length)` は report ID なしの Input report を送ります。
- `customHid.sendReport(data, length, reportId)` は report ID を持つ descriptor で使います。他の HID Function と組み合わせたときは割り当てた ID を使います。
- `customHid.inputReportCapacity()` / `outputReportCapacity()` / `featureReportCapacity()` は `begin()` が求めた長さを返します。
  report ID を宣言していればその 1 byte を含みます（`begin()` 前は 0）。
- `customHid.setFeatureReport(data, length, reportId)` は、Feature report を宣言した descriptor で、Host の GET_REPORT(Feature) が読む値を設定します。

## Descriptor

この例の descriptor の内容：

- usage page：vendor 定義 `0xff00`
- report ID：なし
- Input report：8 byte（Report Count 8、Report Size 8 bit）
- Output / Feature report はないので、OUT endpoint 用の領域は確保しません

`begin()` は descriptor を解析し（Report Size / Count / ID、Push/Pop）、途中で切れた item、Push/Pop の不整合、
Report ID 0 の明示指定、番号付きと番号なしの report の混在、長すぎる report を拒否します。usage とその意味まで全部検査するものではありません。
descriptor の配列は、動作中ずっと有効で変更しないまま保ってください。

## 想定 Serial 出力

```text
USB custom HID ready (input=8 output=0 feature=0 bytes)
CUSTOM_HID_TX counter=0
CUSTOM_HID_TX counter=1
```

## VendorHID との違い

- `VendorHID` は定義済みの vendor 定義 descriptor と、Output / Feature の callback を持ちます。
- `CustomHID` は descriptor をスケッチで自由に定義できます。
- 独自の descriptor を確かめるには `CustomHID`、簡単な独自バイナリのやり取りには `VendorHID` を使います。

## 確認の手順

- `reportDescriptor` と、`sendReport()` に渡す byte 列を合わせて読みます。Host 側の reader で report ID、項目数、report 長を合わせます。
- この例には Output や Feature のやり取りはありません。Output report を受けるには `VendorHID` を使うか、`EspUsbHidFunction` から独自のクラスを作ります。

## v2 からの変更点

- `device.ready()` が true の間だけ送ります。
- descriptor は `begin()` で解析します。不正な descriptor は Host に届く前に `begin()` が失敗します（原因は `customHid.startError()`）。
- `inputReportSize` は、宣言する interrupt IN の packet 長を決めます。複合 HID の interface でも high speed でも反映されます。v2 は `CFG_TUD_HID_EP_BUFSIZE` で buffer の大きさを決めていましたが、この設定は廃止です。
  1 packet より長い report は、1回の転送の複数の packet で送ります。
- 新しく `inputReportCapacity()` などの容量の照会、`inputPacketSize()`、`setFeatureReport()` が使えます。
- `sendReport(data, length[, reportId])` は変わりません。

## 関連

- [VendorHID](../VendorHID/) - 定義済みの vendor 定義 HID
- [Gamepad](../Gamepad/) - HID gamepad
- [HID API ガイド](../../docs/classes/hid.ja.md)
- [全サンプル](../README.md)
