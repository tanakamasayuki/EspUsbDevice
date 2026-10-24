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

## 読み方と確認の手順

### 実行前の準備

USB MIDIとMIDI用UARTの間で基本メッセージを中継する。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

Serial1がMIDI側、Serialがログ側です。MIDI_RX_PIN・MIDI_TX_PINを基板へ合わせ、DIN端子をGPIOへ直結せず適切なMIDI電気回路を使います。待ち時間を制限した一部のchannelメッセージ用parserであり、全SysEx・running status・real-timeの混在を扱うものではありません。

### APIの使い方と改造時の注意

MIDI channelとUSB cable・portは別の指定です。APIのchannel番号の基準とpacketのfieldをホストアプリへ合わせます。MIDI event packetとUARTの生メッセージはbyte配置が異なり、MIDIは音声sampleではなく制御メッセージを運びます。

保持した音符には終了を送ります。送信結果を確認し、ホスト不在時の再試行・破棄を決めます。reset・切断で手元の発音状態が無効になる場合があります。複数化や完全なparserへ進む前に、一channel・一portで確認します。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](MIDIInterface.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
