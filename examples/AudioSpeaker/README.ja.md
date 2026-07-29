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
