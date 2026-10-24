# AudioSpeaker

> English: [README.md](README.md)

defaultのUAC1 Playback functionを公開し、Hostから届いたPCMをbounded FIFOから読む例です。
UAC2が必要な場合は`EspUsbAudioFunction` constructorの第2引数へ
`EspUsbAudioProtocol::Uac2`を渡します。

```cpp
EspUsbDevice device;
EspUsbAudioFunction audio(device);
auto &playback = audio.addPlaybackStream();
playback.addFormat({48000, 2, 2, 16});
```

formatのfield順はEspUsbHostと同じ語彙です。

```text
sampleRate, channels, bytesPerSample, bitsPerSample
```

`loop()`から`playback.read()`を呼び、コピー済みPCMをI2S、codec、PCMFlowなどへ
渡します。`audio.pollEvent()`はTinyUSB taskでユーザーコードを実行せずにstream、
mute、volume、sample rate変更を通知します。volume/mute DSPはPCMへ暗黙適用しません。

feature unitの状態は直接取得・設定もできます。

```cpp
bool leftMuted;
int16_t rightVolume;
audio.getMute(leftMuted, EspUsbAudioDirection::Playback, 1);
audio.getVolume(rightVolume, EspUsbAudioDirection::Playback, 2);
```

channelは`0` Master、`1` Left、`2` Rightです。volumeは1/256 dB単位です。

UAC1の列挙、streaming、mute、volume、control連打はS3 peer testで確認済みです。
UAC2 descriptor/class requestはtest済みですが、UAC2 streamingはEspUsbHost側の
UAC2対応後に確認します。

## 読み方と確認の手順

### 実行前の準備

物理スピーカーを駆動せず、再生PCMを受信する。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`addPlaybackStream()`はホスト→deviceの音声です。PCの出力に選び、`playback.read()`のbyte数とpollEvent()の状態を見ます。PCMを観察する例であり、DAC・I2S・スピーカーdriverはつないでいないため、基板から音が出ないこと自体は正常です。

### APIの使い方と改造時の注意

USB音声の方向はdeviceとhostの関係から読みます。PCMを解釈する前に方向・sample rate・channel数・sampleあたりbyte数・有効bit数を合わせます。frameは全channel分のsample一組であり、byte数とは違います。stereoのinterleaved配置はchannelを交互に並べます。

streamの有効化にはホストの操作が必要です。USB列挙の成功と、stream状態・不足・あふれの統計を分けて見ます。bufferとqueueは有限で、`write()`の返却長を無視する例が無欠落を保証するわけではありません。USB callbackを短くし、デコード・保存・重い解析は別へ置きます。codecや外部回路を足す前に既知の試験信号で確かめます。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](AudioSpeaker.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
