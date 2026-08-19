# AudioSpeakerM5

> English: [README.md](README.md)

defaultのUAC1 `EspUsbAudioPlaybackStream`とPCMFlowDeviceの
`M5SpeakerBufferedPlayer`を接続する例です。

UAC2 Hostを使う場合は`EspUsbAudioProtocol::Uac2`を明示選択します。

- USB format: 48 kHz / stereo / 16-bit PCM
- `loop()`で`playback.read()`してPCMをコピー
- `M5SpeakerBufferedPlayer::writePcm()`から`M5.Speaker`へ出力
- `audio.pollEvent()`でstream stateを取得

EspUsbDevice本体はM5Unified、PCMFlow、PCMFlowDeviceへ依存しません。依存するのは
このexampleだけです。USB volume/muteは自動適用せず、必要ならeventからアプリ側DSPを
実装します。
