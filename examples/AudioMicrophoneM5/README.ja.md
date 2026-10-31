# AudioMicrophoneM5

> English: [README.md](README.md)

M5内蔵マイク（`M5.Mic`）の音を、USB Audioの録音デバイスとしてHostへ送る例です（Device→Host）。
現在のUSB sample rateをM5の画面に表示します。

- USB format: 16 kHz、mono、16-bit PCM。M5のPDMマイクはこのrateで無理なく動きます。
- `M5.Mic.record()`は非同期の二重bufferなので、スケッチは15 msのblockを3つ持つringを使い、
  完了済みの最古のblockを`capture.write()`で送ります。
- `audio.configure()`でcapture ringを2 block分（480 frame）にします。
- `capture.write()`は入る分だけを受け取ります。マイクは待てないので、受け取られなかったbyteは捨て、
  `rejected`に数えます。
- `audio.pollEvent()`でcapture streamの状態を受け取り、Hostが録音している間だけblockを送ります。

EspUsbDevice本体はM5Unifiedへ依存しません。依存するのはマイクの取り込みに使うこのexampleだけです。

## ハードウェア

- M5Unifiedが対応するマイク付きのM5Stack ESP32-S3製品（CoreS3など）。native USBの口をHostへつなぐ
- boardに別経路があれば、ログ用の`Serial`（115200）

## 依存library

`sketch.yaml`でM5Unified 0.2.26、M5GFX 0.2.33に固定しています。profileは`esp32s3`だけです。

## 期待される出力

```
AudioMicrophoneM5 ready
mic 1
mic enabled=1 sent=32000 rejected=0 underruns=0
```

## 読み方と確認の手順

### 実行前の準備

固定したlibraryを入れ（profileが自動で入れます）、`esp32s3` profileを選び、診断Serialの接続先を
確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・
開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`capture.write()`の前の`M5.Mic.record()`とringの番号を追います。非同期の録音が今のblockを使えるよう、
前に完了したblockを送ります。録音アプリでこのUSB入力を選びます。

### APIの使い方と改造時の注意

非同期で録音中のblockは、録音が終わるまで有効に保ちます。関数内の一時配列ではringの代わりに
なりません。`sent`をsourceの量（ここでは毎秒32000 byte）と比べます。`rejected`が増え続けるなら
Hostの読出しがマイクより遅く、`underruns`が増えるならHostのほうが速いということです。

### 一つずつ確認する

最初は例を変更せずに動かし、formatや容量は一つずつ変えます。例のエラー表示は、再試行や回復の
方針ではありません。

## v2からの変更点

- capture ringの容量は`audio.configure()`で決めます。
- v3の`write()`は送信待ちのPCMを上書きせず、受け取った量を返します。受け取られなかったbyteを
  無視せず`rejected`として数えるようにしました。
- `capture.stats()`のunderrunをログに加えました。

Arduino-ESP32 3.3.12で`esp32s3` profileのcompileを確認しています。このスケッチ自体の実機試験は
していません。

- [スケッチ](AudioMicrophoneM5.ino)
- [全サンプル](../README.md)
- [Audio API](../../docs/classes/audio.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
