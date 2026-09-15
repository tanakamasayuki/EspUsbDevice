# EspUsbDevice FirmwareDFU

> English: [README.md](README.md)

`dfu-util` で USB 経由のファームウェア更新を行う例です。スケッチは動き続け、USB
キーボードのままです。ROM bootloader は介在せず、ボタンも押しません。

DFU が使うのは **interface 1 本、endpoint 0 本**です（全転送が EP0 を通ります）。
endpoint 予算を使い切ったデバイスにも足せます。他の経路は
[ファームウェア更新ガイド](../../docs/ota-over-usb.ja.md) を参照してください。

## ハードウェア

- USB device 対応の ESP32-S2 / ESP32-S3 / ESP32-P4 board
- [`dfu-util`](https://dfu-util.sourceforge.net/) を入れた PC
- ログ確認用の Serial 接続

## 前提

**application partition が 2 つある** partition scheme が必要です。Arduino の
「Default」にはありますが「Huge APP」にはありません。1 つしか無い場合、スケッチは
起動時に `NO_OTA_PARTITION` を表示します。

## 動作内容

- HID キーボードと DFU interface を 1 台のデバイスとして提供
- DFU でファームウェアイメージを受け取り、空いている OTA partition へ書き込む
- 検証し、boot partition を切り替え、新しいファームウェアで再起動する
- 1 KiB ごとに進捗を表示し、失敗は DFU status code として報告する

## 使い方

```sh
dfu-util -l                                   # デバイスを探す
dfu-util -D build/FirmwareDFU.ino.bin         # 書き込む
```

Linux では `303a:*` 向けの udev rule か `sudo` が必要です。Windows では DFU
interface に WinUSB driver が必要で、[Zadig](https://zadig.akeo.ie/) で入ります。

送るのは Arduino の素の `.bin`（スケッチ → コンパイル済みバイナリをエクスポート、
または `arduino-cli compile` が残す `build/` の中身）です。`idf.py dfu` が作る
`dfu.bin` コンテナ**ではありません**。あれは ROM の DFU 用で別物です
([ガイド 2.6](../../docs/ota-over-usb.ja.md#26-rom-serial-loaderではなくrom-dfuへ入る-s2s3))。

## 主な API

- `EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Download, "Firmware")` で
  function を登録します。`EspUsbDeviceDfuMode::Runtime` にすると、`dfu-util -e`
  に応えて ROM loader へ再起動するだけのデバイスになります
  （[`FirmwareBootMode`](../FirmwareBootMode/) 参照）。
- `dfu.onProgress(cb)` — ここまでに書き込んだバイト数。総量はありません。DFU 1.1 は
  イメージの長さを device に伝えないためです。
- `dfu.onComplete(cb)` — 検証が通り boot partition が移った後に呼ばれます。
  `false` を返すと元に戻します。
- `dfu.onError(cb)` — host へ返す DFU status code を受け取ります。
- `dfu.restartWhenComplete(false)` — 成功しても再起動せずスケッチを続けます。
- `EspUsbDeviceFirmwareUpdate::available()` / `targetLabel()` / `capacity()` —
  更新の書き込み先 partition の情報。
- `EspUsbDeviceFirmwareUpdate::markValid()` — 動作中のイメージを確定し、
  bootloader の rollback 待ちを解除します。

## 注意

- **送ったイメージがこのスケッチを置き換えます。** DFU interface を持つビルドを
  送ってください。そうしないと次の更新は boot mode 経由になります。
- ESP application でないファイルは最初の block で DFU status 3（`errWRITE`）として
  拒否され、壊れたイメージは最後の検証で status 7（`errVERIFY`）になります。
  どちらも boot partition に触れないので、間違ったファイルを送っても実害はありません。
- block size は `CFG_TUD_DFU_XFER_BUFSIZE`（既定 1024 byte）で、functional
  descriptor が宣言する値でもあります。大きなイメージでは `build_opt.h` から
  `-DCFG_TUD_DFU_XFER_BUFSIZE=4096` のように上げてください。
- `dfu-util` が最後の status 読み取りでエラーを出すことがあります。検証が通った
  0.5 秒後にデバイスが新しいイメージで再起動するため、その窓でもう一度ポーリングした
  host にはデバイスが消えたように見えます。更新自体は成功しています。メッセージでは
  なくボードを確認してください。
- rollback は自動ではありません。不正なイメージからの復帰が必要なら、この
  スケッチが呼ぶ `markValid()` に加えて
  `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` が必要です。
- このライブラリは Arduino 標準の USB device class と同時には使えません。
