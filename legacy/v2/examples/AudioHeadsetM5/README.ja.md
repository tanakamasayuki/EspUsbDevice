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

## 読み方と確認の手順

### 実行前の準備

USBの再生・録音をM5のスピーカーとマイクへつなぐ。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

再生とマイクを別々に確認してから同時に使います。PCMFlowDeviceがスピーカーをbufferし、マイクの完成blockをcapture.`write()`へ渡します。この例は再生と録音のchannel構成が異なるため、各addFormat()に合わせて解釈します。

### APIの使い方と改造時の注意

M5Unified・PCMFlow・PCMFlowDeviceを用意し、そのspeaker・mic構成に対応するboardを選びます。これらは例の依存で、EspUsbDeviceの全利用に必要なものではありません。非同期mic・speakerのbufferはハードウェア処理の完了まで残し、ringを一時的なローカル配列へ置き換えないようにします。

USBのPCM形式とplayer側の形式を明示的に合わせます。再生・録音を別々に試してから同時にし、元blockの長さと受付byte数を比べます。stream状態のeventはホストによる利用状況を示します。volume・mute eventを受けただけでは、M5側のgain処理まで自動で実装されません。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](AudioHeadsetM5.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
