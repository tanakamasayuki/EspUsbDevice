# EspUsbDevice AudioSinkM5Speaker

> English: [README.md](README.md)

PC などの Host から USB Audio speaker として PCM を受け取り、M5Stack 系 board の内蔵 speaker で再生する例です。

この example は EspUsbDevice、PCMFlow、PCMFlowDevice、M5Unified の連携例です。EspUsbDevice 本体は
PCMFlow / PCMFlowDevice に依存しません。USB Audio の受信は `EspUsbDeviceAudioSink`、speaker への
安定した受け渡しは PCMFlowDevice の `M5SpeakerBufferedPlayer` が担当します。

## ハードウェア

- USB device 対応の ESP32-S3 搭載 M5Stack 系 board
- PC などの USB host
- M5Unified で `M5.Speaker` が使える board
- ログ確認用の Serial monitor 接続

## 動作内容

- PC からは 48 kHz / 16-bit / stereo の USB Audio speaker として見えます。
- `audio.onPcm()` で Host -> Device の PCM chunk を受け取ります。
- `audio.applyVolume()` で Host 側の mute / volume を PCM buffer に反映します。
- PCMFlowDevice の `M5SpeakerBufferedPlayer::writePcm()` に PCM と format 情報を渡します。
- `writePcm()` が stereo 16-bit PCM を mono 16-bit PCM に downmix し、M5 の内蔵 speaker で再生します。
- 1 秒ごとに受信 chunk 数、再生 chunk 数、wait / gap / drop 数を Serial monitor に出力します。

## データの流れ

```text
PC / Host
  -> USB Audio speaker stream
  -> EspUsbDeviceAudioSink::onPcm()
  -> applyVolume()
  -> M5SpeakerBufferedPlayer::writePcm()
  -> M5.Speaker
```

## 主要 API

- `EspUsbDeviceAudioSink` は USB Audio speaker function を登録します。
- `audio.onPcm(callback)` は PCM buffer と `sampleRate`、`channels`、`bytesPerSample` をまとめて受け取ります。
- `M5SpeakerBufferedPlayer::writePcm(data, bytes, format)` は 16-bit mono / stereo PCM を受け取り、
  stereo 入力は mono に downmix します。
- `M5SpeakerBufferedPlayer` は `M5.Speaker.playRaw()` 用の短期 buffer も保持します。

## 注意

- この example は M5 speaker 再生まで含むため、`PCMFlow`、`PCMFlowDevice`、`M5Unified`、`M5GFX` が必要です。
- `M5SpeakerBufferedPlayer::writePcm()` は同一 sample rate の 16-bit mono / stereo PCM を受け取ります。
  resampling や bit depth 変換が必要な場合は、PCMFlow 側の pipeline を別途挟む設計にしてください。
- USB callback で受け取る PCM buffer は callback の外で保持しないでください。保持が必要な場合はアプリケーション側でコピーします。
- full-speed USB Audio の基本形として 48 kHz / 16-bit / stereo を使います。
- 実機で音切れが出る場合は、PCMFlowDevice 側の profile、chunk size、buffer 数の調整対象です。

## 関連

- [AudioSink](../AudioSink/) - USB Audio speaker sink の最小例
- [PCMFlow](https://github.com/tanakamasayuki/PCMFlow) - PCM data flow
- [PCMFlowDevice](https://github.com/tanakamasayuki/PCMFlowDevice) - PCM output device integration
