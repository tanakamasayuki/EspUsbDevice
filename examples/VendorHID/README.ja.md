# EspUsbDevice VendorHID

> English: [README.md](README.md)

vendor 定義の HID device を作り、Host と独自の 63 byte の payload をやり取りする例です。

Vendor HID は、専用の driver を用意せずに、小さな独自プロトコルを Host のアプリとやり取りしたいときに便利です。
この例は report ID `6` の Input / Output / Feature report を扱います。

63 byte は既定値で、上限ではありません。スケッチの `reportBytes` を `EspUsbDeviceHidVendor::maxReportSize()`（65534）までの
任意の値に変えられ、report descriptor も自動でそれに合わせます。full speed（S2/S3、P4 の FS）では 64 byte の packet を超える report を
1回の転送の複数の packet で送り、high speed（P4 の HS）では interrupt packet を `reportBytes + 1`（最大 1024 byte）にします。
`maxReportSize()` はメモリが確保できることを保証する値ではありません。

## ハードウェア

- ESP32-S3、ESP32-S2、ESP32-P4 の board。native USB OTG の口を Host へつなぐ
- EspUsbHost、hidapi を使う PC アプリ、試験治具などの USB Host
- ログ用の `Serial`（115200）の別の debug 接続

## 動作内容

- payload 63 byte の vendor HID device として `EspUsbDevice` を開始します。
- `setFeatureReport()` で Feature report の初期値を設定します。
- payload の長さと、Input / Output / Feature の容量を表示します。
- Host が構成を済ませている間、63 byte の Input report を1秒ごとに送ります。
- Host からの Output report を `onOutputReport()` で受け、hex dump を表示します。
- Host からの Feature report（SET_REPORT）を `onFeatureReport()` で受け、hex dump を表示します。

## 主要 API

- `EspUsbDeviceHidVendor vendor(device, reportSize)` は vendor 定義 HID の Function を登録します。`reportSize` の既定は 63 です。
- `vendor.sendInput(data, length)` は Input report を送ります。空の payload や `reportSize()` より長い payload は false で、切り詰めはしません。短い payload はそのまま送ります。
- `vendor.onOutputReport(callback)` は Host からの Output report を受けます。
- `vendor.onFeatureReport(callback)` は Host からの Feature report を受けます。どちらも `esp_err_t` を返し、`device.begin()` の前に登録します。
- `vendor.setFeatureReport(data, length)` は、何も送らずに、Host が GET_REPORT(Feature) で読む値を設定します。`begin()` の後に使えます。
- `vendor.reportSize()`、`vendor.inputReportCapacity()`、`vendor.outputReportCapacity()`、`vendor.featureReportCapacity()` で長さを確かめられます。
- callback では `EspUsbDeviceHidReport` の `reportId`、`reportType`、`data`、`length` を見ます。`data` は callback の間だけ有効です。

## Report の仕様

- usage page：vendor 定義 `0xff00`
- report ID：`ESP_USB_DEVICE_HID_REPORT_ID_VENDOR`（`6`）
- payload：63 byte（report ID 込みで 64 byte）
- interrupt IN endpoint：Device → Host
- interrupt OUT endpoint：Host → Device
- Feature report：control 転送

`sendInput()` と `setFeatureReport()` に渡す payload は report ID を含みません。report ID は library が付けます。
Output / Feature の callback の `data` も payload の部分です。

## 想定 Serial 出力

```text
USB vendor HID ready (payload=63 input=64 output=64 feature=64 bytes)
VENDOR_INPUT counter=0
VENDOR_OUTPUT report_id=6 type=2 len=63 data=01 02 03 00 ...
VENDOR_FEATURE report_id=6 type=3 len=63 data=0a 0b 0c 00 ...
```

## 注意

- Vendor HID には対応する Host 側のアプリが要ります。PC では OS の HID API や hidapi などを使います。
- byte の stream ではなく、固定長の report のやり取りとして設計してください。Input report は bulk の stream ではなく、Feature の要求は Output の interrupt report の代わりになりません。
- 大きな連続 stream には CDC ACM や vendor bulk interface の方が向いています。
- `sendInput()` の true は「コピーして送信を始めた」という意味で、Host が読んだことは意味しません。

## v2 からの変更点

- 大きな report にビルド設定が要らなくなりました。v2 では `reportSize` の上限が `build_opt.h` の `CFG_TUD_HID_EP_BUFSIZE - 1`（S2/S3 は 63、P4 は 511）でした。
  この設定は廃止で、定義するとビルドエラーになります。v3 は 65534 まで受け付け、`begin()` で領域を確保します。
- `onOutputReport()` / `onFeatureReport()` の戻り値が `esp_err_t` になりました（`begin()` 前は `ESP_OK`、後は `ESP_ERR_INVALID_STATE`）。この例は戻り値を確かめます。
- `sendInput()` は `reportSize()` より長い payload を切り詰めずに拒否します。
- 新しく、GET_REPORT(Feature) の値を先に設定する `setFeatureReport()` と、`inputReportCapacity()` / `outputReportCapacity()` / `featureReportCapacity()` の照会が使えます。
- `device.ready()` が true の間だけ送ります。

## 関連

- [CustomHID](../CustomHID/) - スケッチで descriptor を書く HID
- [VendorEcho](../VendorEcho/) - vendor bulk interface
- [KeyboardMouse](../KeyboardMouse/) - 複合 HID device
- [HID API ガイド](../../docs/classes/hid.ja.md)
- [全サンプル](../README.md)
