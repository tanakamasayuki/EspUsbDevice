# EspUsbDevice Serial

> English: [README.md](README.md)

USB CDC ACM serial device を作り、PC や USB host とテキストを送受信する例です。

Arduino-ESP32 標準の `USBSerial` / `USBCDC` は使いません。`EspUsbDeviceCdcSerial` を
`EspUsbDevice` に登録し、USB CDC の descriptor と endpoint をこのライブラリ側で構成します。
受信・送信 ring の大きさは `configure()` で実行時に決めます。`build_opt.h` は要りません。

## ハードウェア

- native USB OTG port を持つ ESP32-S2 / S3 / P4 board
- PC、EspUsbHost を動かす別 ESP32、またはテスト治具などの USB host
- ログ確認用の Serial monitor 接続（書込みに使う UART / USB Serial-JTAG 側）

## 動作内容

- CDC port の受信 ring を 256 byte、送信 ring を 1024 byte に設定してから `EspUsbDevice` を開始します。
- 受信した 1 文字を `echo: x` として返します。
- terminal が DTR を立てている間、3 秒ごとに `tick=... baud=...` を送ります。
- Host が設定した line coding、DTR / RTS、SEND_BREAK と、受信通知を通常の Serial monitor に出力します。

## 使い方

1. sketch を書き込み、通常の Serial monitor を開きます。
2. USB device port を PC または Host に接続します。
3. PC 側で追加された serial port を開きます。
4. 文字を送ると `echo: ...` が返ります。
5. Serial monitor 側には `CDC_LINE_CODING`、`CDC_LINE_STATE`、`CDC_BREAK`、`CDC_RX` が出ます。

## 主要 API

- `EspUsbDeviceCdcSerial usbSerial(device, "name")` は CDC ACM function を登録します。省略できる名前は Host に表示される interface 名になります。
- `usbSerial.configure(EspUsbBulkStreamConfig)` で `rxCapacity`、`txCapacity` などを設定します。停止中だけ受け付けます。
- `usbSerial.available()` / `read()` は Host から届いた byte を読みます。`read()` は待ちません。
- `usbSerial.write()` / `print()` / `printf()` は Host へ送ります。空きができるまで `setTxTimeoutMs()`（既定 1000 ms、この例では 100 ms）まで待ちます。
- `usbSerial.flush()` は送信を始め、DMA と必要な ZLP の完了まで同じ期限で待ちます。
- `usbSerial.connected()` は「構成済みかつ DTR=1」です。`write()` は DTR を条件にしません。
- `usbSerial.lineCoding()` / `lineState()` は現在値の写しを返します。
- `usbSerial.onLineCoding()` / `onLineState()` / `onBreak()` / `onRx()` で callback を登録します。戻り値は `esp_err_t` で、`begin()` の前だけ受け付けます。
- `usbSerial.readCapacity()` / `writeCapacity()` は `begin()` が実際に確保した容量です。

## 注意

- USB CDC とログ用 Serial monitor は別の経路です。
- PC 側の serial port 名は OS によって異なります。
- baud rate は USB CDC の line coding として通知されますが、USB の実転送速度そのものではありません。
- callback は USB worker task で動きます。短く保ち、待つ処理、`device.end()`、設定変更をしないでください。
- `EspUsbDeviceCdcSerial` は Arduino の `Stream` なので、`Stream&` を受け取るコードへそのまま渡せます（[CdcConsole](../CdcConsole/)）。
- Core の USB CDC on boot を有効にしないでください。OTG controller はこのライブラリが使います。

## 関連

- [SerialMulti](../SerialMulti/) - 1 台に複数の CDC port
- [CdcConsole](../CdcConsole/) - CDC を `Stream&` として渡す例
- [Keyboard](../Keyboard/) - HID keyboard device
- [CDC API](../../docs/classes/cdc.ja.md)

## 読み方と確認の手順

### 実行前の準備

受信とline stateを観察できるCDCシリアル機能を作る。

上のハードウェアの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`setup()`は`device.begin()`の前にportを設定し、callbackをすべて登録します。どれかが拒否されたら表示して止まります。新しいUSBシリアルportへ文字とCR/LFを送り、一行単位ではなく文字ごとに返信することを確認します。端末の開閉でDTRと定期tickの関係を比べます。

### APIの使い方と改造時の注意

`device.ready()`はUSBの構成済み状態、CDCの`connected()`はホストのDTRに関わり、別の問いに答えます。受信通知の量は一行・一packetの区切りではありません。実際に読めたbyteから自分でメッセージを組み立てます。`write()`はbyte、`print()`/`printf()`はテキスト整形です。`flush()`はPCアプリが消費済みという保証ではありません。

`write()`は受け付けたbyte数を返します。期限切れ・切断・bus resetでは要求より少ないことがあります。line coding通知はホストが求めたシリアル条件であり、USBの速度を変えません。callbackを短く保ち、大きな解析は同期した受け渡しでloop()へ分けます。確実な命令通信には受付byte数・未送信の後半・切断時の途中入力を扱う方針が必要です。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。容量・期限・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

## v2 からの変更点

- ring の大きさは `CFG_TUD_CDC_*` の build flag ではなく、`begin()` 前の `configure()` で決めます。
- callback の登録は `esp_err_t` を返します。この例は結果を一つずつ確認します。
- `write()` / `flush()` は `setTxTimeoutMs()` まで待つようになりました。止まった terminal が `loop()` を 1 秒止めないよう、この例は 100 ms にしています。
- この例で新しく示すもの：`onBreak()`、tick に載せる `lineCoding()` の写し、開始時の実効容量の表示。
- interface に名前（`"EspUsbDevice Serial"`）を付けました。

- [スケッチ](Serial.ino)
- [全サンプル](../README.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
