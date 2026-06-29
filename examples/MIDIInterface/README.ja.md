# EspUsbDevice MIDIInterface

> English: [README.md](README.md)

UART MIDI 1.0 と USB MIDI 1.0 を相互変換する bridge example です。

DIN MIDI などの 31250 baud serial MIDI を `Serial1` で受け取り、USB-MIDI event packet として
Host へ送信します。Host から受け取った USB-MIDI event packet は serial MIDI byte stream として
`Serial1` へ出力します。

## ハードウェア

- USB device 対応の ESP32-S3 など Arduino-ESP32 board
- 31250 baud MIDI 入出力回路
- `MIDI_RX_PIN` / `MIDI_TX_PIN` に接続した UART MIDI
- PC、DAW、MIDI monitor などの USB MIDI host
- ログ確認用の Serial monitor 接続

## 動作内容

- `Serial1` を 31250 baud / 8N1 で起動します。
- serial MIDI channel voice message を USB-MIDI event packet へ変換します。
- Host からの USB-MIDI event packet を serial MIDI byte stream へ戻します。
- Note On / Off、Control Change、Program Change、Pressure、Pitch Bend を扱います。

## 主要 API

- `MIDI.writePacket(packet)` は raw USB-MIDI event packet を Host へ送信します。
- `MIDI.readPacket(packet)` は Host からの raw USB-MIDI event packet を読みます。

## 制限

- この example は channel voice message 中心です。
- SysEx、running status、real-time message の完全な MIDI parser ではありません。
- DIN MIDI 回路は 3.3 V MCU に合った回路にしてください。

## 関連

- [MIDI](../MIDI/) - USB MIDI の基本送受信
- [MIDIController](../MIDIController/) - ADC / button から MIDI message を生成する例
