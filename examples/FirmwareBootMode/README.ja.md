# EspUsbDevice FirmwareBootMode

> English: [README.md](README.md)

動作中のスケッチから、チップの **ROM download loader**（BOOT を押しながら reset した
ときの状態＝いわゆる boot mode）へ入る例です。誰もボードに触らずに `esptool` で
flash 全体を書き換えられます。

これは他のすべての更新経路の背後にある復旧手段です。OTA partition が無い
ファームウェアでも、自分を更新できないほど壊れたファームウェアでも通用します。
loader は mask ROM にあり、壊すことができないからです。

## ハードウェア

- USB device 対応の ESP32-S2 / ESP32-S3 / ESP32-P4 board
- `esptool` の入った PC
- ログ確認用の Serial 接続

## 動作内容

CDC serial port と DFU runtime interface を提供し、loader を要求する方法を
3 通り用意します。行き先はすべて同じです。

| 操作 | host 側 |
|---|---|
| CDC port を 1200bps で開いて DTR を落とす | `arduino-cli upload …`、Arduino IDE、`stty -F /dev/ttyACM0 1200` |
| `DFU_DETACH` | `dfu-util -e` |
| CDC port に `b` を送る | 自前のスクリプト |

あとは通常どおり書き込みます。

```sh
esptool --port <port> write_flash 0x0 firmware.bin
```

## loader がどのポートで応答するか

ここがチップごとに違い、いちばん驚かれる部分です。

| Chip | 再起動後 |
|---|---|
| ESP32-S3 | 共有 PHY が USB Serial/JTAG に戻るので、コネクタ 1 つのボードでは**同じケーブルがそのまま使えます** |
| ESP32-S2 | OTG port 上の ROM 自身の CDC |
| ESP32-P4 | USB Serial/JTAG port。デバイスが載っていた高速 OTG コネクタ**ではありません** |

詳細（Secure Boot と `USB_PHY_SEL` eFuse の影響を含む）は
[USB経由のファームウェア更新 2.3節](../../docs/ota-over-usb.ja.md#23-romがどのusb-interfaceで応答するか)
にあります。

## 主な API

- `device.rebootToBootloader()` — USB device を detach し、ターゲットの
  download-boot フラグを立てて再起動します。戻りません。フラグのレジスタは
  チップごとに違い、ESP32-P4 ではソフトウェアリセットのビットと同じレジスタに
  同居しています。これがライブラリの API になっている理由です。
- `device.rebootToRomDfu()` — S2/S3 の ROM を、serial loader ではなく USB-OTG 上の
  DFU デバイスとして立ち上げます。`dfu-util` を使う host 向け。ESP32-P4 では
  再起動せず `false` を返します。
- `port.onLineCoding(cb)` — host が要求した baud rate が渡ります。1200bps touch の
  検出はこれです。
- `EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Runtime)` — `dfu-util -e` が
  話しかける interface を追加します。`onDetach()` を設定しなければ、自分で
  loader へ再起動します。

## 注意

- **callback ではなく `loop()` で実行してください。** このライブラリの class
  callback はすべて usbd task 上で動き、その中から再起動すると host がまだ
  終えていない control transfer を切ってしまいます。このスケッチはフラグを立て、
  `loop()` で再起動します。
- **意図的な操作の後ろに置いてください。** 紛れ込んだ 1 バイトでユーザーの作業が
  bootloader に落ちるべきではありません。1200bps touch は host が既に話せる作法です。
- Arduino-ESP32 の `usb_persist_restart()` は同じ仕事をしますが、このライブラリを
  使うスケッチからは **link できません**。`esp32-hal-tinyusb.c` を引き込み、同じ
  TinyUSB callback を 2 つ二重定義するためです。しかも ESP32-P4 では何もしません。
  `rebootToBootloader()` を使ってください。
- download-boot フラグは残りません。`esptool` が書き込んで reset すると、
  ボードは通常どおり新しいアプリケーションを起動します。
- これは **flash 全体**を置き換えます。[`FirmwareDFU`](../FirmwareDFU/) や
  [`FirmwareHTTP`](../FirmwareHTTP/) はスケッチを動かしたまま OTA partition を
  1 つ書く方式で、そこが違います。
