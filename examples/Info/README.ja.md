# EspUsbDevice 診断ツール

> English: [README.md](README.md)

board の立ち上げと問題の絞り込みに使う、3 つの診断 sketch です。class の使い方を示すものではありません。ライブラリが何を作ったか、Host がそれを受け入れたか、1 回ずつの転送に Host がどう反応するかを見せます。

| sketch | 目的 | Host | 使う時 |
| --- | --- | --- | --- |
| [EspUsbDeviceBringUpCheck](EspUsbDeviceBringUpCheck/README.ja.md) | `begin()`、列挙、suspend、Host から device への経路を順に確かめます。列挙されないときは確認項目を表示します | 要る | 最初に。新しい board やケーブルのたびに |
| [EspUsbDeviceDescriptorDump](EspUsbDeviceDescriptorDump/README.ja.md) | ライブラリが作ったすべての descriptor と、その chip の controller に対する endpoint の予算を表示します | 要らない | 次に。Function の組を決めたとき、変えたとき |
| [EspUsbDeviceConsole](EspUsbDeviceConsole/README.ja.md) | serial terminal から HID report や vendor の転送を 1 つずつ送り、Host から届く request をすべて表示します | 要る | Host のアプリが思ったように反応しないとき |

## 使い分け

- **Host に device が出てこない。** EspUsbDeviceBringUpCheck を動かします。`BEGIN failed` なら原因は board 側です（descriptor、endpoint の予算、設定）。`BEGIN ok` なのに `MOUNTED` が出ないなら、ケーブル、コネクタ、電源、Host を疑います。
- **`begin()` が失敗する。または Host に思ったものと違うものが見える。** 同じ Function を有効にして EspUsbDeviceDescriptorDump を動かします。FS と HS で宣言している byte そのものと、endpoint がその controller に収まるかが分かります。ケーブルなしでも動きます（`EspUsbDeviceStartMode::DescriptorsOnly`）。
- **列挙はされるが、アプリが反応しない。** EspUsbDeviceConsole を動かします。usage や report を 1 つずつ試し、`HOST_` の行でアプリが送ってくるものを見ます。

3 つとも、書込みに使う port（UART または USB Serial/JTAG）の Serial monitor に表示します。ライブラリが使う OTG port とは別です。EspUsbDeviceConsole はコマンドもそこで受け付けます。

## 関連

- [全サンプル](../README.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
- [USB device 開発ガイド](../../docs/usb-device-guide.ja.md) - 立ち上げの手順と Host 側のツール
- [トラブルシューティング](../../docs/troubleshooting.ja.md)
