# AudioHeadsetM5

> English: [README.md](README.md)

M5ハードウェアで動くUSB Audio headsetの例です。Hostの音声をM5スピーカーで再生し
（Host→Device）、M5マイクの取り込みをHostへ返します（Device→Host）。

- USB format: 48 kHz / 16-bit、speakerはstereo、microphoneはmono。speakerと
  microphoneは1つのUSB sample rateを共有します。
- 受信したPCMはPCMFlowDeviceの`M5SpeakerBufferedPlayer::writePcm()`経由で
  `M5.Speaker`へ出力します。
- `M5.Mic.record()`の取り込みは小さなblock ringを使い、完了済みの最古blockを
  `capture.write()`で送ります。

**既知の制約:** M5のスピーカーとマイクの同時使用（全二重）は安定して動作しません。
M5Unifiedは両方を1つのI2S portでTX専用/RX専用にinstallし、それぞれの`begin()`が
相手のdriverをuninstallするため、再生＋録音の同時利用はglitchします（CoreS3でも
確認。スピーカーを別I2S portへ移しても改善せず）。このexampleはベストエフォートの
デモです。安定動作が必要なら単方向の`AudioSpeakerM5`（再生）/`AudioMicrophoneM5`
（録音）を、両方向のデモは非M5の`AudioHeadset`（USBのみ）を使ってください。

EspUsbDevice本体はM5Unified、PCMFlow、PCMFlowDeviceへ依存しません。依存するのは
このexampleだけです。
