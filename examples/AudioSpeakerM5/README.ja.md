# AudioSpeakerM5

> English: [README.md](README.md)

UAC1の`EspUsbAudioPlaybackStream`をPCMFlowDeviceの`M5SpeakerBufferedPlayer`へつなぎ、Hostの音声を
M5のスピーカーで鳴らす例です。現在のUSB sample rateをM5の画面に表示します。

UAC2のHostがある場合は`EspUsbAudioProtocol::Uac2`を明示します。

- USB format: 48 kHz、stereo、16-bit PCM
- `audio.configure()`でplayback ringを480 frame（10 ms）にする
- `loop()`で`playback.read()`がPCMをコピーする
- `M5SpeakerBufferedPlayer::writePcm()`が`M5.Speaker`へ渡す。playerが受け取らなかったchunkは
  `unsupported`に数える
- `audio.pollEvent()`でstream状態を受け取り、Hostがstreamを閉じたらplayerを止める

EspUsbDevice本体はM5Unified、PCMFlow、PCMFlowDeviceへ依存しません。依存するのはこのexampleだけです。
USBのvolume／muteは自動では適用しません。通知されたeventや`audio.getVolume()`／`audio.getMute()`
を使って、アプリ側でgainを実装できます。

## ハードウェア

- M5Unifiedが対応するスピーカー付きのM5Stack ESP32-S3製品（CoreS3など）。native USBの口をHostへつなぐ
- boardに別経路があれば、ログ用の`Serial`（115200）

## 依存library

`sketch.yaml`でPCMFlow 0.2.1、PCMFlowDevice 1.0.1、M5Unified 0.2.26、M5GFX 0.2.33に固定しています。
profileは`esp32s3`だけです。

## 期待される出力

```
AudioSpeakerM5 ready
speaker 1
chunks=... bytes=192000 speaker=... unsupported=0 usb overruns=0 play_chunks=... waits=... gaps=... drops=...
```

## 読み方と確認の手順

### 実行前の準備

固定したlibraryを入れ（profileが自動で入れます）、`esp32s3` profileを選び、診断Serialの接続先を
確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・
開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

M5Unifiedでスピーカーを開始し、PCMFlowDeviceのplayerを開始してから、USBのformatを登録して
`device.begin()`を呼びます。M5の音声初期化が成功してから、PCでこのUSB出力を選びます。
`bytes=`はUSBのringから読んだ量、`speaker=`はplayerが受け取った量です。

### APIの使い方と改造時の注意

USBのPCM formatとplayerのformatを明示して合わせます。playerのbufferはhardwareが使い終わるまで
有効に保ちます。mute／volumeのeventはPCMを変えないので、gainをどこで掛けるかを決めます。
playerが捨てたり待ったりした場合は、その数と`usb overruns`を比べ、どちらが遅れたかを見ます。

### 一つずつ確認する

最初は例を変更せずに動かし、formatや容量は一つずつ変えます。例のエラー表示は、再試行や回復の
方針ではありません。

## v2からの変更点

- playback ringとevent queueの容量を`audio.configure()`で決めます。build時の固定容量で開始する
  代わりに、設定の失敗を表示します。
- スケッチの読み出しが遅れると、playback ringは古い未読PCMを捨てます（`DropOldest`）。毎秒のログに
  USBのoverrunを加えました。
- USBの開始に失敗したときは、従来どおり`device.lastErrorName()`を表示します。

Arduino-ESP32 3.3.12で`esp32s3` profileのcompileを確認しています。このスケッチ自体の実機試験は
していません。

- [スケッチ](AudioSpeakerM5.ino)
- [全サンプル](../README.md)
- [Audio API](../../docs/classes/audio.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
