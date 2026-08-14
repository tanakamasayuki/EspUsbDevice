# EspUsbDeviceConsole

> English: [README.md](README.md)

Serial monitor から**手打ちで USB 転送を試す**ためのスケッチです。
実験のたびにビルドし直さずに、HID report や vendor bulk を 1 件ずつ送って
Host の反応を見られます。

同時に、Host から降りてくるもの（HID output report、HID protocol 切り替え、
vendor control request、vendor bulk OUT）を `HOST_` 接頭辞ですべて表示します。

## ハードウェア

- USB device 対応の Arduino-ESP32 board（ESP32-S2 / S3 / P4）
- 検証対象の USB host（PC など）
- **コマンド入力用の Serial 接続**（書き込みに使う UART / USB Serial-JTAG 側）

シリアル接続は 2 系統関わります。コマンドは書き込みに使うポートに打ち、
USB device コネクタは検証対象の Host につなぎます。コネクタが 1 つしかないボードでは、
peer ボードを使うか、抜いた時点でログが切れることを承知で使ってください。

## コマンド

| コマンド | 内容 |
|---|---|
| `help` | コマンド一覧 |
| `state` | mount 状態、速度、LED、HID protocol |
| `text <string>` | 文字列をキー入力として送る |
| `key <usage> [modifiers]` | raw HID usage を 1 回送る（例 `key 0x04 0x02`） |
| `hold <usage> [modifiers]` | 押しっぱなしにする |
| `release` | 押下中のキーをすべて解放 |
| `mouse <dx> <dy> [buttons] [wheel]` | mouse report を送る |
| `click <1\|2\|4>` | 左 / 右 / 中クリック |
| `report <id> <hex...>` | raw HID report を送る（例 `report 1 00 00 04 00 00 00 00 00`） |
| `vendor <hex...>` | vendor bulk IN に生バイトを送る |
| `vendortext <string>` | 同じものを ASCII で送る |

数値は `0x04` でも `4` でも受け付けます。キャプチャや仕様書は 16 進と 10 進が混在するので、
書き写すときに基数を変換しなくて済むようにしています。

## 使いどころ

- **どの usage に Host が反応するか調べる。** アプリケーションが特定のキーだけ拾う、
  ゲームが特定のボタン配置しか見ない、といった場合に 1 個ずつ試せます。
- **report descriptor を確定する前に、生 report を試す。** `report` で任意のバイト列を
  投げ、Host 側の見え方を確認してから descriptor を書けます。
- **Host が何を送ってくるかを見る。** Host 側の純正ソフトを動かしながら
  `HOST_VENDOR_CONTROL` / `HOST_VENDOR_OUT` / `HOST_HID_OUTPUT` を眺めると、
  Host が期待している初期化シーケンスがわかります。
- **boot protocol の切り替わりを確認する。** BIOS / UEFI では Host が boot protocol を
  要求します。`HOST_HID_PROTOCOL` がその瞬間を表示します。

vendor control request には既定で固定の banner を返します。実際のプロトコルを再現する
場合は、スケッチ内の `onControlRequest` を書き換えてください。

## 注意

- `not mounted` のときの送信は queue されずに捨てられます。まず `state` を確認してください。
- `vendor` 系は Host 側が interface を claim している必要があります
  （Linux/WinUSB/WebUSB など）。claim されていないと `vendor_mounted=0` のままです。
- keyboard / mouse として登録されているので、**Host の入力欄にフォーカスがあると
  実際に文字が入力されます。** テキストエディタなど安全な場所で試してください。

## 想定 Serial 出力

```text
=== EspUsbDevice console ===
BEGIN ok - connect the device connector to the host under test
MOUNTED
HOST_HID_PROTOCOL instance=0 protocol=report
state
STATE mounted=1 speed=full leds=0x00 hid_protocol=report vendor_mounted=0
key 0x04
SEND key usage=0x04 modifiers=0x00 ok=1
report 1 00 00 05 00 00 00 00 00
SEND report id=1 length=8 ok=1 data=00 00 05 00 00 00 00 00
HOST_HID_OUTPUT leds=0x02 num=0 caps=1 scroll=0
```

## 関連

- [EspUsbDeviceBringUpCheck](../EspUsbDeviceBringUpCheck/) — 列挙されない場合はまずこちら
- [EspUsbDeviceDescriptorDump](../EspUsbDeviceDescriptorDump/) — 何を宣言しているかの確認
- [docs/usb-device-guide.ja.md](../../../docs/usb-device-guide.ja.md) — 実験の進め方全体
