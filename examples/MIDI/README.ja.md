# EspUsbDevice MIDI

> English: [README.md](README.md)

USB MIDI device を作り、Host へ note / control change を送信し、Host からの MIDI packet を
受信する例です。

Arduino-ESP32 標準の USB MIDI class は使いません。`EspUsbDeviceMidi` を `EspUsbDevice`
に登録し、USB MIDI class descriptor と bulk endpoint をこのライブラリ側で構成します。

## ハードウェア

- USB device 対応の ESP32-S3 など Arduino-ESP32 board
- PC、DAW、MIDI monitor、EspUsbHost を動かす別 ESP32 などの USB host
- ログ確認用の Serial monitor 接続

## 動作内容

- `EspUsbDevice` を full-speed USB MIDI device として起動します。
- 1 秒ごとに C major arpeggio の `noteOn()` / `noteOff()` を送信します。
- 同時に `controlChange()` を送信します。
- Host から受信した 4 byte USB-MIDI event packet を Serial monitor に出力します。

## 主要 API

- `EspUsbDeviceMidi MIDI(device)` は USB MIDI function を登録します。
- `MIDI.noteOn(channel, note, velocity)` は Note On を送信します。
- `MIDI.noteOff(channel, note, velocity)` は Note Off を送信します。
- `MIDI.controlChange(channel, control, value)` は Control Change を送信します。
- `MIDI.programChange()`、`polyPressure()`、`channelPressure()`、`pitchBend()` も使えます。
- `MIDI.writePacket(packet)` は raw USB-MIDI event packet を送信します。
- `MIDI.readPacket(packet)` は Host からの raw USB-MIDI event packet を読みます。

## 注意

- USB MIDI は 4 byte の USB-MIDI event packet として送受信します。
- `channel` は `0` から `15` です。MIDI の表示上は channel 1 から 16 に対応します。
- Serial monitor はログ確認用です。MIDI の入出力は USB device port 側で確認してください。
- DAW や OS によっては device を認識したあと、MIDI input / output port を手動で有効化する必要があります。

## 関連

- [Serial](../Serial/) - CDC ACM serial device
- [Keyboard](../Keyboard/) - HID keyboard device

## 読み方と確認の手順

### 実行前の準備

USB MIDIの音符を生成し、受信event packetを調べる。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`sendStep()`で前の音符を終了して次を始め、`loop()`でreadPacket()も処理します。MIDI monitorや楽器でこのportを選びます。APIのchannelは0始まりで、Note Onに対応するNote Offを送らないと発音が続く場合があります。

### APIの使い方と改造時の注意

MIDI channelとUSB cable・portは別の指定です。APIのchannel番号の基準とpacketのfieldをホストアプリへ合わせます。MIDI event packetとUARTの生メッセージはbyte配置が異なり、MIDIは音声sampleではなく制御メッセージを運びます。

保持した音符には終了を送ります。送信結果を確認し、ホスト不在時の再試行・破棄を決めます。reset・切断で手元の発音状態が無効になる場合があります。複数化や完全なparserへ進む前に、一channel・一portで確認します。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](MIDI.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
