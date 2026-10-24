# AudioHeadset

> English: [README.md](README.md)

speaker（Host→Device）とmicrophone（Device→Host）を同時に担う1つのUSB Audio
デバイス、つまりloopback headsetの例です。`playback.read()`で読んだPCMを
`capture.write()`でHostへ返すので、PCが再生した音がそのまま録音入力へ戻ります。

```cpp
EspUsbDevice device;
EspUsbAudioFunction audio(device);
auto &playback = audio.addPlaybackStream();
auto &capture = audio.addCaptureStream();
playback.addFormat({48000, 1, 2, 16});
capture.addFormat({48000, 1, 2, 16});
```

Hostからは再生デバイス1つと録音デバイス1つに見えます。両方向はmono 48 kHz /
16-bitのformatを共有します。このstackのUSB Audioデバイスはsample rateを両方向で
1つだけ使います。`audio.pollEvent()`は各streamの状態を個別に通知します
（`event.target`が`Playback`または`Capture`）。

UAC2が必要な場合は`EspUsbAudioFunction` constructorの第2引数へ
`EspUsbAudioProtocol::Uac2`を渡します。

両方向同時の動作はS3 peer test `usb_audio_headset`で確認済みです。OUT/IN両stream
のenumerate/開始、Hostのspeaker PCMがDeviceへ届くこと、Deviceのmic PCMがHostへ
届いて無音でないことを検証します（UAC1 / FS）。

## 読み方と確認の手順

### 実行前の準備

受けた再生PCMをUSB録音streamへ折り返す。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`device.begin()`前に再生・録音のformatを設定します。PC側で両方向を選び、`playback.read()`で受けたbyteをcapture.`write()`へ渡します。両方でホストの利用が必要で、`capture.write()`が受け付けなかったbyteをすべて保持する構成ではありません。

### APIの使い方と改造時の注意

USB音声の方向はdeviceとhostの関係から読みます。PCMを解釈する前に方向・sample rate・channel数・sampleあたりbyte数・有効bit数を合わせます。frameは全channel分のsample一組であり、byte数とは違います。stereoのinterleaved配置はchannelを交互に並べます。

streamの有効化にはホストの操作が必要です。USB列挙の成功と、stream状態・不足・あふれの統計を分けて見ます。bufferとqueueは有限で、`write()`の返却長を無視する例が無欠落を保証するわけではありません。USB callbackを短くし、デコード・保存・重い解析は別へ置きます。codecや外部回路を足す前に既知の試験信号で確かめます。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](AudioHeadset.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
