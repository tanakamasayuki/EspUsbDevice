# EspUsbDevice MIDIInterface

> English: [README.md](README.md)

UART MIDI 1.0 と USB MIDI 1.0 を中継する例です。

DIN MIDI などの 31250 baud の serial MIDI を `Serial1` で受け、USB-MIDI event packet として Host へ送ります。
Host から届いた USB-MIDI event packet は、serial MIDI の byte 列として `Serial1` へ書き戻します。

## ハードウェア

- native USB OTG port を持つ ESP32-S2 / S3 / P4 board
- 31250 baud の MIDI 入出力回路（入力は photocoupler で絶縁し、出力は 3.3 V MCU に合った driver を使う）
- `MIDI_RX_PIN` / `MIDI_TX_PIN`（GPIO39 / GPIO40）につないだ UART MIDI
- PC、DAW、MIDI monitor などの USB MIDI host
- ログ確認用の Serial monitor 接続

## 動作内容

- `Serial1` を 31250 baud / 8N1 で開始します。
- MIDI function を設定します（名前 `EspUsbDevice MIDI Interface`、受信・送信 ring 各 64 event）。
- serial MIDI の channel voice message を USB-MIDI event packet に変換します。
- Host からの USB-MIDI event packet を serial MIDI の byte 列に戻します。
- UART 側からは Note On / Off、Poly Pressure、Control Change、Program Change、Channel Pressure、Pitch Bend を扱います。Host から来た packet は CIN に応じて（SysEx や 1 byte message も）UART へ書きます。
- USB の送信 ring が受け付けなかった UART のメッセージは捨てずに保持し、後の loop で送ります。

## 主要 API

- `midi.writePacket(packet)` は USB-MIDI event packet を Host へ送ります。`false` は送信 ring が満杯（または cable/CIN が不正）で、何も入らなかったという意味です。
- `midi.readPacket(packet)` は Host からの USB-MIDI event packet を読みます。
- `midi.configuration()` / `configure()` で ring の大きさと名前を設定します。
- `ESP_USB_DEVICE_MIDI_CIN_*` は Code Index Number の定数です。

## 制限

- UART から USB への方向は channel voice message だけを扱います。
- UART 側の SysEx、running status、real-time message まで扱う完全な MIDI parser ではありません。
- USB 向けのメッセージを保持している間は UART を読みません。その間の byte は UART driver の buffer が受けます。
- 3.3 V MCU に合った MIDI の電気回路を使ってください。

## 関連

- [MIDI](../MIDI/) - 基本的な USB MIDI 送受信
- [MIDIController](../MIDIController/) - ADC とボタンから MIDI メッセージを作る
- [MidiEcho](../MidiEcho/) - 同じ「保持して再送」の形の echo
- [MIDI API](../../docs/classes/midi.ja.md)

## 読み方と確認の手順

### 実行前の準備

USB MIDIとMIDI用UARTの間で基本メッセージを中継する。

上のハードウェアの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

Serial1がMIDI側、Serialは診断用です。MIDI_RX_PIN/MIDI_TX_PINをboardに合わせ、DINコネクタをGPIOへ直結せず適切なMIDIの電気回路を使います。parserは待ち時間を区切って一部のchannel messageを扱い、SysEx・running status・real-timeの割込みまでは扱いません。`pendingToUsb`は変換済みのメッセージを`writePacket()`が受け付けるまで1つ保持し、Hostが構成していないときだけ捨てます。

### APIの使い方と改造時の注意

MIDI channelとUSB cable/portは別の指定です。APIのchannel番号とpacketのfieldをホストアプリに合わせます。MIDI event packetは生のUARTメッセージと同じbyte配置ではありません。MIDIは制御メッセージを運び、音声sampleは運びません。

押したnoteには対応する解放を必ず送ります。送信結果を確認し、ホスト不在時に再試行するか捨てるかを決めます。resetや切断で手元のnote状態は無効になり得ます。多重化や完全なMIDI parserを加える前に、1 channel・1 cableから始めます。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・容量を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

## v2 からの変更点

- USB の送信 ring が拒否したメッセージを保持して再送します。v2 は `writePacket()` が `false` を返すと黙って捨てていました。
- ring の大きさと function 名は `CFG_TUD_MIDI_*` の build flag ではなく `configure()` で決めます。
- object は v2 の `MIDI` ではなく `midi` という instance です。

- [スケッチ](MIDIInterface.ino)
- [全サンプル](../README.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
