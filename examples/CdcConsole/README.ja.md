# EspUsbDevice CdcConsole

> English: [README.md](README.md)

USB CDC ACM port を、Arduino の `Stream &` を受け取るコードへ渡す例です。

`EspUsbDeviceCdcSerial` 自身が `Stream` です。`Serial` 向けに書いた console、命令解析、protocol ライブラリにそのまま渡せます。包む object は要りません。この sketch は `Stream &` だけを使う byte の echo です。

## ハードウェア

- native USB OTG port を持つ ESP32-S2 / S3 / P4 board
- serial terminal を使える USB host（PC、または EspUsbHost を動かす別 ESP32）
- Serial monitor への出力はありません

## 動作内容

- CDC port の受信 ring を 256 byte、送信 ring を 512 byte、`transferBytes` を 64 にして `EspUsbDevice` を開始します。
- `loop()` から `echo(console)` を呼びます。`echo()` が知っているのは `Stream &` だけです。読める byte があり、送る空きがある間、1 byte 読んで送り返します。

## 使い方

1. sketch を書き込みます。
2. USB device port を Host に接続します。
3. 新しい serial port を開きます。Linux は `/dev/ttyACM*`（`picocom /dev/ttyACM0`）、Windows は `COM` port、macOS は `/dev/cu.usbmodem*` です。
4. 文字を打つと、そのまま返ります。device の echo だけを見るには、terminal の local echo を切ります。

## 主要 API

- `EspUsbDeviceCdcSerial console(device)` は CDC ACM function を登録します。
- `console.configure(EspUsbBulkStreamConfig)` で `rxCapacity`、`txCapacity`、`transferBytes` を決めます。停止中だけ受け付けます。
- `Stream &` 経由で使うもの：`available()` / `read()` は待ちません。`availableForWrite()` は送信 ring の空きです。`write()` は空きができるまで `setTxTimeoutMs()`（既定 1000 ms）まで待ちます。この例では先に `availableForWrite()` を確かめるので待ちません。
- `write()` は DTR を条件にしません。`connected()`（「構成済みかつ DTR=1」）はこの例では使いません。

## 注意

- `configure()` や `begin()` が失敗すると、別の debug 接続の `Serial`（115200）にエラー名を表示します。
- Core の USB CDC on boot を有効にしないでください。OTG controller はこのライブラリが使います。
- `transferBytes = 64` は IN 送信 1 回の上限です。P4 の HS controller（bulk packet 512）では毎回 short packet になります。

## 関連

- [Serial](../Serial/) - line coding、DTR、break の callback を使う CDC
- [SerialMulti](../SerialMulti/) - 1 台に複数の CDC port
- [CompositeCdcMidi](../CompositeCdcMidi/) - 1 台に CDC と MIDI
- [CDC API](../../docs/classes/cdc.ja.md)

## 読み方と確認の手順

### 実行前の準備

上のハードウェアの条件を確認し、board と例の profile を選びます。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USB の役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`setup()` は設定して開始するだけです。動作はすべて `echo(Stream &)` にあります。この関数は `Serial`、`HardwareSerial`、network client に対しても、そのまま compile できます。

### API の使い方と改造時の注意

`available()` が返す量は、一行でも一 packet でもありません。行単位の console は、自分の区切りまで byte をためます。`Serial` 向けのコードは `write()` が失敗しない前提のことがあります。USB では、期限切れ・切断・bus reset のときに要求より少ない byte 数を返します。

### 一つずつ確認する

最初は例を変えずに動かし、echo を確かめます。それから `echo()` を自分の `Stream &` 利用コードに置き換えます。

## v2 からの変更点

この例は v3 で新しく作りました。v2 には対応する例がありません。

- [スケッチ](CdcConsole.ino)
- [全サンプル](../README.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
