# AudioSpeaker

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

UAC1の列挙、streaming、mute、volume、control連打はS3 peer testで確認済みです。
