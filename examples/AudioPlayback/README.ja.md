# EspUsbDevice AudioPlayback

> English: [README.md](README.md)

2 つの PCM 形状と 2 つの sample rate を持つ USB Audio Class 2.0（UAC2）の speaker です。Host がどれを選んだかを表示します。

format は実行時に `addFormat()` で登録します。形状（alternate setting）と rate（Clock Source）は Host が選びます。sketch は PCM を読み、`currentFormat()` で今の format を尋ねるだけです。再 build も `build_opt.h` も要りません。

## ハードウェア

- native USB OTG port を持つ ESP32-S2 / S3 / P4 board
- 音を再生できる USB host（PC、または EspUsbHost を動かす別 ESP32）
- format の表示を見る Serial monitor 接続
- codec、I2S、DAC は要りません。sketch は PCM を数えた後に捨てます。

## 動作内容

- playback stream を 1 つ持つ UAC2 function を作ります。
- 4 つの format を登録します。すべて stereo です。

  | 形状 | rate | frame | alternate |
  | --- | --- | --- | --- |
  | 16 bit を 2 byte に | 24000、16000 Hz | 4 byte | 1 |
  | 24 bit を 3 byte に | 24000、16000 Hz | 6 byte | 2 |

  1 つの Function は clock を 1 つ共有するので、両方の形状に同じ rate の組を持たせます。開始時の rate は最初に登録した 24000 Hz です。
- event queue なし（`eventCapacity = 0`）、playback ring は 32 frame（32 × 6 byte = 192 byte）にします。
- `loop()` で最大 192 byte ずつ PCM を読みます。Host が音を流している間、1 秒ごとに今の format とその 1 秒に読んだ byte 数を表示します。

## Host 側での確かめ方

Linux（ALSA）では `aplay -l` で card を探し、format ごとに試験音を流して Serial monitor を見ます。

```sh
speaker-test -D hw:1,0 -c 2 -r 24000 -F S16_LE  -t sine   # alternate 1
speaker-test -D hw:1,0 -c 2 -r 16000 -F S24_3LE -t sine   # alternate 2
```

期待する出力（1 秒に 1 行）：

```text
24000 Hz, 2 channels, 16 bits in 2 bytes: 96000 bytes read
16000 Hz, 2 channels, 24 bits in 3 bytes: 96000 bytes read
```

byte 数は rate × frame の大きさです。秒ごとに少しずれます。Host が流していない間（alternate 0）は何も表示しません。

Windows と macOS では、出力 device の sound 設定で format を選びます。このライブラリの動作は ESP32 の Host で確かめています。上の command を含め、PC の OS ではまだ確かめていません。

## 主要 API

- `EspUsbAudioFunction audio(device, EspUsbAudioProtocol::Uac2)` は UAC2 function を登録します。既定の protocol は UAC1 です。
- `audio.addPlaybackStream()` は Host から device への向きを選び、その stream を返します。
- `playback.addFormat({rate, channels, bytesPerSample, bitsPerSample})` は format を 1 つ登録します。失敗すると `false` を返し、`device.lastError()` を設定します（値の誤りや重複は `ESP_ERR_INVALID_ARG`、1 packet が 1023 byte を超えるときは `ESP_ERR_INVALID_SIZE`）。
- `audio.configure(EspUsbAudioConfig)` で `eventCapacity`、`playbackBufferFrames` などを決めます。停止中だけ受け付けます。戻り値を確かめます。
- 設定が食い違うと `device.begin()` は `ESP_ERR_INVALID_ARG` で失敗します。format がない、形状ごとに rate の組が違う、1 つの向きで channel 数が違う、などです。
- `playback.read(data, length)` は frame 単位でだけ返します。待ちません。
- `playback.currentFormat(format)` は使用中の形状と rate を写します。stream が止まっている間や切り替え中は `false` です。

## 注意

- UAC2 の playback は feedback endpoint 付きの非同期方式です（USB 2.0 §5.12.4.1）。feedback は FS で 10.14 形式の 3 byte、HS で 16.16 形式の 4 byte です（USB 2.0 §5.12.4.2）。値は公称 rate からの定数で、外部 codec の clock には追従しません。
- この例の packet の大きさ：24 kHz のとき、FS は 1 ms ごとに 100 / 150 byte、HS は 125 µs ごとに 16 / 24 byte です。
- ring は 24 kHz・24 bit で約 1.3 ms 分です。1 ms ごとに数えるだけの loop には足ります。codec へ流すときはもっと大きな ring が要ります。`playback.stats().overrunCount` を見てください。ring が満杯のときは古い PCM から捨てます（既定の `DropOldest`）。
- Host が形状を切り替えると ring は消され、新しい stream が始まるまで `read()` は 0 を返します。
- Host からの volume と mute は PCM に掛けません。`eventCapacity = 0` なので event としても残りません。event の扱いは [AudioSpeaker](../AudioSpeaker/) を見てください。

## 関連

- [AudioSpeaker](../AudioSpeaker/) - event と counter を表示する UAC1 playback
- [AudioHeadset](../AudioHeadset/) - 1 つの Function で playback と capture
- [AudioSpeakerM5](../AudioSpeakerM5/) - M5Stack の speaker で再生
- [Audio API](../../docs/classes/audio.ja.md)

## 読み方と確認の手順

### 実行前の準備

上のハードウェアの条件を確認し、board と例の profile を選びます。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USB の役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`setup()` は format の登録、設定、開始を行い、最初に失敗した段階のエラー番号を表示します。`loop()` は format を決め付けません。表示のたびに `currentFormat()` で尋ねます。

### API の使い方と改造時の注意

codec をつなぐときは、`currentFormat()` が変わるたびに codec を設定し直し、登録した中で最大の frame に合わせて ring の大きさを決めます。rate は両方の形状に足すか、どちらにも足さないかのどちらかです。

### 一つずつ確認する

まず 4 つの format をそれぞれ確かめます。次に format を 1 つ足すか減らすか、ring の大きさを変えて、もう一度確かめます。

## v2 からの変更点

この例は v3 で新しく作りました。v2 には対応する例がありません。

- [スケッチ](AudioPlayback.ino)
- [全サンプル](../README.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
