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
