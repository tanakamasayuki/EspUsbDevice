# EspUsbDevice USBVendor

> English: [README.md](README.md)

HID ではない vendor-specific USB interface を作る例です。vendor HID は独自データを HID report で送りますが、
`USBVendor` は bulk IN / OUT endpoint と vendor control request を使います。

Arduino-ESP32 の `USBVendor` API 互換を目指したものではありません。`EspUsbDeviceVendor` は
`available()` / `read()` / `write()` / `flush()` と callback による単純な API で、Arduino の `Print` でもあります。

## ハードウェア

- native USB OTG port を持つ ESP32-S2 / S3 / P4 board
- PC、EspUsbHost を動かす別 ESP32、または他の USB host
- ログ確認用の Serial monitor 接続

## 動作内容

- `bInterfaceClass = 0xff` の vendor-specific interface を作ります。`configure()` で受信 ring を 512 byte、送信 ring を 1024 byte にします。
- bulk OUT で受けたデータを Serial monitor に hex で表示し、`onRx()` の通知もログに出します。
- 受けたデータを `echo: ...` として bulk IN で返します。返事が途中で切れないよう、全体が入る空きを待ってから書きます。
- vendor control IN request `0x10` に `EspUsbDeviceVendor` を返します。
- vendor control OUT request `0x11` で最大 64 byte のデータ（なしでもよい）を受け、DATA 段階で受信長を確認し、転送が完了してから表示します。
- `EspUsbDeviceConfig::webusbEnabled` と `webusbUrl` で WebUSB の landing URL を、`deviceInterfaceGuid` で Windows の `DeviceInterfaceGUIDs` を設定します。
- ライブラリ自身が応答する WebUSB / Microsoft OS 2.0 の要求を `device.onAnyControlRequest()` でログに出します。
- 3 秒ごとに bulk IN で status 行を送ります。

## 使い方

1. sketch を書き込み、通常の Serial monitor を開きます。
2. USB device port を PC または Host に接続します。
3. Host 側で vendor-specific interface を開きます。
4. bulk OUT に byte を送り、bulk IN から echo を読みます。
5. IN の control transfer（`bmRequestType = 0xc0`、`bRequest = 0x10`）で device 情報を読みます。OUT（`0x40`、`0x11`）で数 byte を送ることもできます。

PC で使う場合は、通常 libusb、WinUSB、WebUSB などを使った Host 側の実装が必要です。
Windows では、ライブラリの Microsoft OS 2.0 descriptor により `.inf` なしで WinUSB が割り当てられます。

```python
# Host 側の pyusb の例
import usb.core
dev = usb.core.find(idVendor=0x303a, idProduct=0x4019)
print(bytes(dev.ctrl_transfer(0xc0, 0x10, 0, 0, 64)))
dev.ctrl_transfer(0x40, 0x11, 0, 0, b"\x01\x02\x03")
```

## 主要 API

- `EspUsbDeviceVendor vendor(device)` は vendor-specific function を登録します。省略できる第 2 引数は FS の bulk packet 長です。USB 2.0 5.8.3 により FS で使えるのは 8/16/32/64 だけで、それ以外は `device.begin()` が `ESP_ERR_INVALID_SIZE` で失敗します。HS では常に 512 です。
- `vendor.configure(EspUsbVendorConfig)` で ring と受信方式を設定します。停止中だけ受け付けます。
- `vendor.available()` / `read()` は bulk OUT で受けた byte を読みます。待ちません。
- `vendor.write()` / `print()` / `printf()` は送信 ring へコピーし、受け付けた byte 数を返します。待ちません。
- `vendor.waitWritable(n, timeoutMs)` は n byte が入るまで待ちます。`writeAvailable()` は今の空きです。
- `vendor.flush()` は溜まった分の送信を始めます（`autoFlush = false` のときだけ必要）。完了は待ちません。
- `vendor.mounted()` は Host が構成し、endpoint が開いているかを返します。
- `vendor.onRx(callback)` は bulk OUT の受信時に呼ばれます。
- `vendor.onControlRequest(callback)` は EP0 の vendor request を SETUP、DATA、ACK の各段階で受け取ります。
- `vendor.sendControlResponse(request, data, length)` は data 段階を始めます。IN なら送るデータ、OUT なら受信先です。`wLength = 0` なら status 段階だけを返します。
- `request.actualBytes` は data 段階で実際に転送した byte 数です。
- `device.onAnyControlRequest(observer)` は、`onControlRequest()` より先にライブラリが応答するものも含め、すべての control request を観察します。戻り値は `esp_err_t` で、停止中に設定します。開始後は `ESP_ERR_INVALID_STATE` です。
- `config.webusbEnabled` は WebUSB の BOS capability を加え、`config.webusbUrl` は landing page を設定します。`https://` と `http://` は WebUSB の scheme byte で表します。
- `config.deviceInterfaceGuid` は Windows のアプリがこの interface を探すときの GUID です。製品では自分で生成した値を使ってください。

## vendor request の番号

ライブラリは、`onControlRequest()` より前に、次の 2 つの vendor request にだけ応答します。

| 要求 | bmRequestType | bRequest | wValue | wIndex | 条件 |
|---|---|---|---|---|---|
| WebUSB GET_URL | `0xC0` | `0x01` | URL の index（1 に応答、他は STALL） | `2` | `webusbEnabled = true` |
| Microsoft OS 2.0 descriptor set | `0xC0` | `0x02` | `0` | `7` | Vendor または DFU の Function がある |

`bRequest` が同じでも、向き・`wIndex`・`wValue` が違う要求は `onControlRequest()` に届きます。この例の独自 protocol は、どちらとも重ならない `0x10` と `0x11` を使います。

## vendor HID との違い

- vendor HID は HID driver 経由で扱いやすく、小さな固定 report に向いています。
- `USBVendor` は bulk 転送、PC アプリ、ブラウザ連携、独自 protocol に向いています。
- `USBVendor` では Host 側で driver、権限、interface claim の処理が必要になることがあります。

## 注意

- callback は USB worker で動き、`begin()` の前に登録します。短く保ち、待つ処理や `device.end()` を呼ばないでください。
- control OUT の受信先は転送が終わるまで有効でなければならないので、global 配列にしています。
- SETUP または DATA で `false` を返すと要求を STALL します。ACK で `false` を返しても、完了した転送は取り消せません。
- bulk データは byte stream です。複数の `write()` が 1 回の Host の read に届くことがあり、Vendor の既定では packet 長ちょうどの書込みの後に ZLP を付けません。メッセージは長さで区切ってください。

## 関連

- [VendorEcho](../VendorEcho/) - 書き切れなかった残りを保持する最小の bulk echo
- [ReconfigureVendor](../ReconfigureVendor/) - 実行中に ring の大きさを変える
- [Serial](../Serial/) - CDC ACM serial
- [Vendor API](../../docs/classes/vendor.ja.md)

## 読み方と確認の手順

### 実行前の準備

Vendor bulkデータをechoし、定義したcontrol要求へ応答する。

上のハードウェアの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

このvendor interfaceはホストのclientが開く必要があり、OSのシリアルportではありません。`onRx()`は通知を記録するだけで、読み出しとechoは`loop()`が行うので、遅いホストがUSB workerを止めることはありません。`handleControl()`は方向と段階の扱いが異なる0x10と0x11に応答し、定義していない要求はSTALLします。

### APIの使い方と改造時の注意

USBのIN/OUTは常にホストから見た名前です。INはホストへ、OUTはdeviceへ向かいます。endpointの方向と、アプリ上の要求・応答の役割を分けて考えます。bulk転送はbyte streamで、命令全体とは限りません。実際の長さとprotocolの区切りを使います。control転送も、payloadだけでなく方向・request・value・index・lengthを持ちます。

USB転送の受付や完了は、アプリとしての受理確認ではありません。エラーや状態の返答を確認し、切断時にはprotocolの状態を回復します。callbackのpointerは戻った後に再利用されることがあります。借用buffer（`writeDirect`/`readDirect`）では、投稿と完了の所有権の規則を守り、転送中のデータを上書きしません。ringや転送長を調整する前に、短い既知の要求から始めます。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。request・容量・URLを一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

## v2 からの変更点

- request の番号を `0x01` / `0x02` から `0x10` / `0x11` に変えました。v2 でも `0x01` は WebUSB（その例で有効）に、`wIndex = 7` の `0x02` は Microsoft OS 2.0 に使われていたため、情報の要求は sketch に届いていませんでした。
- OUT の要求はデータを buffer に受け、段階ごとに処理します。v3 ではデータ付きの OUT 要求に受信先が必要です。v2 は status 段階だけを返していました。
- echo を `onRx()` の callback から `loop()` へ移し、返事が途中で切れないよう `waitWritable()` で待ちます。既定の `autoFlush` では `flush()` は不要です。
- WebUSB の URL に `https://` を付けました。scheme がないと URL descriptor は「URL 全体を含む」という意味になり、Host が `example.com/...` を拒むことがあります。
- 新しく示すもの：`deviceInterfaceGuid`（v2 には GUID の設定がありませんでした）、`onAnyControlRequest()`、`actualBytes`、`configure()` による ring の大きさ。

- [スケッチ](USBVendor.ino)
- [全サンプル](../README.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
