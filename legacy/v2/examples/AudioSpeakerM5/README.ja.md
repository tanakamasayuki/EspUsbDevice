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

## 読み方と確認の手順

### 実行前の準備

受信したUSB PCMをM5のスピーカーで鳴らす。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

M5Unified・PCMFlow・`PCMFlowDeviceでplayback.read()`からM5SpeakerBufferedPlayerへつなぎます。M5の音声初期化成功後にPCのUSB出力へ選びます。mute・volumeイベントはPCMへ自動適用されないため、どこでgainを反映するかを決めます。

### APIの使い方と改造時の注意

M5Unified・PCMFlow・PCMFlowDeviceを用意し、そのspeaker・mic構成に対応するboardを選びます。これらは例の依存で、EspUsbDeviceの全利用に必要なものではありません。非同期mic・speakerのbufferはハードウェア処理の完了まで残し、ringを一時的なローカル配列へ置き換えないようにします。

USBのPCM形式とplayer側の形式を明示的に合わせます。再生・録音を別々に試してから同時にし、元blockの長さと受付byte数を比べます。stream状態のeventはホストによる利用状況を示します。volume・mute eventを受けただけでは、M5側のgain処理まで自動で実装されません。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](AudioSpeakerM5.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
