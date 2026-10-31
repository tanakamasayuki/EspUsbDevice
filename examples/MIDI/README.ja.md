# EspUsbDevice MIDI

> English: [README.md](README.md)

USB MIDI 1.0 device を作り、Host へ note / control change を送り、Host から届いた MIDI packet をログに出す例です。

Arduino-ESP32 標準の USB MIDI class も TinyUSB の MIDI FIFO も使いません。`EspUsbDeviceMidi` を
`EspUsbDevice` に登録し、USB MIDI の descriptor と bulk endpoint をこのライブラリ側で構成します。
ring の大きさと function 名は `configure()` で決めます。

## ハードウェア

- native USB OTG port を持つ ESP32-S2 / S3 / P4 board
- PC、DAW、MIDI monitor、EspUsbHost を動かす別 ESP32 などの USB host
- ログ確認用の Serial monitor 接続

## 動作内容

- MIDI function を設定（名前 `EspUsbDevice MIDI`、受信・送信 ring 各 32 event）してから `EspUsbDevice` を開始します。
- Host が device を構成している間、1 秒ごとに C major の arpeggio を `noteOn()` / `noteOff()` で送り、各 step で `controlChange()`（CC 74）も送ります。各 event が送信 ring に入ったかをログに出します。
- Host から受けた 4 byte の USB-MIDI event packet を、cable、CIN、MIDI の 3 byte に分けて表示します。
- ライブラリが捨てた packet（未知の cable、予約された CIN、端数）を `receiveErrors()` で報告します。

## 使い方

1. sketch を書き込み、通常の Serial monitor を開きます。
2. USB device port を Host に接続します。
3. MIDI monitor や DAW を開き、`EspUsbDevice MIDI` の入力を有効にすると note が見えます。
4. Host から device の出力 port へ note を送ると、Serial monitor に `MIDI_RX` が出ます。

## 主要 API

- `EspUsbDeviceMidi midi(device)` は送受信とも cable 1 本の USB MIDI function を登録します。`EspUsbDeviceMidi(device, in, out)` で方向ごとに指定できます（各 1〜16）。
- `midi.configuration()` / `configure(EspUsbMidiConfig)` で `name`、`rxPackets`、`txPackets`、`rxMode` などを読み書きします。停止中だけ受け付けます。constructor の cable 数を残すため、`configuration()` の値から始めます。
- `midi.noteOn(channel, note, velocity, cable = 0)` は Note On を送ります。
- `midi.noteOff(channel, note, velocity, cable = 0)` は Note Off を送ります。
- `midi.controlChange(channel, control, value, cable = 0)` は Control Change を送ります。
- `midi.programChange()`、`polyPressure()`、`channelPressure()`、`pitchBend()` も使えます。
- `midi.writePacket(packet)` は USB-MIDI event packet をそのまま送ります。
- `midi.readPacket(packet)` は Host からの USB-MIDI event packet を読みます。
- `midi.receiveCapacity()` / `sendCapacity()` は確保した ring の event 数、`receiveErrors()` は捨てた packet の数、`startError()` は MIDI に固有の `begin()` 失敗の理由です。

## 注意

- USB MIDI は 4 byte の USB-MIDI event packet で転送されます。header の上位 4 bit が cable、下位 4 bit が Code Index Number（CIN）です。
- `channel` は `0` から `15` で、表示上の MIDI channel 1 から 16 に対応します。便利関数は引数を検査します。channel が 15 超、data byte が 127 超、pitch bend が 16383 超なら、何も送らず `false` を返し、`lastError()` は `ESP_ERR_INVALID_ARG` になります。丸めると別の message になるからです（MIDI 1.0 の data byte は 7 bit）。
- 送信の `true` は「送信 ring にコピーした」という意味で、Host が鳴らしたことは意味しません。ring がいっぱい、または Host に知らせていない cable なら `false` です。
- `EspUsbDeviceMidi` は `Print` でも `Stream` でもありません。event 単位で扱います。
- Serial monitor 側はログ確認用です。MIDI の入出力は USB device port 側で確認します。
- DAW や OS のツールによっては、認識後に MIDI input / output port を手動で有効にする必要があります。

## 関連

- [MIDIController](../MIDIController/) - ADC とボタンから MIDI を作る
- [MIDIInterface](../MIDIInterface/) - UART MIDI と USB MIDI の中継
- [MidiEcho](../MidiEcho/) - 送れなかった packet を保持する echo
- [MIDI API](../../docs/classes/midi.ja.md)

## 読み方と確認の手順

### 実行前の準備

USB MIDIの音符を生成し、受信event packetを調べる。

上のハードウェアの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`sendStep()`は前のnoteを止めて次のnoteを始め、`loop()`は`readPacket()`も読み切ります。stepは`device.ready()`の間だけ送り、Hostがいなくなったら鳴っているnoteの状態を消します。APIのchannelは0始まりで、Note Onに対応するNote Offを送らないと楽器が鳴り続けることがあります。

### APIの使い方と改造時の注意

MIDI channelとUSB cable/portは別の指定です。APIのchannel番号とpacketのfieldをホストアプリに合わせます。MIDI event packetは生のUARTメッセージと同じbyte配置ではありません。MIDIは制御メッセージを運び、音声sampleは運びません。

押したnoteには対応する解放を必ず送ります。送信結果を確認し、ホスト不在時に再試行するか捨てるかを決めます。resetや切断で手元のnote状態は無効になり得ます。多重化や完全なMIDI parserを加える前に、1 channel・1 cableから始めます。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。note・容量・cable数を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

## v2 からの変更点

- ring の大きさと function 名は `CFG_TUD_MIDI_*` の build flag ではなく `configure()` で決めます。object 名は `MIDI` ではなくなりました（単一の global instance はありません）。
- step は Host が device を構成している間だけ送り、送信結果をログに出します。
- 受信ログに cable 番号を加え、ライブラリが拒否した packet を `receiveErrors()` で報告します（v3 は未知の cable や予約 CIN を渡さずに捨てます）。
- `begin()` 失敗時に `midi.startError()` も表示します。
- 便利関数の範囲外の引数は `ESP_ERR_INVALID_ARG` で失敗し、何も送らなくなりました。v2 は data byte を 7 bit に丸めて送っていました。

- [スケッチ](MIDI.ino)
- [全サンプル](../README.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
