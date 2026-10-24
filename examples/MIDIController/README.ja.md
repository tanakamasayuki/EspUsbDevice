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

## 読み方と確認の手順

### 実行前の準備

アナログ操作とスイッチをMIDIメッセージへ変換する。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

可変抵抗の摺動端子をCONTROLLER_PINへ、通常開のスイッチをBUTTON_PINとGND間へつなぎ、ADC電圧と共通GNDを確認します。平滑化と0～127への換算をcontrolChange()より先に追います。接点のチャタリング除去や失敗した送信の保持はないため、実機器では追加します。

### APIの使い方と改造時の注意

MIDI channelとUSB cable・portは別の指定です。APIのchannel番号の基準とpacketのfieldをホストアプリへ合わせます。MIDI event packetとUARTの生メッセージはbyte配置が異なり、MIDIは音声sampleではなく制御メッセージを運びます。

保持した音符には終了を送ります。送信結果を確認し、ホスト不在時の再試行・破棄を決めます。reset・切断で手元の発音状態が無効になる場合があります。複数化や完全なparserへ進む前に、一channel・一portで確認します。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](MIDIController.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
