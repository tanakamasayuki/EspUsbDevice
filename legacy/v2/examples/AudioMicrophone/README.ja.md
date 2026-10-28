# AudioMicrophone

> English: [README.md](README.md)

defaultのUAC1 Capture functionを公開し、Device→HostへPCMを送出する例です。PCからは
録音（入力）デバイスとして見えます。ここではmono 48 kHz / 16-bitのsourceとして
440 Hzの正弦波を生成します。

```cpp
EspUsbDevice device;
EspUsbAudioFunction audio(device);
auto &capture = audio.addCaptureStream();
capture.addFormat({48000, 1, 2, 16});
```

formatのfield順はEspUsbHostと同じ語彙です。

```text
sampleRate, channels, bytesPerSample, bitsPerSample
```

`loop()`から`capture.write()`でPCMをHost方向へ送ります。書き込み先はbounded USB IN
FIFOです。実マイクの取り込みやcodec入力はアプリケーション側の仕事なので、正弦波の
生成部を実際のsourceへ置き換えてください。`audio.pollEvent()`はTinyUSB taskで
ユーザーコードを実行せずにcapture streamの開始/停止（`StreamStateChanged`）を
通知します。

UAC2が必要な場合は`EspUsbAudioFunction` constructorの第2引数へ
`EspUsbAudioProtocol::Uac2`を渡します。

Device→Host streamingはS3 peer test `usb_audio_microphone`で、Hostが無音でない
PCMを受信することを確認済みです（UAC1 / FS）。

## 読み方と確認の手順

### 実行前の準備

合成した試験音をUSBマイク入力として送る。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`addCaptureStream()`はdevice→ホストです。`loop()`で符号付きPCMを生成し、`capture.write()`でbyte列を渡します。この入力を録音して試験音を確認します。物理マイクではなく、要求byte数が有限bufferの受付量を超える場合があります。

### APIの使い方と改造時の注意

USB音声の方向はdeviceとhostの関係から読みます。PCMを解釈する前に方向・sample rate・channel数・sampleあたりbyte数・有効bit数を合わせます。frameは全channel分のsample一組であり、byte数とは違います。stereoのinterleaved配置はchannelを交互に並べます。

streamの有効化にはホストの操作が必要です。USB列挙の成功と、stream状態・不足・あふれの統計を分けて見ます。bufferとqueueは有限で、`write()`の返却長を無視する例が無欠落を保証するわけではありません。USB callbackを短くし、デコード・保存・重い解析は別へ置きます。codecや外部回路を足す前に既知の試験信号で確かめます。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](AudioMicrophone.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
