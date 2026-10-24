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
- `config.webusbEnabled` はこのライブラリの WebUSB BOS descriptor と、この vendor function
  がある場合の Microsoft OS 2.0 WinUSB descriptor を有効にします。
- `config.webusbUrl` は WebUSB landing page URL です。`http://` / `https://` は WebUSB URL
  descriptor の scheme field として符号化されます。

## VendorHID との違い

- `VendorHID` は HID driver で扱いやすく、固定長 report の小さな独自通信に向いています。
- `USBVendor` は HID ではないため、bulk 転送、PC app、browser、独自 protocol に向いています。
- `USBVendor` は Host 側 driver / permission / claim 処理が必要になることがあります。

## 注意

- Microsoft OS 2.0 descriptor は composite descriptor builder が実際に割り当てた vendor
  interface 番号を対象にします。`EspUsbDeviceVendor` がない構成では広告しません。
- vendor code、GUID、Microsoft OS 2.0 descriptor の細かい差し替え API はまだありません。

## 関連

- [VendorHID](../VendorHID/) - HID report を使う vendor-defined HID
- [Serial](../Serial/) - CDC ACM serial

## 読み方と確認の手順

### 実行前の準備

Vendor bulkデータをechoし、定義したcontrol要求へ応答する。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

OSのシリアルportではなく、Host clientでVendor interfaceを開きます。`onRx()`のbyte単位の塊と、方向・応答が異なる0x01/0x02のcontrol要求を比べます。callback内のechoは小さな例なので、連続通信の解析や再送queueは外へ分けます。

### APIの使い方と改造時の注意

USBのIN/OUTは常にdevice側から見た名称で、INはホストへ、OUTはdeviceへ向かいます。要求・応答というアプリの役割とendpoint方向を区別します。bulkはbyte streamであり、一転送が一命令とは限りません。実際の長さとprotocolの区切りを使います。control転送はpayloadだけでなく方向・request・value・index・lengthも持ちます。

USB転送の受付・完了はアプリの確認応答ではありません。相手のstatusも確認し、切断時にはprotocol状態を回復します。callbackのポインタは戻ると再利用される場合があります。非同期・direct bufferはacquire・submit・completionの所有規約を守り、転送中に上書きしません。queueや転送長を調整する前に短い既知の要求から試します。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](USBVendor.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
