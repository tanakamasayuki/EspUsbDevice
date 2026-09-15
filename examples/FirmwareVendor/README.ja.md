# EspUsbDevice FirmwareVendor

> English: [README.md](README.md)

vendor 固有 interface 経由のファームウェア更新です。コマンドは control request、
イメージは bulk OUT endpoint。**ライブラリ内で最速の経路**で、ESP32-P4 の
high-speed リンクでは DFU の EP0 転送に比べてはるかに短時間でイメージが渡ります。

代償は host 側を自分で書いて配る必要があることです。同じ仕事を標準ツールで、しかも
endpoint 消費 0 でやるのが [`FirmwareDFU`](../FirmwareDFU/) です。

## ハードウェア

- USB device 対応の ESP32-S2 / ESP32-S3 / ESP32-P4 board
- Python と `pyusb` が使える PC
- ログ確認用の Serial 接続

## 前提

**application partition が 2 つある** partition scheme。

## プロトコル

| 方向 | Request | 意味 |
|---|---|---|
| control OUT `0x40` | `0x01`、`wValue`=size 下位、`wIndex`=size 上位 | 開始 |
| bulk OUT | — | イメージ |
| control OUT `0x40` | `0x02` | commit して再起動 |
| control OUT `0x40` | `0x03` | 中止 |
| control IN `0xc0` | `0x04` → 8 バイト | active flag、error、書き込み済みバイト数 |

コマンドを EP0、データを bulk に分けるのが真似する価値のある形です。status 要求は
bulk endpoint が忙しい最中を含めていつでも答えられるので、host は転送を邪魔せずに
進捗を見られます。

## 使い方

```sh
uv run --with pyusb python3 firmware_vendor.py build/FirmwareVendor.ino.bin
```

Linux では device node への書き込み権限が要ります（VID:PID 向けの udev rule か
`sudo`）。Windows では、このライブラリが出す Microsoft OS 2.0 descriptor により
vendor interface に WinUSB driver が自動で当たります。

## 主な API

- `EspUsbDeviceVendor vendor(device)` が転送路です。`onControlRequest()` で
  コマンドを受け、`sendControlResponse()` で応答します。
- `EspUsbDeviceFirmwareUpdate update` が flash 側を担当します。
- `vendor.available()` / `read()` で `loop()` から bulk FIFO を吸い出します。

## 注意

- **control callback はフラグを立てるだけで、仕事は `loop()` がします。** control
  request も `onRx()` も usbd task 上で動き、そこで flash を erase すると device 全体が
  止まります。`loop()` が書いている間 bulk endpoint は NAK を返しますが、それは
  device が止まっているのではなく host が減速しているだけです。
- ESP32-P4 では `build_opt.h` から `CFG_TUD_VENDOR_RX_BUFSIZE` を上げて、FIFO を
  flash 書き込みより先行させてください。ライブラリの既定値は一般的な用途向けで、
  一方向の連続ストリーム向けではありません
  （[応用ガイド 5.4節](../../docs/usb-device-advanced.ja.md#54-バッファのサイズ)）。
- このプロトコルに認証はありません。interface を claim できる相手なら誰でも
  書き換えられます。
- このライブラリは Arduino 標準の USB device class と同時には使えません。
