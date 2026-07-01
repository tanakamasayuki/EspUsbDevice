# EspUsbDevice AudioSink

> English: [README.md](README.md)

USB Audio speaker device を作り、Host から送られてくる PCM playback data を受け取る例です。

この example は I2S DAC / codec へ出力しません。最初の Audio 実装確認用として、受信 chunk 数、
byte 数、直近 sample を Serial monitor に出力します。実際に音を出す場合は `onAudioData()` 内で
I2S や外部 codec へ PCM を渡してください。

## ハードウェア

- USB device 対応の ESP32-S3 など Arduino-ESP32 board
- PC などの USB host
- ログ確認用の Serial monitor 接続

## 動作内容

- `EspUsbDeviceAudioSink` を 48 kHz / 16-bit / stereo speaker として登録します。
- Host 側で音声出力 device として選ぶと、speaker streaming interface が有効になります。
- Host から届いた PCM chunk を `onData()` callback で受け取ります。
- `applyVolume()` で Host から設定された mute / volume を sample buffer に反映します。
- 1 秒ごとに受信 chunk 数、byte 数、直近 sample を Serial monitor に出力します。

## 主要 API

- `EspUsbDeviceAudioSink audio(device, 48000, ESP_USB_DEVICE_AUDIO_BITS_16, ESP_USB_DEVICE_AUDIO_SPK_STEREO)`
  は USB Audio speaker function を登録します。
- `audio.onData(callback)` は Host -> Device の speaker PCM を受け取ります。
- `audio.onEvent(callback)` は volume、mute、sample rate、interface enable の変更を受け取ります。
- `audio.applyVolume(data, length)` は Audio class の volume / mute 状態を PCM buffer に反映します。
- `audio.writeMic(data, length)` は microphone path を有効にした場合に Device -> Host へ PCM を送ります。

## 注意

- この example は speaker sink の最小例です。I2S pin、DMA buffer、codec 初期化は扱いません。
- Arduino Core の USB Audio 実装をベースにした機能です。対象 board / core 設定で TinyUSB Audio が有効である必要があります。
- 現在は Audio 単独 device として使う想定です。HID / CDC / MSC などとの複合 Audio device は未対応です。
- full-speed では 48 kHz / 16-bit / stereo 程度を基本にしてください。
- Serial monitor はログ確認用です。Audio stream は USB device port 側で確認してください。

## 関連

- [MIDI](../MIDI/) - USB MIDI device
- [Serial](../Serial/) - CDC ACM serial device
