# EspUsbDevice

EspUsbDevice は、新しい ESP32 Arduino USB Device ライブラリです。

Arduino-ESP32 標準の `USB`、`USBHIDKeyboard`、`USBHIDMouse` API との互換は
目標にしません。port、speed、descriptor、endpoint packet size、raw class
report をスケッチから明示的に制御できる、よりよい小さな USB Device ライブラリを
目指します。

最初の実装対象は `EspUsbHost` の peer / loopback テストです。これは実ハードウェアで
具体的に検証でき、ライブラリが制御すべき低レベル挙動を明確にできるためです。
テスト向けの機能を先に実装しますが、それはプロジェクトの最終的な範囲ではありません。

## リリース範囲

このリリースでは、HID keyboard / mouse / gamepad / consumer / system / custom / vendor HID、
CDC ACM、USB MIDI、MSC、USBVendor、最小 USB Audio sink を扱えます。

代表的な用途:

- layout 対応 keyboard、raw HID usage、mouse / gamepad / media key を送る。
- PC や `EspUsbHost` と CDC ACM serial / USB MIDI で通信する。
- RAM disk、FAT RAM disk、SD card を USB MSC として公開する。
- HID ではない vendor-specific bulk/control interface を作る。
- Host からの USB Audio speaker PCM を callback で受け取る。

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
- USBVendor bulk IN/OUT、control request、WebUSB landing URL。
- USB Audio speaker sink callback。
- pytest-embedded peer / loopback テスト用の serial command sketch。

USB Audio は単独 device の最小 speaker sink から開始しています。このライブラリの責務は
USB Audio class と PCM callback までです。受け取った PCM はアプリケーション、PCMFlow、
PCMFlowDevice など任意の処理系へ渡します。

- PCMFlow: https://github.com/tanakamasayuki/PCMFlow
- PCMFlowDevice: https://github.com/tanakamasayuki/PCMFlowDevice

## 最小例

Keyboard:

```cpp
#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

void setup()
{
  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4001;
  config.product = "EspUsbDevice Keyboard";
  device.begin(config);
}

void loop()
{
  if (device.ready())
  {
    keyboard.write("hello");
    delay(1000);
  }
}
```

CDC ACM serial:

```cpp
#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceCdcSerial SerialUSB(device);

void setup()
{
  EspUsbDeviceConfig config;
  config.product = "EspUsbDevice Serial";
  device.begin(config);
}

void loop()
{
  if (SerialUSB.connected())
  {
    SerialUSB.println("hello");
    delay(1000);
  }
}
```

## Examples

ユーザー向けの基本 sketch は [examples/README.ja.md](examples/README.ja.md) にまとめています。

- `Keyboard`: layout 付き ASCII 文字列と HID usage ID を送信する boot keyboard。
- `Mouse`: 移動、wheel、button を送信する boot mouse。
- `KeyboardMouse`: keyboard + mouse の composite HID。
- `Gamepad`: axes、hat、button を送信する HID gamepad。
- `MediaKeys`: volume、再生停止、system control を送信する HID media keys。
- `VendorHID`: 独自 63 byte report を送受信する vendor-defined HID。
- `USBVendor`: bulk IN/OUT と control request を扱う vendor-specific interface。
- `CustomHID`: sketch 定義の HID report descriptor を使う custom HID。
- `Serial`: PC / Host とテキストを送受信する CDC ACM serial。
- `MIDI`: note / control change を送受信する USB MIDI device。
- `MIDIController`: ADC / button input を MIDI CC / note に変換する controller。
- `MIDIInterface`: UART MIDI 1.0 と USB MIDI 1.0 の bridge。
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

## 制限事項

- Arduino-ESP32 標準の `USB.begin()`、`USBHIDKeyboard`、`USBHIDMouse` などとは併用しません。
- USB Audio は単独 device の最小 speaker sink 実装です。複合 Audio device は未対応です。I2S、codec、DAC などのデバイス接続はこのライブラリの責務外です。
- MSC は block device と filesystem を分けて扱います。Host から通常の drive として mount
  するには FAT RAM disk helper または SD card などを使います。
- flash / SPIFFS / LittleFS を USB MSC として直接公開することは標準方針にしません。
- SD card を MSC として Host に公開している間は、ESP32 側で同じ card の file API を使わないでください。
- WebUSB / Microsoft OS 2.0 descriptor の基本応答は Arduino-ESP32 TinyUSB core に依存します。
  custom vendor code、GUID、Microsoft OS 2.0 descriptor 内容の差し替え API は未実装です。

テスト構造と段階的なカバレッジ計画は [tests/TEST_PLAN.ja.md](tests/TEST_PLAN.ja.md)
を参照してください。
設計背景と `EspUsbHost` 既存テストからの移行メモは [docs/DESIGN_NOTES.ja.md](docs/DESIGN_NOTES.ja.md)
にまとめています。
現在の開発方針と残作業は [docs/DEVELOPMENT_PLAN.ja.md](docs/DEVELOPMENT_PLAN.ja.md)
にまとめています。
リリース前確認は [docs/RELEASE_CHECKLIST.ja.md](docs/RELEASE_CHECKLIST.ja.md) を参照してください。
