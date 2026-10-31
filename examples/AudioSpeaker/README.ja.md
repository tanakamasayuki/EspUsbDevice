# AudioSpeaker

> English: [README.md](README.md)

UAC1のPlayback functionを公開し、Hostから届いたPCMを容量の決まったringから読む例です。
PCからは出力（スピーカー）デバイスとして見えます。UAC2が必要な場合は
`EspUsbAudioFunction` constructorの第2引数へ`EspUsbAudioProtocol::Uac2`を渡します。

```cpp
EspUsbDevice device;
EspUsbAudioFunction audio(device);
auto &playback = audio.addPlaybackStream();

playback.addFormat({48000, 2, 2, 16});   // falseなら理由はdevice.lastError()
EspUsbAudioConfig config;
config.eventCapacity = 16;
config.playbackBufferFrames = 480;       // 48 kHz stereo 16-bitの10 ms分
audio.configure(config);                 // ESP_OKか失敗理由を返す
device.begin(identity);
```

formatのfield順はEspUsbHostと同じ語彙です。

```text
sampleRate, channels, bytesPerSample, bitsPerSample
```

`loop()`から`playback.read()`を呼び、コピー済みPCMをI2S、codec、PCMFlowなどへ渡します。
ringは`playbackBufferFrames`分のframeを持ちます。読み出しが遅れると、既定の`DropOldest`方針で
古い未読PCMを捨て、`stats().overrunCount`／`overrunBytes`に数えます。

`audio.pollEvent()`はstream、sample rate、mute、volumeの変化をスケッチのtaskで取り出します。
USB workerの上でスケッチのコードは動きません。volume／muteをPCMへ暗黙に適用することはありません。

feature unitの状態は直接取得・設定もできます。

```cpp
bool leftMuted;
int16_t rightVolume;
audio.getMute(leftMuted, EspUsbAudioDirection::Playback, 1);
audio.getVolume(rightVolume, EspUsbAudioDirection::Playback, 2);
```

channelは`0` Master、`1` Left、`2` Rightです。volumeは1/256 dB単位（−90 dB〜0 dB、1 dB刻み）です。
スケッチは1秒ごとにbyte数とmasterのmute／volumeを表示します。

## ハードウェア

- ESP32-S3、ESP32-S2、ESP32-P4のboard。native USB OTGの口をHostへつなぐ
- `Serial`（115200）用の別のdebug接続

DAC・I2S・スピーカーは駆動しないので、boardから音が出ないのは正常です。

## 期待される出力

```
Speaker ready: UAC1, 48000 Hz, 16-bit, stereo
stream enabled=1 alt=1
read=192000 usb=192000 overruns=0/0 bytes mute=0 volume=0/256 dB dropped events=0
```

## 読み方と確認の手順

### 実行前の準備

boardに合うprofileを選びます（既定は`esp32s3`。ほかに`esp32s2`、v1.xのP4は`esp32p4`、
v3.1以上のP4は`esp32p4x`。v3.0は対象外）。診断Serialの接続先も確認します。
[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と
相手の利用可能状態の違いを説明しています。

### コードを追う

formatとringの容量は`device.begin()`の前に決めます。PCの出力にこのデバイスを選び、`read=`のbyte数と
eventを見ます。`usb=`はUSBで届いた量、`read=`はスケッチがringから取り出した量です。

### APIの使い方と改造時の注意

PCMを解釈する前に方向・sample rate・channel数・sampleあたりbyte数・有効bit数を合わせます。
frameは全channel分のsample一組で、`read()`はframe境界に切り下げたbyte数を返します。
streamはHostがalternate settingを選んだときに始まるので、列挙の成功とは分けて確認します。
format・rateは`addFormat()`を重ねて増やせます（[AudioPlayback](../AudioPlayback/AudioPlayback.ino)参照）。

### 一つずつ確認する

最初は例を変更せずに動かし、ログとHostの音声設定を照合します。formatや容量は一つずつ変えます。
例のエラー表示は、再試行や回復の方針ではありません。

## v2からの変更点

- ringとevent queueの容量は、build flagではなく`audio.configure()`で決めます。失敗は`configure()`
  または`device.begin()`の戻り値でわかります。
- playback ringが満杯のときは、既定で古い未読PCMを捨てます（`DropOldest`）。未読PCMを残して新着を
  捨てたい場合は`config.playbackOverflowPolicy = EspUsbAudioOverflowPolicy::DropNewest`を指定します。
- `SampleRateChanged` eventと`overrunBytes`も表示します。

Arduino-ESP32 3.3.12で`sketch.yaml`の全profileのcompileを確認しています。このスケッチ自体の
実機試験はしていません。

- [スケッチ](AudioSpeaker.ino)
- [全サンプル](../README.md)
- [Audio API](../../docs/classes/audio.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
