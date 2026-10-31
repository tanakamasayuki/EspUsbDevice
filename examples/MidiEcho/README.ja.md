# EspUsbDevice MidiEcho

> English: [README.md](README.md)

受け取った event packet を、そのまま Host へ送り返す USB MIDI 1.0 device です。

packet の API（`readPacket()` / `writePacket()`）と、送信 ring が満杯のときの扱いを示します。送れなかった 1 packet を保持して送り直し、先を読んで失うことをしません。

## ハードウェア

- native USB OTG port を持つ ESP32-S2 / S3 / P4 board
- MIDI ソフトを使える USB host（`amidi`、DAW、MIDI monitor のある PC、または EspUsbHost を動かす別 ESP32）
- Serial monitor への出力はありません

## 動作内容

- 各方向 1 cable、受信・送信 ring を 16 packet、`transferPackets` を 4（IN 送信 1 回は最大 4 packet、16 byte）にして `EspUsbDevice` を開始します。
- Host が構成している間、4 byte の USB-MIDI event packet を 1 つ読み、送り返します。
- `writePacket()` が `false` の間は `pending` に保持します。Host がいなくなったら捨てます。

## Host 側での確かめ方

Linux（ALSA）の場合：

```sh
amidi -l                                   # port を探す。例: hw:1,0,0
amidi -p hw:1,0,0 -S '90 3C 64 80 3C 00' -d -t 1
```

`amidi -d` が返ってきた `90 3C 64` と `80 3C 00` を表示します。ALSA が MIDI の byte を cable 0 の USB-MIDI event packet にし、device は同じ packet を返します。

DAW や MIDI monitor では、device の出力 port を入力 port へつなぎ、返ってくる内容を見ます。列挙の後に port を手で有効にするアプリもあります。Windows と macOS はこの例で未確認です。

## 主要 API

- `EspUsbDeviceMidi midi(device)` は各方向 1 cable の USB MIDI function を登録します。
- `EspUsbMidiConfig`：`inCables` / `outCables`（1〜16。0 は現在の数を保つ）、`rxPackets` / `txPackets`（ring の大きさ、packet 単位）、`transferPackets`（0 は bus packet 1 つ分。FS は 64 byte、HS は 512 byte）。`midi.configure()` は停止中だけ受け付けます。
- `midi.readPacket(packet)` は 1 packet を読めたら `true`、なければ `false` です。待ちません。
- `midi.writePacket(packet)` は送信 ring へ写せたら `true` です。ring が満杯のとき、Host が構成していないとき、この function が送れない cable や CIN のときは `false` です。
- `EspUsbDeviceMidiPacket` は `header`（上位 4 bit が cable、下位 4 bit が Code Index Number）と `byte1`〜`byte3` です。

## 注意

- ライブラリは、知らない cable や予約済みの CIN（0 と 1）の受信 packet を捨て、`receiveErrors()` で数えます。そのため、この例が読む packet はすべて送り返せるものです。
- MIDI に byte の流れの API はありません。`EspUsbDeviceMidi` は `Print` でも `Stream` でもありません。
- `configure()` や `begin()` が失敗すると、別の debug 接続の `Serial`（115200）にエラー名を表示します。

## 関連

- [MIDI](../MIDI/) - note を送り、受信 packet を表示する
- [MIDIInterface](../MIDIInterface/) - UART MIDI と USB MIDI の間で同じ「保持して送り直す」形
- [CompositeCdcMidi](../CompositeCdcMidi/) - この echo と CDC port を 1 台に
- [VendorEcho](../VendorEcho/) - vendor bulk の byte で同じ形
- [MIDI API](../../docs/classes/midi.ja.md)

## 読み方と確認の手順

### 実行前の準備

上のハードウェアの条件を確認し、board と例の profile を選びます。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USB の役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`held` は「送るのを待っている packet が 1 つある」という意味です。何も保持していないときだけ次を読みます。送信 ring が満杯になると次に受信 ring が埋まり、Host が IN を読むまで device は OUT data を受け取りません。device の中で packet は失われません。

### API の使い方と改造時の注意

MIDI channel と USB cable は別のものです。送る前に packet を変えるときは、cable を `inCableCount()` 未満に保ちます。cable を増やすときは `inCables` と `outCables` を明示します。

### 一つずつ確認する

最初は例を変えずに、短い message の echo を確かめます。次に長い連続送信をして、返ってきた event の数が合うかを確かめます。

## v2 からの変更点

この例は v3 で新しく作りました。v2 には対応する例がありません。

- [スケッチ](MidiEcho.ino)
- [全サンプル](../README.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
