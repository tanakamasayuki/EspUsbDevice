# EspUsbDevice MSC FAT RAM Disk

> English: [README.md](README.md)

RAM 上に小さな FAT12 image を作り、USB Mass Storage Class デバイスとしてホストへ公開する例です。

[MSC](../MSC/) の例は生の block I/O を確かめるものでした。この例はその上の段階で、ホストがドライブへファイルをコピーして取り出した後、ESP32 側でそのファイルを読みます。

## ハードウェア

- ESP32-S3 / ESP32-S2 / ESP32-P4 の board。**native USB OTG** の口でつなぐ
- USB ホスト（PC など）
- ログ用の別の Serial 接続

## 動作内容

- 64 KiB の RAM buffer を、ラベル `ESPUSB` の FAT12 volume としてフォーマットする（ESP32-S2 の内部 DRAM に収まる大きさ）
- 最初のファイルとして `README.TXT` を置く
- 書込み可能な MSC デバイスとして公開し、ホストが `CONFIG.TXT` をコピーできるようにする
- ホストが取り出し・停止した後、ESP32 側で `CONFIG.TXT` を読み、サイズと内容（先頭 255 byte まで）を表示する

## 使い方

1. スケッチを書き込み、Serial monitor を 115200 で開く。
2. native USB の口を PC へつなぐ。
3. `ESPUSB` ドライブが現れたら、ルートに `CONFIG.TXT` という名前のファイルをコピーする。
4. OS からドライブを取り出す・unmount する（ファイルマネージャーの「取り出し」、Linux なら `eject /dev/sdX` や `udisksctl unmount -b /dev/sdX1`）。
5. Serial monitor に `MSC_EJECT`、`CONFIG_SIZE`、ファイルの内容が出る。

## 主な API

- `EspUsbDeviceMscFatRamDisk disk(storage, sizeof(storage))` — RAM buffer を FAT disk にする。
- `disk.format("ESPUSB")` — FAT12 image を作る（buffer の中身は消える）。buffer の大きさは問わない。
  volume が 4085 cluster 未満（FAT12 の上限）に収まる最小の cluster 長を 1〜128 sector から選ぶ。
  65536 sector 以上の volume は大きさを `BPB_TotSec32` に記録する。false になるのは、16 sector 未満のときと、128 sector の cluster でも FAT12 に収まらないときだけ。
- `disk.addTextFile("README.TXT", text)` / `addFile()` — ホストに見えるファイルを置く。
- `disk.attach(msc)` — read/write と `START STOP UNIT` の callback、容量を設定する。媒体の挿入は設定しないので、`device.begin()` の前に `msc.mediaPresent(true)` を呼ぶ。
- `disk.onEject(callback)` — ホストの取り出し・停止の後に USB worker 上で呼ばれる。この例では flag を立てるだけ。
- `disk.exists()`、`fileSize()`、`readFile()` — 取り出しの後、`loop()` からホストが書いたファイルを読む。`readFile()` は volume の外へ出る cluster の連鎖を辿らずに止まる。
- `msc.configure(EspUsbMscConfig)` — Function 名と 512 byte の staging buffer（FS/HS どちらでも有効）を設定する。

## 制限

- sector は 512 byte 固定。常に FAT12 で、ルートディレクトリは 64 entry
- ファイル名は 8.3 形式のみ。長いファイル名には対応しない
- ルートディレクトリの通常ファイルのみ
- ホストが書いている可能性がある間、ESP32 側で FAT image を読まない。取り出しを待つ
- RAM の内容は電源断や reset で消える
- buffer は内部 DRAM に収まる大きさにしています。image は `memcpy` するだけで DMA には使わないので、大きくしたいときは PSRAM に置けます。永続的な媒体やずっと大きな容量には SD カード（[MSCSdCard](../MSCSdCard/)）を、firmware にはファイルを保持せず flash へ直接書く [FirmwareMSC](../FirmwareMSC/) を使ってください

firmware 更新や Wi-Fi 転送に使う場合も、ファイルの走査と読み出しはホストが取り出し・unmount した後に行います。

## Serial の出力例

```text
USB FAT RAM disk ready blocks=128 bytes=65536
MSC_EJECT
CONFIG_SIZE 12
CONFIG_BEGIN
hello=world
CONFIG_END
```

## 確認の手順

- `format()`、`addTextFile()`、`attach()` で `device.begin()` の前に disk を準備します。ファイルをコピーし、ホストの書き出しを待って取り出し、`onEject()` の flag から `loop()` の `readFile()` へ進む流れを追ってください。
- 表示は先頭の一部だけです。任意の大きさのファイルが表示用 buffer に収まるとは仮定しないでください。
- unmount だけでは `START STOP UNIT` を送らないホストもあります。`MSC_EJECT` が出なければ、ファイルマネージャーの「取り出し」（または `eject`）を使ってください。
- 書き手は一度に一つ：mount 中はホスト、取り出し後はスケッチです。

## v2 からの変更点

- Function 名と staging buffer（`transferBytes`）を、ビルド時の `CFG_TUD_MSC_EP_BUFSIZE` ではなく `msc.configure()` で指定します。
- 取り出しの callback は flag を立てるだけにし、`MSC_EJECT` は USB worker ではなく `loop()` から表示します。
- 開始の失敗時に `msc.startError()` も表示します。
- RAM image を PSRAM に置けることを README に書きました（v3 ではこの領域を DMA に使いません）。
- VID/PID（303a:4008）、volume、ファイルは v2 と同じです。

## 関連

- [MSC](../MSC/) — 最小の生 block I/O の例
- [MSCSdCard](../MSCSdCard/) — SD カードの永続 storage
- [MSC API ガイド](../../docs/classes/msc.ja.md)
- [スケッチ](MSCFatRamDisk.ino) / [全サンプル](../README.md) / [サンプルの進め方](../../docs/example-workflow.ja.md)
