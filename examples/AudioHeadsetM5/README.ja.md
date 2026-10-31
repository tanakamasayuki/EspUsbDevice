# AudioHeadsetM5

> English: [README.md](README.md)

M5ハードウェアで動くUSB Audio headsetの例です。Hostの音声をM5スピーカーで再生し（Host→Device）、
M5マイクの取り込みをHostへ返します（Device→Host）。共有のUSB sample rateをM5の画面に表示します。

- USB format: 48 kHz / 16-bit、speakerはstereo、microphoneはmono。speakerとmicrophoneは1つの
  USB sample rateを共有するので、両方に48 kHzを登録します。
- `audio.configure()`で両方向のringを10 ms分にします。
- 受信したPCMはPCMFlowDeviceの`M5SpeakerBufferedPlayer::writePcm()`経由で`M5.Speaker`へ出力します。
- `M5.Mic.record()`の取り込みは10 msのblockを3つ持つringを使い、完了済みの最古blockを
  `capture.write()`で送ります。`write()`が受け取らなかったbyteは`rejected`に数えます。

**既知の制約:** M5のスピーカーとマイクの同時使用（全二重）は安定して動作しません。M5Unifiedは両方を
1つのI2S portでTX専用/RX専用にinstallし、それぞれの`begin()`が相手のdriverをuninstallするため、
再生＋録音の同時利用はglitchします（CoreS3でも確認。スピーカーを別I2S portへ移しても改善せず）。
これはM5Unified側の性質で、EspUsbDevice v3でも変わりません。このexampleはベストエフォートの
デモです。安定動作が必要なら単方向の[AudioSpeakerM5](../AudioSpeakerM5/)（再生）／
[AudioMicrophoneM5](../AudioMicrophoneM5/)（録音）を、両方向のデモは非M5の
[AudioHeadset](../AudioHeadset/)（USBのみ）を使ってください。

EspUsbDevice本体はM5Unified、PCMFlow、PCMFlowDeviceへ依存しません。依存するのはこのexampleだけです。

## ハードウェア

- M5Unifiedが対応するスピーカーとマイク付きのM5Stack ESP32-S3製品（CoreS3など）。native USBの口を
  Hostへつなぐ
- boardに別経路があれば、ログ用の`Serial`（115200）

## 依存library

`sketch.yaml`でPCMFlow 0.2.1、PCMFlowDevice 1.0.1、M5Unified 0.2.26、M5GFX 0.2.33に固定しています。
profileは`esp32s3`だけです。

## 読み方と確認の手順

### 実行前の準備

固定したlibraryを入れ（profileが自動で入れます）、`esp32s3` profileを選び、診断Serialの接続先を
確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・
開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

両方を使う前に、再生とマイクを別々に確かめます。PCMFlowDeviceがスピーカー側のbufferを持ち、
完了したマイクのblockを`capture.write()`へ渡します。この例ではplaybackとcaptureのchannel数が
違うので、各streamはそれぞれの`addFormat()`の宣言で解釈します。

### APIの使い方と改造時の注意

非同期のマイク・スピーカーのbufferは、hardwareの処理が終わるまで有効に保ちます。関数内の一時配列は
ringの代わりになりません。USBのPCM formatとplayerのformatを明示して合わせます。stream状態のeventで
Hostが各方向を使っているかがわかります。USBのvolume／muteのeventを受けても、M5側のgainは
自動では変わりません。

### 一つずつ確認する

最初は例を変更せずに動かし、formatや容量は一つずつ変えます。例のエラー表示は、再試行や回復の
方針ではありません。

## v2からの変更点

- ringとeventの容量は`audio.configure()`で決めます。
- v3の`write()`は送信待ちのPCMを上書きせず、受け取った量を返します。各マイクblockの残りは
  `rejected`として数えます。
- スピーカーへ渡した量、送った量、`rejected`を1秒ごとに表示します。

Arduino-ESP32 3.3.12で`esp32s3` profileのcompileを確認しています。このスケッチ自体の実機試験は
していません。

- [スケッチ](AudioHeadsetM5.ino)
- [全サンプル](../README.md)
- [Audio API](../../docs/classes/audio.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
