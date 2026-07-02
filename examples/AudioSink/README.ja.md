# EspUsbDevice AudioSink

> English: [README.md](README.md)

USB Audio speaker device を作り、Host から送られてくる PCM playback data を受け取る例です。

この example は I2S DAC / codec へ出力しません。最初の Audio 実装確認用として、受信 chunk 数、
byte 数、直近 sample を Serial monitor に出力します。実際に音を出す場合は `onPcm()` で受け取った
PCM をアプリケーション、PCMFlow、PCMFlowDevice などへ渡してください。

## ハードウェア

- USB device 対応の ESP32-S3 など Arduino-ESP32 board
- PC などの USB host
- ログ確認用の Serial monitor 接続

## 動作内容

- `EspUsbDeviceAudioSink` を 48 kHz / 16-bit / stereo speaker として登録します。
- Host 側で音声出力 device として選ぶと、speaker streaming interface が有効になります。
- Host から届いた PCM chunk を `onPcm()` callback で受け取ります。
- `applyVolume()` で Host から設定された mute / volume を sample buffer に反映します。
- 1 秒ごとに受信 chunk 数、byte 数、直近 sample を Serial monitor に出力します。

## 主要 API

- `EspUsbDeviceAudioSink audio(device, 48000, ESP_USB_DEVICE_AUDIO_BITS_16, ESP_USB_DEVICE_AUDIO_SPK_STEREO)`
  は USB Audio speaker function を登録します。
- `audio.onPcm(callback)` は Host -> Device の speaker PCM を受け取ります。callback には
  `data`、`length`、`sampleRate`、`channels`、`bytesPerSample` が入った `EspUsbDeviceAudioPcm`
  が渡されます。
- `audio.onData(callback)` は `void *data` と `length` だけを受け取る低レベル callback です。
- `audio.onEvent(callback)` は volume、mute、sample rate、interface enable の変更を受け取ります。
- `audio.applyVolume(data, length)` は Audio class の volume / mute 状態を PCM buffer に反映します。
- `audio.writeMic(data, length)` は microphone path を有効にした場合に Device -> Host へ PCM を送ります。

## PCM の受け渡し

`onPcm()` は PCMFlow などに渡しやすいように、buffer と format 情報をまとめて渡します。
このライブラリは受信した PCM を所有しないため、callback の外で使う場合はアプリケーション側で
buffer をコピーしてください。callback 内で `applyVolume()` を呼ぶと、Host 側の mute / volume 設定を
反映した PCM を後段へ渡せます。

## 注意

- この example は speaker sink の最小例です。I2S pin、DMA buffer、codec 初期化は扱いません。
- このライブラリは USB Audio class と PCM callback までを担当します。I2S bridge や codec / DAC 接続は PCMFlowDevice など出力側ライブラリの責務です。
- Arduino Core の USB Audio 実装をベースにした機能です。対象 board / core 設定で TinyUSB Audio が有効である必要があります。
- 現在は Audio 単独 device として使う想定です。HID / CDC / MSC などとの複合 Audio device は未対応です。
- full-speed では 48 kHz / 16-bit / stereo 程度を基本にしてください。
- Serial monitor はログ確認用です。Audio stream は USB device port 側で確認してください。

## 関連

- [PCMFlow](https://github.com/tanakamasayuki/PCMFlow) - PCM data flow
- [PCMFlowDevice](https://github.com/tanakamasayuki/PCMFlowDevice) - PCM output device integration
- [MIDI](../MIDI/) - USB MIDI device
- [Serial](../Serial/) - CDC ACM serial device
