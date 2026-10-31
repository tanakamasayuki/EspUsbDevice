# EspUsbDeviceConsole

> English: [README.md](README.md)

**serial terminal から手で** USB device を操作する例です。HID report や vendor の転送を 1 つずつ送り、
実験のたびに sketch を作り直さずに Host の反応を確かめられます。

Host から *下りてくる* もの（HID output report、HID protocol の切替、vendor control request とそのデータ、vendor bulk OUT）も、
`HOST_` を付けてすべて表示します。

## ハードウェア

- USB device に対応した Arduino-ESP32 board（ESP32-S2 / S3 / P4）
- 試験対象の USB host（通常は PC）
- **命令を入力する serial 接続**（書込みに使う UART / USB Serial-JTAG 側）

接続は 2 本です。命令は書込み用の port へ送り、USB device のコネクタは試験対象の Host につなぎます。
コネクタが 1 つしかない board では、相手役の board を使うか、抜いたときにログが消えることを受け入れてください。

## 命令

| 命令 | 意味 |
|---|---|
| `help` | 命令の一覧 |
| `state` | 構成と休止の状態、LED、HID protocol、vendor の状態と送信の空き |
| `text <string>` | 文字列をキー入力として送る |
| `key <usage> [modifiers]` | HID usage を 1 回押して離す。例 `key 0x04 0x02` |
| `hold <usage> [modifiers]` | 押したままにする |
| `release` | 押しているものをすべて離す |
| `mouse <dx> <dy> [buttons] [wheel]` | mouse report を送る |
| `click <1\|2\|4>` | 左 / 右 / 中央 click |
| `report <id> <hex...>` | HID input report をそのまま送る。例 `report 1 00 00 04 00 00 00 00 00` |
| `vendor <hex...>` | vendor の bulk IN endpoint へ byte を送る |
| `vendortext <string>` | 同じことを ASCII で |

数値は `0x04` でも `4` でも受け付けます。capture や datasheet では 16 進と 10 進が混ざり、書き写すときの手での変換が誤りのもとになるためです。

## 何に使うか

- **Host がどの usage に反応するかを調べる。** アプリが一部のキーしか拾わない、ゲームが特定のボタン配置しか読まない、といったときに 1 つずつ試します。
- **report descriptor を決める前に生の report を試す。** `report` は任意の byte 列を送るので、Host がどう読むかを確かめられます。keyboard と mouse は HID interface 0 を共有し、report ID（keyboard 1、mouse 2）で区別されます。payload に report ID の byte は含めません。
- **Host が送ってくるものを観察する。** Host の専用ソフトを動かしながら `HOST_VENDOR_CONTROL` / `HOST_VENDOR_CONTROL_DATA` / `HOST_VENDOR_OUT` / `HOST_HID_OUTPUT` を見ると、期待されている初期化の手順が分かります。
- **boot protocol への切替を見る。** BIOS / UEFI は boot protocol を要求します。`HOST_HID_PROTOCOL` でその瞬間が分かります。

vendor control request には、既定で固定の banner（IN）を返すか、最大 64 byte を受け取って（OUT）応答します。実際の protocol を再現したいときは sketch の `handleVendorControl()` を書き換えてください。
`wIndex = 7` の `bRequest = 0x02` は Microsoft OS 2.0 の要求で、sketch に届く前にライブラリが応答します。

## 注意

- 構成されていない間の送信は拒否され、溜めておかれることはありません。先に `state` を確認してください。
- `vendor` には Host が device を構成している必要があります。byte が送信 ring から出ていくには、Host のアプリ（libusb / WinUSB / WebUSB）が bulk IN を読む必要があります。`written` は ring が受け付けた量です。
- bulk OUT は callback 方式で受けます。`HOST_VENDOR_OUT` の 1 行は受信した USB packet 1 つで、Host の 1 回の書込みとは限りません。
- keyboard と mouse として登録されるので、**キー入力は Host でフォーカスのあるところに実際に入ります。** 捨ててよい text editor を前面にして試してください。
- callback はすべて USB worker で動き、`begin()` の前に登録します。

## Serial 出力の例

```text
=== EspUsbDevice console ===
BEGIN ok - connect the device connector to the host under test
MOUNTED
HOST_HID_PROTOCOL instance=0 protocol=report
state
STATE mounted=1 suspended=0 leds=0x00 hid_protocol=report vendor_mounted=1 vendor_tx_free=512
key 0x04
SEND key usage=0x04 modifiers=0x00 ok=1
report 1 00 00 05 00 00 00 00 00
SEND report id=1 length=8 ok=1 error=ESP_OK data=00 00 05 00 00 00 00 00
HOST_HID_OUTPUT leds=0x02 num=0 caps=1 scroll=0
```

## 関連

- [EspUsbDeviceBringUpCheck](../EspUsbDeviceBringUpCheck/) - 何も認識されないときはここから
- [EspUsbDeviceDescriptorDump](../EspUsbDeviceDescriptorDump/) - 宣言している内容を確かめる
- [docs/usb-device-guide.ja.md](../../../docs/usb-device-guide.ja.md) - 立ち上げの全手順
- [HID API（v3）](../../../docs/classes/hid.ja.md)、[Vendor API（v3）](../../../docs/classes/vendor.ja.md)

## 読み方と確認の手順

### 実行前の準備

診断consoleからHIDとVendorデータを明示的に操作する。

上のハードウェアの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

help/stateから始め、捨ててよい文書でtextか1つのkeyを試します。key（押して離す）とhold/releaseを区別し、次の試験の前に解放を確かめます。UARTの命令は生成するUSB動作を操作するもので、Vendor bulk interfaceそのものではありません。

### APIの使い方と改造時の注意

開始・列挙・interfaceの発見・実際のclass通信を、別々の確認点として扱います。descriptorはdeviceが何を提示するかを示すだけで、転送の完了やprotocolの実装を証明しません。interface番号・endpoint address・USB address・物理portはそれぞれ別のものを指します。

まず同じdeviceを直結して再接続で比べます。hubやFunctionの追加は、単純な場合を理解してからにします。エラー名と、それを出した操作を記録します。callbackからのログ出力は診断用で、高頻度の通信の手本ではありません。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。ホストがない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。命令や応答を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

## v2 からの変更点

- **`state` は bus 速度を表示しなくなりました。** v2 は TinyUSB の `tud_speed_get()` で読んでいましたが、v3 には交渉した速度を返す公開 API がありません。代わりに `suspended` と vendor の送信の空きを表示します。
- vendor control request を段階ごとに処理します。データ付きの OUT 要求には受信先を渡し、ACK の後にその byte を `HOST_VENDOR_CONTROL_DATA` として表示します。v2 は status 段階だけを返していました。64 byte を超える要求は STALL します。
- bulk OUT は `rxMode = Callback` と `onRxData()` で受けるので、受信 ring を確保しません。v2 は `onRx()` の中で FIFO を読んでいました。
- `report` は送信に失敗すると `lastErrorName()` を表示します。
- `vendor` は `flush()` を呼ばなくなりました。既定の `autoFlush` で送信が始まります。
- callback の登録と `configure()` の結果を `begin()` の前に確認します。

- [スケッチ](EspUsbDeviceConsole.ino)
- [全サンプル](../../README.md)
- [サンプルの進め方](../../../docs/example-workflow.ja.md)
