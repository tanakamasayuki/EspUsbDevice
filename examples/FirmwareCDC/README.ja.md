# EspUsbDevice FirmwareCDC

> English: [README.md](README.md)

最小のファームウェア更新です。素の CDC serial port に、長さを送ってからその分の
バイトを送るだけ。host 側を標準ツールではなく自前スクリプトにしたい場合や、
すでに serial port を持つ機器に更新コマンドを足すときの雛形として使えます。

## ハードウェア

- USB device 対応の ESP32-S2 / ESP32-S3 / ESP32-P4 board
- Python と `pyserial` が使える PC
- ログ確認用の Serial 接続

## 前提

**application partition が 2 つある** partition scheme。

## プロトコル

```
host -> device   "FW <size>\n"
device -> host   "READY\n"          （または "ERR <reason>\n"）
host -> device   <size> バイト
device -> host   "OK\n"             （または "ERR <reason>\n"）、その後再起動
```

長さを先に送るのは、CDC が framing の無いバイトストリームだからです。長さが無いと
device はイメージの終わりと単なる間を区別できません。

## 使い方

```sh
uv run --with pyserial python3 firmware_cdc.py /dev/ttyACM0 build/FirmwareCDC.ino.bin
```

## 主な API

- `EspUsbDeviceCdcSerial port(device, "Firmware")` が転送路です。
- `EspUsbDeviceFirmwareUpdate update` が flash 側を担当します
  （`begin(size)` / `write()` / `end()` / `abort()`）。
- 実際のサイズを渡す `update.begin(size)` は、partition より大きいイメージを
  **host が 1 バイトも送る前に**拒否します。
- `update.end()` は boot partition を移す前に検証するので、途中で切れた upload は
  次回起動時ではなくそこで落ちます。

## 注意

- **flash への書き込みは USB callback ではなく `loop()` で行っています。** この
  ライブラリの class callback はすべて usbd task 上で動き、flash の erase は
  device 全体から数ミリ秒を奪います。このスケッチは代わりに `port.available()` を
  polling します。これは書き方の偶然ではなく、真似すべきパターンです。
- full-speed bulk の上限は理論値 1.216 MB/s で、CDC の framing と chunk ごとの flash
  書き込みでそれを大きく下回ります。300KB のイメージなら十分ですが、P4 で 1MB なら
  [`FirmwareVendor`](../FirmwareVendor/) の方が速く、
  [`FirmwareDFU`](../FirmwareDFU/) なら標準ツールが使えます。
- このプロトコル自体には認証も整合性チェックもありません。検証するのはイメージ自身の
  checksum を見る `end()` だけです。port を開ける相手なら誰でも書き換えられます。
- このライブラリは Arduino 標準の USB device class と同時には使えません。
