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
