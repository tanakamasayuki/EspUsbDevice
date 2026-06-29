# EspUsbDevice CustomHID

> English: [README.md](README.md)

任意の HID report descriptor を渡して、custom HID device を作る例です。

この example は vendor-defined usage page `0xff00` の 8 byte Input report を 1 秒ごとに
Host へ送信します。Keyboard、Mouse、Gamepad、VendorHID の定型 class に当てはまらない
小さな HID device を試すための出発点です。

## ハードウェア

- USB device 対応の ESP32-S3 など Arduino-ESP32 board
- EspUsbHost、HID parser、独自 Host application などの USB host
- ログ確認用の Serial monitor 接続

## 動作内容

- `REPORT_DESCRIPTOR` を sketch 内で定義します。
- `EspUsbDeviceHidCustom` に descriptor と input report size を渡します。
- 1 秒ごとに 8 byte report を Host へ送信します。
- Serial monitor に送信 counter を出力します。

## 主要 API

- `EspUsbDeviceHidCustom customHid(device, descriptor, descriptorLength, inputReportSize)` は custom HID function を登録します。
- `customHid.sendReport(data, length)` は report ID なしの Input report を送信します。
- `customHid.sendReport(data, length, reportId)` は report ID 付き descriptor 用に使います。

## Descriptor

この example の descriptor は次の report を定義します。

- usage page: vendor-defined `0xff00`
- report ID: なし
- input report size: 8 bytes
- report count: 8
- report size: 8 bits

descriptor と `inputReportSize` が一致していないと、Host 側 parser や endpoint MPS の期待と
実際の report 長がずれます。まず小さい固定長 report から始めるのがおすすめです。

## VendorHID との違い

- `VendorHID` は定型の vendor-defined report descriptor と Output / Feature callback を持ちます。
- `CustomHID` は descriptor を sketch 側で完全に定義します。
- 独自 descriptor を検証したい場合は `CustomHID`、単純な独自バイナリ通信なら `VendorHID` が向いています。

## 関連

- [VendorHID](../VendorHID/) - 定型 vendor-defined HID
- [Gamepad](../Gamepad/) - HID gamepad device
