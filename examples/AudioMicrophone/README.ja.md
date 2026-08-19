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
