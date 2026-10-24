# EspUsbDevice Serial

> English: [README.md](README.md)

USB CDC ACM serial device を作り、PC や USB host とテキストを送受信する例です。

Arduino-ESP32 標準の `USBSerial` / `USBCDC` は使いません。`EspUsbDeviceCdcSerial` を
`EspUsbDevice` に登録し、USB CDC class descriptor と endpoint をこのライブラリ側で
構成します。

## ハードウェア

- USB device 対応の ESP32-S3 など Arduino-ESP32 board
- PC、EspUsbHost を動かす別 ESP32、またはテスト治具などの USB host
- ログ確認用の Serial monitor 接続

## 動作内容

- `EspUsbDevice` を full-speed CDC ACM serial device として起動します。
- USB CDC 側に 3 秒ごとに `tick=...` を送信します。
- USB CDC 側から受信した 1 文字を `echo: x` として返します。
- Host が設定した line coding と DTR / RTS の状態を通常の Serial monitor に出力します。

## 使い方

1. sketch を書き込み、通常の Serial monitor を開きます。
2. USB device port を PC または Host に接続します。
3. PC 側で追加された serial port を開きます。
4. 文字を送ると `echo: ...` が返ります。
5. Serial monitor 側には `CDC_LINE_CODING` と `CDC_LINE_STATE` が出ます。

## 主要 API

- `EspUsbDeviceCdcSerial UsbSerial(device)` は CDC ACM function を登録します。
- `UsbSerial.available()` は Host から受信済みの byte 数を返します。
- `UsbSerial.read()` は Host からの byte を読みます。
- `UsbSerial.write()` / `print()` / `printf()` は Host へ送信します。
- `UsbSerial.flush()` は送信を flush します。
- `UsbSerial.connected()` は DTR が立っている状態を接続済みとして返します。
- `UsbSerial.onLineCoding(callback)` は baud / stop bits / parity / data bits の変更を受け取ります。
- `UsbSerial.onLineState(callback)` は DTR / RTS の変更を受け取ります。
- `UsbSerial.onRx(callback)` は Host から受信があったときに呼ばれます。

## 注意

- USB CDC とログ用 Serial monitor は別の経路です。
- PC 側の serial port 名は OS によって異なります。
- baud rate は USB CDC の line coding として通知されますが、USB の実転送速度そのものではありません。
- 既存 Arduino USB class と同時に使う設計ではありません。

## 関連

- [Keyboard](../Keyboard/) - HID keyboard device
- [Mouse](../Mouse/) - HID mouse device

## 読み方と確認の手順

### 実行前の準備

受信とline stateを観察できるCDCシリアル機能を作る。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

deviceへUsbSerialを登録してcallbackを用意し、deviceを開始します。新しいUSBシリアルportへ文字とCR/LFを送り、一行単位ではなく文字ごとに返信することを確認します。端末の開閉でDTRと定期tickの関係を比べます。

### APIの使い方と改造時の注意

`device.ready()`はUSBの設定済み状態、`CDCのconnected()`はホストのDTRに関わり、別の問いに答えます。受信通知の量は一行・一packetの区切りではありません。実際に読めたbyteから自分でメッセージを組み立てます。`write()`はbyte、`print()`/`printf()`はテキスト整形です。`flush()`はPCアプリが消費済みという保証ではありません。

line coding通知はホストが求めたシリアル条件であり、USBの速度を変えません。callbackを短く保ち、大きな解析は同期した受け渡しでloop()へ分けます。確実な命令通信には受付byte数・未送信の後半・切断時の途中入力を扱う方針が必要です。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](Serial.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
