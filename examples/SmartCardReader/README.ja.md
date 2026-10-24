# EspUsbDevice SmartCardReader

> English: [README.md](README.md)

ボードを 1 slot の USB CCID スマートカードリーダーにする例です。リーダーはライブラリ
（`EspUsbDeviceCcid`）が、slot の中のカードはスケッチが実装します。

CCID は PC/SC スタックが標準で話すクラスなのでドライバは不要です。Windows のスマート
カードサービス、macOS、Linux の `pcsc_scan`、EspUsbHost の `ccid*` API のいずれからも
リーダーとして見えます。

## ハードウェア

- USB device 対応の ESP32-S3（または ESP32-S2 / ESP32-P4）Arduino-ESP32 ボード
- PC、または EspUsbHost を動かした別の ESP32
- ログ表示とカード出し入れのためのシリアルモニタ

## 動作

- `bInterfaceClass = 0x0b`、bulk IN / bulk OUT と挿抜通知用の interrupt IN で列挙します。
- 起動時から slot にカードが入っています。ATR は MIFARE Classic 1K を表す PC/SC の
  合成 ATR なので、ATR からカード名を出す Host ではその名前で表示されます。
- 擬似カードは次の命令に応答します。
  - `FF CA 00 00 00`（PC/SC の Get UID）→ `04 11 22 33` と `9000`
  - `80 01 00 00 Lc <data>` → 同じデータと `9000`
  - それ以外 → `6D00`（命令を知らないカードと同じ応答）
- シリアルモニタの `i` でカードを挿入、`r` で排出します。どちらも interrupt endpoint で
  Host へ通知するので、Host 側アプリからは挿抜イベントとして見えます。

## 試し方

Linux の `pcsc-lite` の場合:

```sh
pcsc_scan
```

`EspUsbDevice CCID Reader` として現れ、カード挿入時に上記の ATR が表示されます。
`opensc-tool --atr` や `scriptor` でも確認できます。

EspUsbHost を動かした別の ESP32 と繋ぐ場合は、本リポジトリの `tests/peer/usb_ccid` に
リーダーを open して同じやり取りを行う Host 側スケッチがあります。

## 自分のカードを書く

カード固有の処理は `onApdu` callback だけです。Host が送った APDU を受け取り、2 byte の
status word を含む応答を書きます。周辺の CCID プロトコル（slot status、活性化、ATR、
parameter、abort）はライブラリが処理するので、カード側は APDU に答えるだけです。

callback は TinyUSB device task で実行されます。短く済ませ、中から USB API を呼び返さない
でください。

## 注意

- 1 slot、T=1、short APDU level exchange です。chaining、extended APDU、PIN pad、
  clock / data rate の交渉は範囲外で、class descriptor でも非対応と宣言します。
- CCID メッセージの上限は 271 byte です。増やす場合はビルド時に
  `ESP_USB_DEVICE_CCID_BUFFER_SIZE` を定義してください。
- Arduino-ESP32 標準の `USB.begin()` とは併用できません。

## 読み方と確認の手順

### 実行前の準備

CCID readerを模擬し、一部のAPDUへ応答する。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

ATRとUIDはサンプル配列で、実reader ICから取得したものではありません。`onApdu()`で入力長と返信容量を確認してデータとstatusを作ります。UARTのi/rで挿入状態を変えPC/SC clientで確認します。任意の実カードapplicationを実装する例ではありません。

### APIの使い方と改造時の注意

readerの列挙、interface開始、カード有無、活性化・ATR、APDUの各段階を分けます。CCID転送の成功とアプリのstatus wordは別の問いです。出力容量と実応答長の検査を残し、カード不在を空の正常応答として解釈しないようにします。

reader固有の疑似APDU・escapeは全スマートカードの共通命令ではありません。正確なreader・カードの組合せを確認して再利用します。callbackを短く保ち、完了待ちのreader操作はスケッチのアプリ側経路で行います。callback後も必要なデータはコピーします。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](SmartCardReader.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
