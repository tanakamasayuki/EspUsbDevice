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

## 読み方と確認の手順

### 実行前の準備

Vendor bulkとcontrol命令を使ってfirmwareを受け取る。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

control要求でstart・commit・abortを予約し、`loop()`で更新とbulk受信を処理します。要求番号とstatusを読み、control応答成功をFlash書き込み完了と考えないようにします。Host uploaderでも部分入力と報告されたエラーを扱います。

### APIの使い方と改造時の注意

選んだtargetとpartition構成のapplication imageを使い、結合Flash dumpやbootloader binaryと混同しないようにします。利用可否・容量を確認してから送信します。転送の応答、受信byte数、image検証、boot先選択、次の起動成功は別段階です。ROM loaderの例はboot modeの変更であり、同じアプリ内更新ではありません。

試験前に記載の復旧・書き込み経路を確保します。通常のfirmware更新でNVSが残る場合があるので、新しいアプリが初期設定状態とは仮定しません。`markValid()`はアプリの検証状態に関わるもので、任意の受信imageの真正性を証明するAPIではありません。実製品では認証・image検証の方針も必要です。Flash・再起動は例の実行経路に置き、新たな長時間USB callbackへ移さないようにします。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](FirmwareVendor.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
