# AudioMicrophoneM5

> English: [README.md](README.md)

M5内蔵マイク（`M5.Mic`）の取り込みをUSB Audio録音デバイス（Device→Host）として
Hostへストリームする例です。

- USB format: 16 kHz / mono / 16-bit PCM。M5のPDMマイクが安定して動くレートです。
- `M5.Mic.record()`は非同期・double-bufferedなので、小さなblock ringを持ち、
  完了済みの最古blockを`capture.write()`で送ります。
- `audio.pollEvent()`でcapture streamの状態を取得し、現在のUSB sample rateを
  M5 displayに表示します。

EspUsbDevice本体はM5Unifiedへ依存しません。依存するのはこのexampleだけで、
マイク取り込みに使用します。
