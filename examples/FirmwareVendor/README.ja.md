# EspUsbDevice FirmwareVendor

> English: [README.md](README.md)

vendor 独自の interface で firmware を更新する例です。命令は control 要求、image は bulk OUT endpoint で送ります。
**このライブラリで最も速い経路**で、ESP32-P4 の HS 接続の bulk OUT なら、DFU の EP0 転送のごく一部の時間で image を送れます。

その代わり、ホスト側は自分で書いて配る必要があります。標準ツールで同じことをするなら、endpoint を使わない [FirmwareDFU](../FirmwareDFU/) があります。

## ハードウェア

- ESP32-S2 / ESP32-S3 / ESP32-P4 の board。**native USB OTG** の口でつなぐ
- Python 3 と `pyusb`（と libusb の backend）の入った PC
- ログ用の別の Serial 接続

## 必要な条件

**application partition が 2 つある** partition scheme が必要です。`sketch.yaml` の全 profile が使う Arduino の「Default」scheme にはあります。1 つしかないとスケッチは `NO_OTA_PARTITION` を表示します。試す前に復旧の経路（ROM loader）を用意してください。

## プロトコル

| 方向 | 要求 | 意味 |
|---|---|---|
| control OUT `0x40` | `0x01`、`wValue`＝長さの下位 16 bit、`wIndex`＝上位 16 bit | 開始 |
| bulk OUT | — | image |
| control OUT `0x40` | `0x02` | 確定（検証）して再起動 |
| control OUT `0x40` | `0x03` | 中止 |
| control IN `0xc0` | `0x04` → 8 byte | `[0]` 実行中、`[1..3]` 最後の `esp_err_t`（LE）、`[4..7]` 書いた byte 数（LE） |

命令は EP0、データは bulk、という形が手本にする価値のある点です。status 要求は bulk endpoint が忙しい間も含めていつでも答えられるので、ホストは転送を止めずに進捗を見られます。それ以外の vendor 要求は STALL します。

## 使い方

```sh
uv run --with pyusb python3 firmware_vendor.py build/FirmwareVendor.ino.bin
```

[`firmware_vendor.py`](firmware_vendor.py) は `303a:4000` を探して vendor interface を claim し、START を送り、image を 16 KiB ずつの bulk 転送で送り、device が全部書き終わるまで STATUS を poll してから COMMIT を送ります。

Linux では device node への書込み権限が要ります。`SUBSYSTEM=="usb", ATTR{idVendor}=="303a", ATTR{idProduct}=="4000", MODE="0666"` のような udev rule か `sudo` を使ってください。Windows では、このライブラリが出す Microsoft OS 2.0 descriptor により、vendor interface に WinUSB driver が自動で当たります。

image は普通の Arduino の `.bin`（`<build-path>/<sketch>.ino.bin`）で、`*.merged.bin` や `*.bootloader.bin` ではありません。

## 主な API

- `EspUsbDeviceVendor vendor(device)` — 転送路。`vendor.configure(EspUsbVendorConfig)` で容量を決める。この例は受信 ring 16 KiB、送信 ring なし（`txCapacity = 0`、`transferBytes = 0`）。応答は EP0 で返すため。
- `vendor.onControlRequest()` — 命令を処理する（SETUP・DATA・ACK の各段階で呼ばれる。この例は SETUP で判断する）。応答は `sendControlResponse()`。停止中に登録し、`esp_err_t` を返す。
- `EspUsbDeviceFirmwareUpdate update` — flash 側。`begin(size)` / `write()` / `end()` / `abort()` / `written()` / `lastError()`。
- `vendor.available()` / `read()` — `loop()` から受信 ring を読み出す。

## Serial の出力例

```text
Ready. python3 firmware_vendor.py <firmware.bin>
Receiving 301456 bytes into app1
1 KiB
...
294 KiB
Verified - restarting into the new firmware
```

## 注意

- **control の callback は flag を立てるだけで、仕事は `loop()` が行います。** control 要求は USB worker で動き、そこで flash を消去すると device 全体が止まります。受信 ring がいっぱいの間、`loop()` が書いている間は bulk endpoint が NAK を返します。これはホストを待たせているだけで、device が止まっているのではありません。
- 持続的な速度を決めるのは受信 ring の大きさです（特に ESP32-P4 の HS）。ring が深いほど、flash の書込みより先に endpoint を進められます。実行時の `configure()` の値なので、別の flag で再ビルドする必要はありません。
- プロトコルに認証はありません。interface を claim できる人なら誰でも書き換えられます。`markValid()` も真正性の確認ではありません。
- 送った image がこのスケッチを置き換えます。更新経路を持つ image を送ってください。
- Arduino 内蔵の USB class（USB MSC / DFU / CDC on boot）をこのライブラリと同時に有効にしないでください。

## 確認の手順

control 要求は開始・確定・中止の仕事を予約するだけで、更新と bulk の読み出しは `loop()` が行います。control の応答は flash の書込み完了を意味しません。ホストは STATUS の `written` を見てから確定します。ホストの uploader は、途中までの入力や報告されたエラーも扱う必要があります。

## v2 からの変更点

- 受信 ring を `build_opt.h` の `CFG_TUD_VENDOR_RX_BUFSIZE` ではなく、実行時に `vendor.configure()` で決めます（16 KiB、送信 ring なし）。
- callback は、vendor 要求であることを確かめてから処理します。
- STATUS の byte 1〜3 に、最後の `esp_err_t` の下位 24 bit を入れるようにしました（v2 は byte 1 に下位 8 bit、byte 2〜3 は 0）。同梱の script は 3 byte を読み、16 進で表示します。v2 の script でも、よくあるエラーでは byte 1 が 0 以外になります。
- `loop()` は 1 KiB ずつ読みます。

## 関連

- [FirmwareCDC](../FirmwareCDC/) — 同じことを serial port で
- [FirmwareDFU](../FirmwareDFU/) — 同じことを `dfu-util` で
- [VendorEcho](../VendorEcho/VendorEcho.ino) — 最小の vendor bulk の例
- [USB 経由のファームウェア更新](../../docs/ota-over-usb.ja.md)
- [Vendor API ガイド](../../docs/classes/vendor.ja.md)
- [スケッチ](FirmwareVendor.ino) / [全サンプル](../README.md) / [サンプルの進め方](../../docs/example-workflow.ja.md)
