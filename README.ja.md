# EspUsbDevice

EspUsbDevice は、新しい ESP32 Arduino USB Device ライブラリです。

Arduino-ESP32 標準の `USB`、`USBHIDKeyboard`、`USBHIDMouse` API との互換は
目標にしません。port、speed、descriptor、endpoint packet size、raw class
report をスケッチから明示的に制御できる、よりよい小さな USB Device ライブラリを
目指します。

最初の実装対象は `EspUsbHost` の peer / loopback テストです。これは実ハードウェアで
具体的に検証でき、ライブラリが制御すべき低レベル挙動を明確にできるためです。
テスト向けの機能を先に実装しますが、それはプロジェクトの最終的な範囲ではありません。

## 設計目標

- `EspUsbHost` と同じように、明示設定と callback ベースの API にする。
- Arduino USB class descriptor に依存せず、descriptor はこのライブラリで所有する。
- HID は文字入力ではなく usage ID と raw report を第一級 API にする。
- ESP32-S3 2台構成の peer テストと、ESP32-P4 1台構成の loopback テストを
  初期検証ターゲットとして支える。
- Arduino-ESP32 標準 USB Device stack とは排他利用にする。このライブラリを使う
  スケッチでは `USB.begin()` を呼ばない。

## 現在のスコープ

最初のマイルストーンは、既存 `EspUsbHost` peer device を置き換え、実ハードウェアで
コア API を検証することです。HID MVP から始め、CDC ACM、USB MIDI、MSC まで peer /
loopback テストで確認できる範囲を広げています。

- device port / speed / VID / PID / string / power 設定。
- speed に応じた descriptor 生成と endpoint MPS 選択。
- HID boot keyboard の raw report 送信。
- HID keyboard output report callback による LED 状態受信。
- HID boot mouse の raw report 送信。
- HID consumer / system / gamepad / custom / vendor report。
- CDC ACM serial。
- USB MIDI event packet と note / control change helper。
- USB MSC block device と SCSI callback。
- pytest-embedded peer / loopback テスト用の serial command sketch。

## Examples

ユーザー向けの基本 sketch は [examples/README.ja.md](examples/README.ja.md) にまとめています。

- `Keyboard`: layout 付き ASCII 文字列と HID usage ID を送信する boot keyboard。
- `Mouse`: 移動、wheel、button を送信する boot mouse。
- `KeyboardMouse`: keyboard + mouse の composite HID。
- `Gamepad`: axes、hat、button を送信する HID gamepad。
- `Serial`: PC / Host とテキストを送受信する CDC ACM serial。
- `MIDI`: note / control change を送受信する USB MIDI device。
- `MSC`: RAM buffer を block device として公開する Mass Storage Class。
- `MSCFatRamDisk`: RAM 上の FAT12 disk で Host とファイルを受け渡す Mass Storage Class。
- `MSCSdCard`: SPI SD card を Host へ USB storage として公開する Mass Storage Class。

## HID Keyboard / Mouse APIs

Keyboard:

- `keyboard.setLayout(layout)` は EspUsbHost と同じ layout ID と keymap table を使い、
  Device 側では ASCII から usage への逆変換に使います。
- `keyboard.write(text)`、`tapKey(key)`、`pressKey(key)` は文字向けの上位 helper です。
- `keyboard.tapUsage()`、`pressUsage()`、`releaseUsage()`、`releaseAll()`、
  `sendReport()` で raw HID usage / report 制御もできます。
- `keyboard.onOutputReport(callback)` は Host からの LED output report を受け取ります。

Mouse:

- `mouse.move(x, y)`、`wheel(delta)`、`sendReport(report)` は移動と raw report を送信します。
- `mouse.press(buttons)`、`release(buttons)`、`releaseAll()`、`click(button)`、
  `buttons()` は Device 側 button 状態を保持して扱います。

## CDC / MIDI / MSC APIs

CDC ACM:

- `EspUsbDeviceCdcSerial` は USB serial の read / write callback と helper を提供します。
- `available()`、`read()`、`write()`、`print()` 系の Arduino らしい使い方と、
  raw callback の両方を扱えます。

USB MIDI:

- `EspUsbDeviceMidi` は 4 byte USB-MIDI event packet を送信します。
- `noteOn()`、`noteOff()`、`controlChange()` などの helper と `writePacket()` を併用できます。

MSC:

- `EspUsbDeviceMsc` は inquiry、media 状態、capacity、read/write callback を扱います。
- `EspUsbDeviceMscRamDisk` は外部 RAM buffer を block device として公開する helper です。
- `EspUsbDeviceMscFatRamDisk` は RAM 上に小さい FAT12 image を作り、Host との一時ファイル
  受け渡しに使う helper です。
- `EspUsbDeviceMscSdCard` は Arduino `SD` の raw sector I/O を MSC に接続する helper です。
- MSC は block device と filesystem が別です。OS からドライブとしてマウントするには、
  有効な FAT image か SD card などの実 storage を read/write callback に接続してください。
- flash / SPIFFS / LittleFS の直接公開は標準方針にしません。永続ストレージは SD card、
  一時ファイル受け渡しは RAM disk + FAT helper を優先します。

テスト構造と段階的なカバレッジ計画は [tests/TEST_PLAN.ja.md](tests/TEST_PLAN.ja.md)
を参照してください。
設計背景と `EspUsbHost` 既存テストからの移行メモは [docs/DESIGN_NOTES.ja.md](docs/DESIGN_NOTES.ja.md)
にまとめています。
現在の実機・ツール環境に基づく開発順序は [docs/DEVELOPMENT_PLAN.ja.md](docs/DEVELOPMENT_PLAN.ja.md)
にまとめています。
