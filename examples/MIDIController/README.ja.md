# EspUsbDevice MIDIController

> English: [README.md](README.md)

ADC とボタンの入力を USB MIDI のメッセージに変える例です。

## ハードウェア

- native USB OTG port を持つ ESP32-S2 / S3 / P4 board
- `CONTROLLER_PIN`（GPIO5、ESP32-P4 では GPIO16）につないだ可変抵抗などのアナログ入力。ADC が使える pin にしてください
- `BUTTON_PIN`（GPIO0、多くの S2/S3 board の BOOT ボタン。ESP32-P4 では GPIO35）につないだボタン。どちらの pin も board の回路図で確認してください
- PC、DAW、MIDI monitor などの USB MIDI host
- ログ確認用の Serial monitor 接続

## 動作内容

- アナログ入力を平滑化し、値が変わったら MIDI CC 74 として送ります。
- ボタンを押すと C4 の Note On、離すと Note Off を送ります。
- Host には `EspUsbDevice MIDI Controller` という名前の USB MIDI device として認識されます。
- 送信 ring が受け付けなかった送信はやり直すので、解放が失われません。Host が切断した後は、次の構成のときに現在のつまみ位置を送り直します。
- Host から届く packet は受信 ring を確保せず、callback で受け取って捨てます。

## 主要 API

- `midi.controlChange(channel, control, value)` は Control Change を送ります。
- `midi.noteOn(channel, note, velocity)` は Note On を送ります。
- `midi.noteOff(channel, note, velocity)` は Note Off を送ります。
- 3 つとも event を送信 ring に入れられなければ `false` を返します。この例は `true` のときだけ「送った」状態を更新します。
- `EspUsbMidiConfig::rxMode = EspUsbMidiRxMode::Callback` と `rxPackets = 0`、`onPacket()` で、受信 event を ring ではなく callback へ渡します。
- `device.ready()` は Host が device を構成したかを返します。

`channel` は `0` から `15` で、表示上の MIDI channel 1 から 16 に対応します。
control と note の値は `0` から `127` です。範囲外の値は `ESP_ERR_INVALID_ARG` で失敗し、送りません。ADC からの変換はこの範囲に収めます。

## 調整項目

- `CONTROLLER_PIN`: アナログ入力 pin
- `BUTTON_PIN`: ボタン入力 pin
- `MIDI_CC_CUTOFF`: 送信する control number
- `MIDI_NOTE_C4`: ボタンに割り当てる note number
- `midiConfig.txPackets`: 送信 ring に待たせておける event 数

## 関連

- [MIDI](../MIDI/) - 基本的な USB MIDI 送受信
- [MIDIInterface](../MIDIInterface/) - UART MIDI と USB MIDI の中継
- [MIDI API](../../docs/classes/midi.ja.md)

## 読み方と確認の手順

### 実行前の準備

アナログ操作とスイッチをMIDIメッセージへ変換する。

上のハードウェアの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

可変抵抗のwiperをCONTROLLER_PINへ、常開スイッチをBUTTON_PINとGNDの間へつなぎ、ADCの電圧範囲とGNDの共通化を確認します。平滑化と0〜127への変換から`controlChange()`までを追います。`sentCcValue`と`sentPressed`はHostへ送れた状態を持つので、拒否された送信は次のloopでやり直します。ボタンのチャタリング除去はしていないので、実際のcontrollerでは加えてください。

### APIの使い方と改造時の注意

MIDI channelとUSB cable/portは別の指定です。APIのchannel番号とpacketのfieldをホストアプリに合わせます。MIDI event packetは生のUARTメッセージと同じbyte配置ではありません。MIDIは制御メッセージを運び、音声sampleは運びません。

押したnoteには対応する解放を必ず送ります。送信結果を確認し、ホスト不在時に再試行するか捨てるかを決めます。resetや切断で手元のnote状態は無効になり得ます。多重化や完全なMIDI parserを加える前に、1 channel・1 cableから始めます。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・control番号・noteを一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

## v2 からの変更点

- 拒否された `controlChange()` / `noteOn()` / `noteOff()` をやり直します。v2 は送信の前に新しい値を記録していたので、送信に失敗すると（ring が満杯のときの解放など）失われていました。
- 送信は `device.ready()` の間だけ行い、切断後はつまみ位置を送り直し、note の状態を戻します。
- 受信側は ring を持たない callback 方式にしました。v2 は読まない FIFO を確保していました。
- ESP32-P4 では専用の既定 pin（ADC は GPIO16、ボタンは GPIO35）を使います。v2 は全 target で GPIO5/GPIO0 でした。
- function 名を `configure()` で設定します。

- [スケッチ](MIDIController.ino)
- [全サンプル](../README.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
