# EspUsbDevice FirmwareDFU

> English: [README.md](README.md)

スケッチを動かしたまま（USB キーボードのまま）、`dfu-util` で USB 経由に firmware を更新する例です。ROM bootloader は使わず、ボタンも押しません。

DFU が使うのは **interface 1 つで、endpoint は 0 本**です（転送はすべて EP0）。endpoint を使い切った device にも追加できます。他の経路は [ファームウェア更新ガイド](../../docs/ota-over-usb.ja.md) を参照してください。

## ハードウェア

- ESP32-S2 / ESP32-S3 / ESP32-P4 の board。**native USB OTG** の口でつなぐ
- [`dfu-util`](https://dfu-util.sourceforge.net/) を入れた PC
- ログ用の別の Serial 接続

## 必要な条件

**application partition が 2 つある** partition scheme が必要です。`sketch.yaml` の全 profile が使う Arduino の「Default」scheme にはあります。「Huge APP」にはなく、partition が 1 つしかないと起動時に `NO_OTA_PARTITION` を表示します。

試す前に復旧の経路（ROM loader。[FirmwareBootMode](../FirmwareBootMode/) または BOOT＋reset）を用意してください。

## 動作内容

- 1 つの device に HID キーボードと DFU interface を置く（キーボードは何も入力しない。DFU が他の Function と同居できることを示すため）
- DFU の block を 4096 byte（`wTransferSize`）と宣言する
- DFU で受け取った firmware image を予備の OTA partition へ書く
- image を検証し、boot partition を切り替えて再起動する
- 1 KiB ごとに進捗を表示し、失敗は DFU の status code で示す
- ホストが構成を済ませたら `markValid()` で実行中の image を確定する

## 使い方

```sh
dfu-util -l                                          # device を探す（303a:4000）
dfu-util -d 303a:4000 -D build/FirmwareDFU.ino.bin   # 書き込む
```

Linux では、`303a:*` 用の udev rule か `sudo` が要ります。例：`/etc/udev/rules.d/99-espusb.rules`

```text
SUBSYSTEM=="usb", ATTR{idVendor}=="303a", MODE="0666"
```

Windows では DFU interface に WinUSB driver が要ります。[Zadig](https://zadig.akeo.ie/) で入れられます。

image は普通の Arduino の `.bin` です（「コンパイルしたバイナリをエクスポート」、または `arduino-cli compile` の `<build-path>/<sketch>.ino.bin`）。
`idf.py dfu` が作る `dfu.bin` ではありません。あれは ROM の DFU 用で、別物です（[ガイド 2.6 節](../../docs/ota-over-usb.ja.md#26-romのserial-loaderではなくrom-dfuを使うs2s3)）。`*.merged.bin` や `*.bootloader.bin` でもありません。

## 主な API

- `EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Download, "Firmware")` — Function を登録する。`EspUsbDeviceDfuMode::Runtime` にすると、`dfu-util -e` に ROM loader への再起動で応えるだけの device になる（[FirmwareBootMode](../FirmwareBootMode/)）。
- `dfu.configure(EspUsbDfuConfig)` — block の大きさ（`transferBytes`、64〜65535、既定 1024）、poll 時間、`manifestationTolerant` を設定する。停止中に呼ぶ。
- `dfu.onProgress(cb)` — これまでに書いた byte 数。DFU 1.1 は image の長さを device に伝えないので、総量はない。
- `dfu.onComplete(cb)` — image の検証と boot partition の切り替えの後に呼ばれる。`false` を返すと元に戻す。
- `dfu.onError(cb)` — ホストへ返す `EspUsbDfuStatus`（`Write` = 3、`Verify` = 7 など）を受け取る。
- `dfu.restartWhenComplete(false)` — download が成功しても（検証の約 500 ms 後の）再起動をせず、スケッチを動かし続ける。
- `dfu.setBackend()` — 内蔵の OTA の代わりに、独自の保存先（外部 flash、別の processor など）を使う（[DFU ガイド](../../docs/classes/dfu.ja.md)）。
- `EspUsbDeviceFirmwareUpdate::available()` / `targetLabel()` / `capacity()` — 更新の書込み先 partition。
- `EspUsbDeviceFirmwareUpdate::markValid()` — 実行中の image を確定し、bootloader の rollback を取り消す。

## Serial の出力例

```text
Update target: app1 (1310720 bytes)
Ready (wTransferSize=4096). dfu-util -D <firmware.bin>
DFU 4 KiB
...
DFU 300 KiB
DFU complete - restarting into the new firmware
```

## 確認済みの内容

2.x では、このスケッチを動かす ESP32-P4 を PC から操作し、DFU interface が HS で列挙されること、functional descriptor の `wTransferSize=1024` / `bcdDFU=0x0110` / `canDnload` / `manifestationTolerant=0` をホストが読めること、385 KB の image が EP0 だけで 376 block・2.7 秒（139 KiB/s）で転送されることを確認しました。device は `dfuMANIFEST-WAIT-RESET` を返し、boot partition を切り替えて新しい image で再起動しました。

v3 の DFU はライブラリ独自の driver で、状態遷移、65535 byte までの block、HS での動作を RAM の backend で試験しています。この例そのものは compile だけの確認で、4096 byte の block での `dfu-util` による実際の OTA はまだ記録していません（[DFU ガイド](../../docs/classes/dfu.ja.md)）。

## 注意

- **送った image がこのスケッチを置き換えます。** DFU interface を持つ build を送らないと、次の更新は boot mode 経由になります。
- ESP の application でないファイルは最初の block で DFU status 3（`errWRITE`）で拒否し、壊れた image は最後の検証で status 7（`errVERIFY`）になります。どちらも boot partition に触れないので、ファイルを間違えても害はありません。
- `dfu-util` が最後の status 読み出しでエラーを出すことがあります。device は download の検証から約 0.5 秒後に再起動するので、その間にもう一度 poll したホストには device が消えたように見えます。更新は済んでいます。メッセージではなく board を確かめてください。
- rollback は自動ではありません。壊れた image から復帰する必要がある device には、このスケッチの `markValid()` に加えて `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` が必要です。`markValid()` は真正性の確認ではありません。DFU interface に届く人なら誰でも書き換えられます。
- 通常の firmware の書き換えでは NVS が残ります。
- Arduino 内蔵の USB class（USB MSC / DFU / CDC on boot）をこのライブラリと同時に有効にしないでください。

## v2 からの変更点

- block の大きさは `build_opt.h` の `-DCFG_TUD_DFU_XFER_BUFSIZE` ではなく、実行時に `dfu.configure()`（`EspUsbDfuConfig::transferBytes`）で決めます。この例は 4096（flash の 1 sector。既定 1024 の 1/4 の要求数）にしました。
- `onError()` の引数は `uint8_t` から `EspUsbDfuStatus` になりました。`unsigned(status)` で表示すれば以前と同じ番号です。
- `onProgress()` / `onComplete()` / `onError()` / `restartWhenComplete()` は `esp_err_t` を返し、停止中だけ成功します。
- 開始の失敗時に `dfu.startError()` も表示し、準備完了の行で宣言した `wTransferSize` を表示します。

## 関連

- [FirmwareBootMode](../FirmwareBootMode/) — DFU Runtime / ROM loader
- [FirmwareMSC](../FirmwareMSC/)、[FirmwareHTTP](../FirmwareHTTP/)、[FirmwareCDC](../FirmwareCDC/)、[FirmwareVendor](../FirmwareVendor/) — 他の更新経路
- [USB 経由のファームウェア更新](../../docs/ota-over-usb.ja.md)
- [DFU API ガイド](../../docs/classes/dfu.ja.md)
- [スケッチ](FirmwareDFU.ino) / [全サンプル](../README.md) / [サンプルの進め方](../../docs/example-workflow.ja.md)
