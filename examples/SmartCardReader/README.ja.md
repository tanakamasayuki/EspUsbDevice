# EspUsbDevice SmartCardReader

> English: [README.md](README.md)

ボードを 1 slot の USB CCID スマートカードリーダーにする例です。リーダーはライブラリ
（`EspUsbDeviceCcid`）が、slot の中のカードはスケッチが実装します。

CCID は PC/SC スタックが標準で話すクラスなのでドライバは不要です。Windows のスマート
カードサービス、macOS、Linux の `pcsc_scan`、EspUsbHost の `ccid*` API のいずれからも
リーダーとして見えます。

```cpp
EspUsbDevice device;
EspUsbDeviceCcid reader(device);

EspUsbCcidConfig config;                  // messageBytes 0 = 自動
config.name = "EspUsbDevice CCID Reader";
reader.configure(config);
reader.onApdu(answerApdu);
reader.insertCard(cardAtr, sizeof(cardAtr));
device.begin(identity);
```

## ハードウェア

- USB device 対応の ESP32-S3（または ESP32-S2 / ESP32-P4）Arduino-ESP32 ボード
- PC、または EspUsbHost を動かした別の ESP32
- ログ表示とカード出し入れのためのシリアルモニタ

## 動作

- `bInterfaceClass = 0x0b`、bulk IN / bulk OUT と挿抜通知用の interrupt IN で列挙します
  （既定の `removable = true`）。
- 起動時から slot にカードが入っています。ATR は MIFARE Classic 1K を表す PC/SC の
  合成 ATR なので、ATR からカード名を出す Host ではその名前で表示されます。
- 擬似カードは次の命令に応答します。
  - `FF CA 00 00 00`（PC/SC の Get UID）→ `04 11 22 33` と `9000`
  - `80 01 00 00 Lc <data>` → 同じデータと `9000`
  - それ以外 → `6D00`（命令を知らないカードと同じ応答）
- シリアルモニタの `i` でカードを挿入、`r` で排出します。どちらも interrupt endpoint で
  Host へ通知するので、Host 側アプリからは挿抜イベントとして見えます。
- 受けた APDU、カードの活性化・非活性化を表示し、2 秒ごとに状態の行も表示します。

## 試し方

Linux の `pcsc-lite` の場合:

```sh
pcsc_scan
```

`EspUsbDevice CCID Reader` として現れ、カード挿入時に上記の ATR が表示されます。
`opensc-tool --atr` や `scriptor` でも確認できます。

```sh
echo "FF CA 00 00 00" | scriptor      # 04 11 22 33 90 00
echo "80 01 00 00 02 AB CD" | scriptor # AB CD 90 00
```

EspUsbHost を動かした別の ESP32 と繋ぐ場合は、
[v2 tests/peer/usb_ccid](https://github.com/tanakamasayuki/EspUsbDevice/tree/3fed50d0d777f600432b71381faccd93adb9a202/tests/peer/usb_ccid/) にリーダーを open して
同じやり取りを行う Host 側スケッチがあります。

## 自分のカードを書く

カード固有の処理は `onApdu` callback だけです。Host が送った APDU を受け取り、2 byte の
status word を含む応答を `reply` へ（`capacity` byte 以内で）書きます。0 を返すと command は
失敗し、Host には ICC_MUTE のエラーとして伝わります。周辺の CCID プロトコル（slot status、
活性化、ATR、parameter、abort）はライブラリが処理するので、カード側は APDU に答えるだけです。

callback は USB worker task で実行されます。短く済ませ、中から USB API を呼び返さないで
ください。このスケッチは callback の中では表示せず、APDU を lock の中でコピーして `loop()` で
表示します。

## 注意

- 1 slot、T=1、short APDU level exchange です。chaining、extended APDU、PIN pad、
  clock / data rate の交渉は範囲外で、class descriptor でも非対応と宣言します。
- CCID メッセージの上限は `messageBytes` です。既定の 0 は `begin()` のときに、full speed では
  271 byte（short APDU 一つ分）、high speed では 512 byte（CCID が下限として求める bulk packet の
  大きさ）に決まります。スケッチは `reader.messageCapacity()` で値を表示します。増やすときは
  `config.messageBytes` を指定します。明示した値が bulk packet より小さいと、`begin()` が
  `ESP_ERR_INVALID_SIZE` で失敗します。
- 通知 endpoint を持たない固定カードにするなら `config.removable = false` とし、
  `device.begin()` より前に `insertCard()` します。
- Arduino-ESP32 標準の `USB.begin()` とは併用できません。

## 読み方と確認の手順

### 実行前の準備

boardに合うprofileを選びます（既定は`esp32s3`。ほかに`esp32s2`、v1.xのP4は`esp32p4`、
v3.1以上のP4は`esp32p4x`。v3.0は対象外）。診断Serialの接続先も確認します。
[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と
相手の利用可能状態の違いを説明しています。

### コードを追う

ATRとUIDは例の配列で、実際のリーダーchipから得た値ではありません。`answerApdu()`は入力の長さと
出力の容量を確かめてから、データとstatus byteを作ります。`i`/`r`で挿入状態を変え、PC/SCの
clientから問い合わせます。任意の実カードのアプリケーションを実装するものではありません。

### APIの使い方と改造時の注意

リーダーの列挙、interfaceのopen、カードの有無、活性化とATR、APDUの交換を分けて確認します。
CCIDの転送の成功と、アプリケーションのstatus wordは別の問いへの答えです。渡された容量と実際の
応答長の確認は残し、カードがないことを空の正常応答と解釈しないでください。

リーダー固有の擬似APDUやescape command（`onEscape`）は、すべてのカードに通じる命令ではありません。
命令を流用する前に、リーダーとカードの組合せを確認します。callbackの処理は短くし、時間のかかる
処理はスケッチのtaskで行います。

### 一つずつ確認する

最初は例を変更せずに動かし、ログとPC/SC clientの結果を照合します。カードがない状態から挿入へ
進めます。命令は一つずつ変えます。例のエラー表示は、再試行や回復の方針ではありません。

## v2からの変更点

- `ESP_USB_DEVICE_CCID_BUFFER_SIZE`は廃止しました（定義するとcompile error）。message容量は
  `EspUsbCcidConfig::messageBytes`で、既定は自動です。これによりP4のHS portでも開始できます。
- `configure()`、`onApdu()`、`onPower()`、`removeCard()`は`esp_err_t`を返し、スケッチで確認します。
- カードは`device.begin()`の前に挿入します（着脱可能なリーダーは後からでも受け付け、Hostが
  リーダーを構成した時点で通知します）。
- APDUと電源のcallbackはUSB workerの上で表示しなくなりました。記録した内容を`loop()`が表示します。

Arduino-ESP32 3.3.12で`sketch.yaml`の全profileのcompileを確認しています。このスケッチ自体の
実機試験はしていません。

- [スケッチ](SmartCardReader.ino)
- [全サンプル](../README.md)
- [CCID API](../../docs/classes/ccid.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
