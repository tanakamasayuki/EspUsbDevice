# EspUsbDevice CompositeCdcMidi

> English: [README.md](README.md)

1 台の device に 2 つの Function を載せる例です。CDC ACM の serial port と USB MIDI 1.0 の port で、どちらも Host から届いたものを返します。

1 台の中の Function は互いに独立していることを示します。それぞれが自分の `configure()`、自分の ring、自分の backpressure を持ちます。MIDI の送信 ring が満杯でも serial の echo は止まりません。逆も同じです。

## ハードウェア

- native USB OTG port を持つ ESP32-S2 / S3 / P4 board
- serial terminal と MIDI ソフトを使える USB host（PC、または EspUsbHost を動かす別 ESP32）
- Serial monitor への出力はありません

## 動作内容

- CDC port（interface 名 `Console`）の受信・送信 ring を 128 byte、`transferBytes` を 64 にします。
- MIDI は各方向 1 cable、ring を 16 packet、`transferPackets` を 4 にします。
- product 文字列 `CDC and MIDI echo` で `EspUsbDevice` を開始します。
- `loop()` では、送る空きがある間 CDC の byte を返し、MIDI の packet は [MidiEcho](../MidiEcho/) と同じ「保持して送り直す」形で返します。

## Host 側での確かめ方

Host からは interface が 4 つある複合 device に見えます。CDC の通信用と data 用、MIDI の Audio Control と MIDI Streaming です。CDC の 2 つは Interface Association Descriptor でまとめます。MIDI の 2 つは class 固有の Audio Control header が MIDI Streaming interface を列挙して結び付けます（USB MIDI 1.0 はこの点で Audio 1.0 に従います）。

Linux の場合：

```sh
lsusb -v -d 303a:4000 | grep -E 'iProduct|iInterface|bInterfaceClass'
picocom /dev/ttyACM0                       # 文字を打つと返る
amidi -l                                   # MIDI port を探す
amidi -p hw:1,0,0 -S '90 3C 64' -d -t 1    # 90 3C 64 が表示される
```

serial と MIDI の確認を同時に行い、互いに止め合わないことを見ます。Windows と macOS はこの例で未確認です。

## 主要 API

- `EspUsbDeviceCdcSerial console(device, "Console")` と `EspUsbDeviceMidi midi(device)` は、同じ `EspUsbDevice` に 2 つの Function を登録します。interface 番号と endpoint 番号は `begin()` で登録順に割り当てます。
- `console.configure(EspUsbBulkStreamConfig)` と `midi.configure(EspUsbMidiConfig)` は別々です。どちらも停止中だけ受け付けます。
- `EspUsbDeviceConfig::product` で product 文字列を決めます。ほかの identity は既定のままです。
- `console.writeAvailable()` は CDC 送信 ring の空きです。
- `midi.readPacket()` / `writePacket()` は待ちません。

## 注意

- `device.begin()` は構成全体を確かめます。endpoint 番号と、すべての IN endpoint の送信 FIFO です。CDC は通知用 IN と bulk IN、MIDI は bulk IN を 1 つ使います。
- Host が構成していない間（`device.ready()` が false）、MIDI の echo は止まり、保持中の packet を捨てます。CDC の echo は確認が要りません。停止中は `available()` が 0 で、何も読みません。
- `configure()` や `begin()` が失敗すると、別の debug 接続の `Serial`（115200）にエラー名を表示します。

## 関連

- [MidiEcho](../MidiEcho/) - MIDI の echo だけ
- [CdcConsole](../CdcConsole/) - `Stream &` を通した CDC の echo
- [SerialMulti](../SerialMulti/) - 複数の CDC port
- [CompositeHidCdcMsc](../CompositeHidCdcMsc/) - HID、CDC、MSC を 1 台に
- [CDC API](../../docs/classes/cdc.ja.md)、[MIDI API](../../docs/classes/midi.ja.md)

## 読み方と確認の手順

### 実行前の準備

上のハードウェアの条件を確認し、board と例の profile を選びます。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USB の役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`setup()` は device に触る前に両方の設定を用意し、開始は 1 回だけです。`loop()` は 2 つの Function を順に処理し、どちらでも待ちません。

### API の使い方と改造時の注意

Function を足すときは、それぞれに設定を与え、結果を確かめます。後で 1 つの Function の設定を変えるには、device 全体の `device.end()` と `device.begin()` が要ります（[ReconfigureVendor](../ReconfigureVendor/)）。

### 一つずつ確認する

まずそれぞれの echo を単独で確かめ、次に両方を同時に確かめます。3 つ目の Function は、両方が通ってから足します。

## v2 からの変更点

この例は v3 で新しく作りました。v2 には対応する例がありません。

- [スケッチ](CompositeCdcMidi.ino)
- [全サンプル](../README.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
