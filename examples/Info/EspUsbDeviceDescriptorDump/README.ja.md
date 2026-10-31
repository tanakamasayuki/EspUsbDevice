# EspUsbDeviceDescriptorDump

> English: [README.md](README.md)

登録した Function から **ライブラリが実際に作った descriptor をすべて** 表示する例です。

Host 側では、device が何者かを知るために descriptor を読みます。device 側では自分が descriptor を書く立場なので、
問うべきことは逆になります。いま何を宣言したのか、それはこの controller に収まるのか、です。

## ハードウェア

- USB device に対応した Arduino-ESP32 board（ESP32-S2 / S3 / P4）
- Serial monitor 接続

USB host は要りません。descriptor は `device.begin()` の中で作られるので、ケーブルを Host につながなくても全体を表示できます。
`DUMP_DESCRIPTORS_ONLY 1` にすると controller も起動しません（`EspUsbDeviceStartMode::DescriptorsOnly`）。

## 動作内容

sketch 冒頭の `DUMP_ENABLE_*` で Function の組合せを選び、次を表示します。

- DEVICE descriptor（hex と各 field の解釈）
- CONFIGURATION descriptor の FS 版と HS 版（hex と、block ごとの走査。interface と IAD の文字列番号も含む）
- DEVICE QUALIFIER と OTHER SPEED CONFIGURATION
- BOS、WebUSB URL、Microsoft OS 2.0 の descriptor と、MS OS 2.0 の vendor revision と形式
- HID report descriptor（複数の HID Function は 1 つにまとめられます）
- 存在するすべての string descriptor（Function 名を含む）
- この target の controller の上限と比べた **endpoint の予算**と、FIFO を 2 packet 分にした bulk IN endpoint

```c
#define DUMP_ENABLE_KEYBOARD 1
#define DUMP_ENABLE_MOUSE    0
#define DUMP_ENABLE_GAMEPAD  0
#define DUMP_ENABLE_CDC      1
#define DUMP_ENABLE_MIDI     0
#define DUMP_ENABLE_VENDOR   0
#define DUMP_ENABLE_WEBUSB   0
#define DUMP_DESCRIPTORS_ONLY 0
```

複合 device を作る前に、ここで組合せを変えて build し直し、予算の行を読んでください。組合せが収まるかを知る一番早い方法です。
Function の数に固定の上限はありません。endpoint と FIFO の予算で決まり、収まらない組合せは `begin()` が報告します。

## endpoint の予算

controller ごとの上限です。超える構成は、PHY を始める前に `begin()` が `ESP_ERR_INVALID_SIZE` で拒否します。

| Controller | 最大 endpoint 番号 | 非 control の IN | 非 control の OUT |
|---|---|---|---|
| ESP32-S2 / S3 | 6 | 4 | 6 |
| ESP32-P4 FS port | 6 | 4 | 6 |
| ESP32-P4 HS port | 15 | 7 | 15 |

ESP32-P4 では上限が `config.controller` に従います（`Auto` は HS port）。同じ組合せでも片方の port には収まり、もう片方には収まらないことがあります。
この sketch が表示する予算も、同じように `device.config().controller` に従います。

## 文字列

番号 1〜3 は manufacturer / product / serial number、4 は予約で、5 から Function 名（たとえば CDC port の `"Dump CDC"`）と class 固有の文字列が続きます。
存在する番号だけを表示し、ASCII 以外の UTF-16 は `\uXXXX` で表示します。

## Host 側との照合

ここで表示した CONFIGURATION の hex と、Host が受け取った byte 列は 1 byte 単位で一致するはずです。

```sh
# Linux
lsusb -v -d 303a:4051

# 任意の OS、PyUSB（v2 の試験 tool も標準 descriptor は読めます）
# device_inspect.py: https://github.com/tanakamasayuki/EspUsbDevice/tree/3fed50d0d777f600432b71381faccd93adb9a202/tests/manual/device_inspect
uv run --with pyusb python device_inspect.py --pid 0x4051
```

一致しない、または Host が何も表示しない場合、descriptor は bus に届いておらず、問題はこれより下の層（列挙、電気、controller の選択）にあります。
[EspUsbDeviceBringUpCheck](../EspUsbDeviceBringUpCheck/) に戻ってください。

## Serial 出力の例

```text
=== EspUsbDevice descriptor dump ===
BEGIN ok error=ESP_OK
--- DEVICE descriptor (18 bytes) ---
  0000  12 01 00 02 ef 02 01 40 3a 30 51 40 00 01 01 02
  0010  03 01
  bcdUSB=0x0200 class=0xef (Miscellaneous) subclass=0x02 protocol=0x01 ep0_mps=64
  idVendor=0x303a idProduct=0x4051 bcdDevice=0x0100 configurations=1
--- CONFIGURATION descriptor (full-speed) (... bytes) ---
  ...
  0009  INTERFACE  number=0 alt=0 endpoints=1 class=0x03 (HID) subclass=0x01 protocol=0x01 string=0
  0012  HID        bcdHID=0x0111 descriptors=1 report_descriptor=... bytes
  ...
--- string descriptors ---
  [0] langid=0x0409
  [1] "EspUsb"
  [2] "EspUsbDevice DescriptorDump"
  [3] "espusb-descdump"
  [6] "Dump CDC"
--- endpoint budget ---
  controller           S2/S3 full-speed
  highest ep number    3 / 6
  non-control IN       3 / 4
  non-control OUT      1 / 6
  double-buffered IN   mask=0x....
=== end of dump ===
```

大きさや endpoint 番号は選んだ組合せで変わります。実際の値は自分の出力で確認してください。

## 関連

- [EspUsbDeviceBringUpCheck](../EspUsbDeviceBringUpCheck/) - 先にこちらを動かす
- [docs/usb-device-guide.ja.md](../../../docs/usb-device-guide.ja.md) - descriptor の設計
- [Core API（v3）](../../../docs/usb-device-advanced.ja.md) - controller、endpoint の上限、FIFO
- [文字列 descriptor（v3）](../../../docs/usb-device-advanced.ja.md)

## 読み方と確認の手順

### 実行前の準備

宣言したUSB機能から生成されたdescriptorを調べる。

上のハードウェアの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

DUMP_ENABLEを一つずつ変えてbuildし直し、構成の長さ・interface・endpoint・HID report descriptorを比べます。これはdeviceが提示する内容を調べるもので、すべてのendpointやclassが実際にデータを交換したことは証明しません。

### APIの使い方と改造時の注意

開始・列挙・interfaceの発見・実際のclass通信を、別々の確認点として扱います。descriptorはdeviceが何を提示するかを示すだけで、転送の完了やprotocolの実装を証明しません。descriptorの走査を改造するときも、受け取った長さと容量の確認を残します。interface番号・endpoint address・USB address・物理portはそれぞれ別のものを指します。

大きなdumpは診断用で、高頻度のcallbackの手本ではありません。エラー名と、それを出した構成を記録します。

### 一つずつ確認する

最初は例を変更せず、出力とホスト側の見え方を照合します。Functionや設定を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

## v2 からの変更点

- endpoint の上限は v3 の controller 表に従います。S2/S3 の endpoint 番号は 6 まで、OUT は 6 本です（v2 の表は 5 と 5）。
- class 4 つまでという制限はなくなりました。endpoint と FIFO の予算で決まります。
- HID report descriptor の長さは `hidReportDescriptorLength()` で読むので、HID class descriptor から取り出す必要がなくなりました。
- 文字列は、番号 4 の固定の「class」文字列ではなく、ライブラリの配置（5 から Function 名）に沿って番号順に表示します。
- BOS は WebUSB がなくても、Vendor や DFU の Function があれば作られます（Microsoft OS 2.0 で WinUSB を割り当てるため）。WebUSB URL descriptor、MS OS 2.0 の vendor revision と形式、bulk IN の二重 buffer の mask も表示します。
- `DUMP_DESCRIPTORS_ONLY` で `startMode = DescriptorsOnly` を使い、controller を起動せずに descriptor を作れます。
- WebUSB の URL に `https://` を付けました。

- [スケッチ](EspUsbDeviceDescriptorDump.ino)
- [全サンプル](../../README.md)
- [サンプルの進め方](../../../docs/example-workflow.ja.md)
