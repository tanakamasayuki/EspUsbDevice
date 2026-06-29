# EspUsbDevice MIDIController

> English: [README.md](README.md)

ADC と button input を USB MIDI message に変換する MIDI controller の例です。

## ハードウェア

- USB device 対応の ESP32-S3 など Arduino-ESP32 board
- `CONTROLLER_PIN` に接続した potentiometer などの analog input
- `BUTTON_PIN` に接続した button。default は BOOT button 想定の GPIO0
- PC、DAW、MIDI monitor などの USB MIDI host
- ログ確認用の Serial monitor 接続

## 動作内容

- analog input を smoothing し、MIDI CC 74 に変換して送信します。
- button press で C4 Note On、release で Note Off を送信します。
- USB MIDI device として Host に列挙されます。

## 主要 API

- `MIDI.controlChange(channel, control, value)` で CC を送信します。
- `MIDI.noteOn(channel, note, velocity)` で Note On を送信します。
- `MIDI.noteOff(channel, note, velocity)` で Note Off を送信します。

`channel` は `0` から `15` です。MIDI の表示上は channel 1 から 16 に対応します。

## 調整ポイント

- `CONTROLLER_PIN`: analog input pin
- `BUTTON_PIN`: button input pin
- `MIDI_CC_CUTOFF`: 送信する control number
- `MIDI_NOTE_C4`: button に割り当てる note number

## 関連

- [MIDI](../MIDI/) - USB MIDI の基本送受信
