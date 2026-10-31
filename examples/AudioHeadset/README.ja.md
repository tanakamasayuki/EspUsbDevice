# AudioHeadset

> English: [README.md](README.md)

speaker（Host→Device）とmicrophone（Device→Host）を同時に担う1つのUSB Audioデバイス、つまり
loopback headsetの例です。`playback.read()`で読んだPCMを`capture.write()`でHostへ返すので、
PCが再生した音がそのまま録音入力へ戻ります。

```cpp
EspUsbDevice device;
EspUsbAudioFunction audio(device);
auto &playback = audio.addPlaybackStream();
auto &capture = audio.addCaptureStream();

playback.addFormat({48000, 1, 2, 16});
capture.addFormat({48000, 1, 2, 16});
EspUsbAudioConfig config;
config.playbackBufferFrames = 480;   // 両方向とも10 ms分
config.captureBufferFrames = 480;
audio.configure(config);
device.begin(identity);
```

Hostからは再生デバイス1つと録音デバイス1つに見えます。両方向ともmono 48 kHz / 16-bitです。
1つの`EspUsbAudioFunction`はclockを1つだけ持つので、両方向に同じsample rateの集合を登録します。
食い違うと`device.begin()`が`ESP_ERR_INVALID_ARG`で拒否します。`audio.pollEvent()`は各streamの
状態を個別に通知します（`event.target`が`Playback`または`Capture`）。

`loop()`は`capture.availableForWrite()`が受け取れる分だけplayback PCMを読むので、読んだbyteは
すべて折り返されます。Hostが録音していないとcapture ringが埋まり、playbackを読まなくなり、
playback ringが古いPCMを捨てます（`DropOldest`。`overrunCount`に数える）。

UAC2が必要な場合は`EspUsbAudioFunction` constructorの第2引数へ`EspUsbAudioProtocol::Uac2`を渡します。

## ハードウェア

- ESP32-S3、ESP32-S2、ESP32-P4のboard。native USB OTGの口をHostへつなぐ
- `Serial`（115200）用の別のdebug接続

## 期待される出力

```
Headset ready: UAC1, 48000 Hz, 16-bit, mono
playback enabled=1 alt=1
capture enabled=1 alt=1
echoed=96000 playback overruns=0 capture underruns=0
```

## 読み方と確認の手順

### 実行前の準備

boardに合うprofileを選びます（既定は`esp32s3`。ほかに`esp32s2`、v1.xのP4は`esp32p4`、
v3.1以上のP4は`esp32p4x`。v3.0は対象外）。診断Serialの接続先も確認します。
[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と
相手の利用可能状態の違いを説明しています。

### コードを追う

両方向のformatとringは`device.begin()`の前に決めます。PCの音声アプリで出力と入力の両方にこの
デバイスを選び、再生と録音を同時に行います。両方向ともHostの操作が必要です。

### APIの使い方と改造時の注意

USB音声の方向はdeviceとhostの関係から読みます。PCMを解釈する前に方向・sample rate・channel数・
sampleあたりbyte数・有効bit数を合わせます。streamの有効化は方向ごとにHostが行うので、
stream状態とoverrun／underrunの統計を、列挙の成功と分けて見ます。デコードや解析はcallbackではなく
スケッチのtaskで行います。

### 一つずつ確認する

最初は例を変更せずに動かし、formatや容量は一つずつ変えます。例のエラー表示は、再試行や回復の
方針ではありません。

## v2からの変更点

- ringとeventの容量は`audio.configure()`で決めます。
- v2は読んだbyteをすべて`capture.write()`へ渡し、戻り値を無視していました。v3の`write()`は
  部分書込みなので、この例はcaptureが受け取れる分だけを読み、読んだ分を失いません。
- 折り返したbyte数、playbackのoverrun、captureのunderrunを1秒ごとに表示します。

Arduino-ESP32 3.3.12で`sketch.yaml`の全profileのcompileを確認しています。このスケッチ自体の
実機試験はしていません。

- [スケッチ](AudioHeadset.ino)
- [全サンプル](../README.md)
- [Audio API](../../docs/classes/audio.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
