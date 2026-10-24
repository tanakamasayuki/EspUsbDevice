# EspUsbDevice VendorHID

> English: [README.md](README.md)

Vendor-defined HID device を作り、Host と独自 63 byte payload を送受信する例です。

Vendor HID は、専用 driver を用意せずに小さな独自プロトコルを Host application とやり取り
したい場合に便利です。この example は report ID `6` の Input / Output / Feature report を
扱います。

63 byte は既定値であって上限ではありません。上限は `CFG_TUD_HID_EP_BUFSIZE - 1` で、
ESP32-S2/S3 では 63、**ESP32-P4 では 511** です。P4 の high-speed interrupt endpoint は
125 us ごとに report 全体を 1 packet で運びます。`EspUsbDeviceHidVendor vendor(device, 511)`
と書くだけで、report descriptor もそのサイズに追従します。
[docs/usb-device-advanced.ja.md](../../docs/usb-device-advanced.ja.md#62-最大パケットサイズ)
を参照してください。

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

## 読み方と確認の手順

### 実行前の準備

Vendor定義HIDのInput・Output・Feature reportを扱う。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

定期的にInputを送り、ホストからのOutput・Feature通知を表示します。対応するreport ID・長さを指定できるHID clientで試します。Inputはbulk streamではなく、Feature要求とOutput割り込みreportも同じ操作ではありません。

### APIの使い方と改造時の注意

HID reportは構造化された状態や変化であり、任意のテキスト一行ではありません。report descriptorと合わせて、report ID・usage page/usage・符号・数値範囲を読みます。キーボードはキー位置、マウス移動は相対値、ボタンはbitmapです。実際の操作へつなぐ前に、押下と解放の対、または全状態の比較を確認します。

callbackのreportは借りたデータです。別taskへ渡す場合はbyte列や必要な値を自分の領域へコピーします。callbackでは短い診断に留め、生dumpの連続表示で端末速度が律速になることにも注意します。入力が来なければ、列挙、descriptor・report長、アプリ側の解釈の順に調べ、一度に全部を変えないようにします。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](VendorHID.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
