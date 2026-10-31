# AudioMicrophone

> English: [README.md](README.md)

UAC1のCapture functionを公開し、Device→HostへPCMを送る例です。PCからは録音（入力）デバイスとして
見えます。mono 48 kHz / 16-bitのsourceとして440 Hzの正弦波を生成します。

```cpp
EspUsbDevice device;
EspUsbAudioFunction audio(device);
auto &capture = audio.addCaptureStream();

capture.addFormat({48000, 1, 2, 16});
EspUsbAudioConfig config;
config.captureBufferFrames = 480;   // 48 kHz mono 16-bitの10 ms分
audio.configure(config);
device.begin(identity);
```

formatのfield順はEspUsbHostと同じ語彙です。

```text
sampleRate, channels, bytesPerSample, bitsPerSample
```

`capture.write()`はPCMをringへコピーし、USB workerがHostへ送り出します。入る分だけを受け取って
そのbyte数を返し、送信待ちのPCMを上書きしません。そこでスケッチは、`capture.availableForWrite()`が
1 ms分のblockを丸ごと受け取れるときだけ次のblockを生成します。生成したsampleを失わず、正弦波も
途切れません。ringが空になるとHostには無音が届き、`stats().underrunCount`／`underrunBytes`に数えます。

実マイクの取り込みやcodec入力はアプリケーション側の仕事です。`fillBlock()`を実際のsourceへ置き換えて
ください。`audio.pollEvent()`はcapture streamの開始／停止（`StreamStateChanged`）をスケッチのtaskで
取り出します。

UAC2が必要な場合は`EspUsbAudioFunction` constructorの第2引数へ`EspUsbAudioProtocol::Uac2`を渡します。

## ハードウェア

- ESP32-S3、ESP32-S2、ESP32-P4のboard。native USB OTGの口をHostへつなぐ
- `Serial`（115200）用の別のdebug接続

## 期待される出力

```
Microphone ready: UAC1, 48000 Hz, 16-bit, mono
capture enabled=1 alt=1
written=96000 usb=96000 underruns=0/0 bytes
```

新しい入力デバイスから録音すると、440 Hzの一定の音が入ります。

## 読み方と確認の手順

### 実行前の準備

boardに合うprofileを選びます（既定は`esp32s3`。ほかに`esp32s2`、v1.xのP4は`esp32p4`、
v3.1以上のP4は`esp32p4x`。v3.0は対象外）。診断Serialの接続先も確認します。
[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と
相手の利用可能状態の違いを説明しています。

### コードを追う

`addCaptureStream()`はDevice→Hostです。`loop()`はringに空きがある間だけ符号付きPCMのblockを作り、
USB workerがHostの要求に合わせて送ります。Hostが録音を始める前に書いたPCMは最初のstream開始まで
保持し、streamを止めるとringを消します。

### APIの使い方と改造時の注意

PCMを作る前に方向・sample rate・channel数・sampleあたりbyte数・有効bit数を合わせます。
frameは全channel分のsample一組で、`write()`はframe単位でだけ受け取ります。戻り値は必ず使います。
待てないsource（マイクなど）では、入りきらなかった分をどう扱うかを決める必要があります
（[AudioMicrophoneM5](../AudioMicrophoneM5/)参照）。

### 一つずつ確認する

最初は例を変更せずに動かし、ログと録音アプリの表示を照合します。formatや容量は一つずつ変えます。
例のエラー表示は、再試行や回復の方針ではありません。

## v2からの変更点

- capture ringの容量は、build flagではなく`audio.configure()`で決めます。
- `write()`は部分書込みです。古いPCMを上書きせず、受け取った量を返します。v2のスケッチは戻り値を
  無視していましたが、この例は入る分だけを生成します。
- 書いたbyte数、USBで送ったbyte数、underrunを1秒ごとに表示します。

Arduino-ESP32 3.3.12で`sketch.yaml`の全profileのcompileを確認しています。このスケッチ自体の
実機試験はしていません。

- [スケッチ](AudioMicrophone.ino)
- [全サンプル](../README.md)
- [Audio API](../../docs/classes/audio.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
