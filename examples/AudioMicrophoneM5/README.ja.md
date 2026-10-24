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

## 読み方と確認の手順

### 実行前の準備

M5のマイクPCMをUSB録音streamへ送る。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`M5.Mic.record()`とringのindexをcapture.`write()`の前に追います。現在のblockを非同期録音が使えるよう、完成済みblockを送ります。録音applicationでUSB入力を選び、録音中bufferを早く再利用しないようにします。

### APIの使い方と改造時の注意

M5Unified・PCMFlow・PCMFlowDeviceを用意し、そのspeaker・mic構成に対応するboardを選びます。これらは例の依存で、EspUsbDeviceの全利用に必要なものではありません。非同期mic・speakerのbufferはハードウェア処理の完了まで残し、ringを一時的なローカル配列へ置き換えないようにします。

USBのPCM形式とplayer側の形式を明示的に合わせます。再生・録音を別々に試してから同時にし、元blockの長さと受付byte数を比べます。stream状態のeventはホストによる利用状況を示します。volume・mute eventを受けただけでは、M5側のgain処理まで自動で実装されません。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](AudioMicrophoneM5.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
