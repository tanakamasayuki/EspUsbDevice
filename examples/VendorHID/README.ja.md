# EspUsbDevice VendorHID

> English: [README.md](README.md)

Vendor-defined HID device を作り、Host と独自 63 byte payload を送受信する例です。

Vendor HID は、専用 driver を用意せずに小さな独自プロトコルを Host application とやり取り
したい場合に便利です。この example は report ID `6` の Input / Output / Feature report を
扱います。

## ハードウェア

- USB device 対応の ESP32-S3 など Arduino-ESP32 board
- EspUsbHost、hidapi を使う PC application、またはテスト治具などの USB host
- ログ確認用の Serial monitor 接続

## 動作内容

- `EspUsbDevice` を full-speed vendor HID device として起動します。
- 1 秒ごとに 63 byte の Input report を Host へ送信します。
- Host からの Output report を `onOutputReport()` で受け取り、Serial monitor に hex dump します。
- Host からの Feature report を `onFeatureReport()` で受け取り、Serial monitor に hex dump します。

## 主要 API

- `EspUsbDeviceHidVendor vendor(device)` は vendor-defined HID function を登録します。
- `vendor.sendInput(data, length)` は Host へ Input report を送信します。
- `vendor.onOutputReport(callback)` は Host からの Output report を受け取ります。
- `vendor.onFeatureReport(callback)` は Host からの Feature report を受け取ります。
- callback では `EspUsbDeviceHidReport` の `reportId`、`reportType`、`data`、`length` を確認できます。

## Report 仕様

- usage page: vendor-defined `0xff00`
- report ID: `ESP_USB_DEVICE_HID_REPORT_ID_VENDOR` (`6`)
- payload size: 63 bytes
- interrupt IN endpoint: Device -> Host
- interrupt OUT endpoint: Host -> Device
- Feature report: control transfer

`sendInput()` に渡す payload は report ID を含みません。report ID はライブラリ側で付与します。
Output / Feature callback の `data` も payload 部分です。

## 注意

- Vendor HID は Host 側 application の実装が必要です。
- PC から扱う場合は OS 標準 HID API や hidapi などを使います。
- 汎用 serial のような stream ではなく、固定長 report の交換として設計してください。
- 大きな連続 stream には CDC ACM の方が向いています。

## 関連

- [Serial](../Serial/) - CDC ACM serial device
- [KeyboardMouse](../KeyboardMouse/) - composite HID device
