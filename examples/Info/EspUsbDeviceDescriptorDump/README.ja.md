# EspUsbDeviceDescriptorDump

> English: [README.md](README.md)

登録したクラスから **このライブラリが実際に組み立てた descriptor** を全部表示します。

Host 側では「相手が何者か」を知るために descriptor を読みますが、Device 側では
descriptor を書くのは自分です。したがって知りたいことは逆で、
「自分が何を宣言したのか」「それはこの controller に収まるのか」になります。

## ハードウェア

- USB device 対応の Arduino-ESP32 board（ESP32-S2 / S3 / P4）
- Serial monitor 接続

USB host への接続は不要です。descriptor は `device.begin()` の中で組み立てられるので、
ケーブルを挿さなくても全内容を確認できます。

## 動作内容

スケッチ冒頭の `DUMP_ENABLE_*` でクラス構成を選び、次を表示します。

- DEVICE descriptor（hex + フィールド解釈）
- CONFIGURATION descriptor の Full Speed 版と High Speed 版（hex + ブロック単位の走査）
- DEVICE QUALIFIER と OTHER SPEED CONFIGURATION
- BOS descriptor と Microsoft OS 2.0 descriptor（WebUSB 有効時）
- HID report descriptor
- string descriptor
- **endpoint 予算**（このターゲットの controller 上限との比較）

```c
#define DUMP_ENABLE_KEYBOARD 1
#define DUMP_ENABLE_MOUSE    0
#define DUMP_ENABLE_GAMEPAD  0
#define DUMP_ENABLE_CDC      1
#define DUMP_ENABLE_MIDI     0
#define DUMP_ENABLE_VENDOR   0
#define DUMP_ENABLE_WEBUSB   0
```

複合デバイスを作る前に、ここで構成を切り替えてビルドし、endpoint 予算の行を読むのが
いちばん速い確認方法です。`EspUsbDevice` が保持できるクラスは 4 個までで、5 個目の
オブジェクトは constructor 内の `addClass()` が失敗して**黙って登録されません**。

## endpoint 予算

controller ごとの上限です。超える構成は `begin()` が PHY 起動前に
`ESP_ERR_INVALID_SIZE` で拒否します。

| controller | endpoint 番号の上限 | control 以外の IN | control 以外の OUT |
|---|---|---|---|
| ESP32-S2 / S3 | 5 | 4 | 5 |
| ESP32-P4 rhport 0（FullSpeed） | 6 | 4 | 6 |
| ESP32-P4 rhport 1（HighSpeed） | 15 | 7 | 15 |

ESP32-P4 では `config.controller` で FS / HS を選ぶため、同じ構成でも上限が変わります。
このスケッチの予算表示も `device.config().controller` に追従します。

## Host 側との突き合わせ

ここに出た CONFIGURATION の hex と、Host が受け取った内容は byte 単位で一致するはずです。

```sh
# Linux
lsusb -v -d 303a:4051

# どの OS からでも（PyUSB）
cd tests && uv run --with pyusb python manual/device_inspect/device_inspect.py --pid 0x4051
```

一致しない、あるいは Host 側に何も出ないなら、descriptor がバスに出ていないということで、
問題はこの層より下（列挙、電気、controller 設定）にあります。
まず [EspUsbDeviceBringUpCheck](../EspUsbDeviceBringUpCheck/) に戻ってください。

## 想定 Serial 出力

```text
=== EspUsbDevice descriptor dump ===
BEGIN ok error=ESP_OK
--- DEVICE descriptor (18 bytes) ---
  0000  12 01 00 02 00 00 00 40 3a 30 51 40 00 01 01 02
  0010  03 01
  bcdUSB=0x0200 class=0x00 (per-interface) subclass=0x00 protocol=0x00 ep0_mps=64
  idVendor=0x303a idProduct=0x4051 bcdDevice=0x0100 configurations=1
--- CONFIGURATION descriptor (full-speed) (98 bytes) ---
  ...
  0000  CONFIGURATION total=98 interfaces=3 value=1 attributes=0x80 power=100mA
  0009  INTERFACE  number=0 alt=0 endpoints=1 class=0x03 (HID) subclass=0x01 protocol=0x01
  0012  HID        bcdHID=0x0111 descriptors=1 report_descriptor=65 bytes
  ...
--- endpoint budget ---
  controller           S2/S3 full-speed
  highest ep number    3 / 5
  non-control IN       3 / 4
  non-control OUT      1 / 5
=== end of dump ===
```

## 関連

- [EspUsbDeviceBringUpCheck](../EspUsbDeviceBringUpCheck/) — 先にこちらを動かす
- [tests/manual/device_inspect](../../../tests/manual/device_inspect/) — Host 側から見た同じ descriptor
- [docs/usb-device-guide.ja.md](../../../docs/usb-device-guide.ja.md) — descriptor の設計方針
