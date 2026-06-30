# EspUsbDevice USBVendor

> English: [README.md](README.md)

HID ではない vendor-specific USB interface を作る例です。
`VendorHID` は HID report を使う独自通信ですが、`USBVendor` は bulk IN / OUT endpoint と
vendor control request を使います。

Arduino-ESP32 標準の `USBVendor` API との互換は目標にしません。`EspUsbDeviceVendor` として、
`available()` / `read()` / `write()` / `flush()` と callback を使う単純な API にしています。

## ハードウェア

- USB device 対応の ESP32-S3 など Arduino-ESP32 board
- PC、EspUsbHost を動かす別 ESP32、またはテスト治具などの USB host
- ログ確認用の Serial monitor 接続

## 動作内容

- `bInterfaceClass = 0xff` の vendor-specific interface を構成します。
- bulk OUT で受信した byte列を Serial monitor に hex 表示します。
- 受信データは bulk IN で `echo: ...` として返します。
- vendor control request `0x01` IN には `EspUsbDeviceVendor` 文字列を返します。
- vendor control request `0x02` OUT は status stage だけ成功応答します。
- `EspUsbDeviceConfig::webusbEnabled` と `webusbUrl` で WebUSB landing URL を設定します。
- 3 秒ごとに bulk IN へ status 行を送ります。

## 使い方

1. sketch を書き込み、通常の Serial monitor を開きます。
2. USB device port を PC または Host に接続します。
3. Host 側の vendor-specific interface を開きます。
4. bulk OUT に byte列を送ると、Device は bulk IN に echo を返します。
5. control transfer で `bRequest = 0x01` の IN request を送ると Device 情報を返します。

PC から扱う場合は libusb、WinUSB、WebUSB などの Host 側実装が必要です。

## 主要 API

- `EspUsbDeviceVendor UsbVendor(device)` は vendor-specific function を登録します。
- `UsbVendor.available()` は bulk OUT で受信済みの byte 数を返します。
- `UsbVendor.read()` は Host からの byte を読みます。
- `UsbVendor.write()` / `print()` / `printf()` は Host へ bulk IN で送信します。
- `UsbVendor.flush()` は送信を flush します。
- `UsbVendor.mounted()` は Host に列挙済みかを返します。
- `UsbVendor.onRx(callback)` は bulk OUT の受信時に呼ばれます。
- `UsbVendor.onControlRequest(callback)` は EP0 の vendor request を受け取ります。
- `UsbVendor.sendControlResponse(request, data, length)` は control transfer に応答します。
- `config.webusbEnabled` は Arduino-ESP32 TinyUSB core の WebUSB BOS descriptor を有効にします。
- `config.webusbUrl` は WebUSB landing page URL です。`https://` は core 側の URL descriptor scheme
  ではなく、文字列としてそのまま扱われるため、まずは host/browser 側で期待する形式を確認してください。

## VendorHID との違い

- `VendorHID` は HID driver で扱いやすく、固定長 report の小さな独自通信に向いています。
- `USBVendor` は HID ではないため、bulk 転送、PC app、browser、独自 protocol に向いています。
- `USBVendor` は Host 側 driver / permission / claim 処理が必要になることがあります。

## 注意

- WebUSB / Microsoft OS 2.0 descriptor の実体は Arduino-ESP32 TinyUSB core が生成します。
- `EspUsbDevice` 側では URL や独自 request callback を扱います。vendor code や Microsoft OS 2.0
  descriptor の細かい差し替え API はまだありません。

## 関連

- [VendorHID](../VendorHID/) - HID report を使う vendor-defined HID
- [Serial](../Serial/) - CDC ACM serial
