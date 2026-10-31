# EspUsbDevice MSC

> English: [README.md](README.md)

USB Mass Storage Class（MSC）デバイスを作り、RAM 上の小さな block device を USB ホストへ公開する例です。

SCSI / READ(10) / WRITE(10) の立ち上げ確認用です。`EspUsbDeviceMscRamDisk` が read/write callback と block 範囲の確認を受け持つので、MSC の callback を全部手で書く必要はありません。
`msc.onRead()` / `onWrite()` を直接書く例は [MscCdcBlocks](../MscCdcBlocks/MscCdcBlocks.ino) を参照してください。

## ハードウェア

- ESP32-S3 / ESP32-S2 / ESP32-P4 の board。ホストへは **native USB OTG** の口でつなぐ
- USB ホスト（PC、EspUsbHost を動かす別の ESP32、試験治具など）
- ログ用の別の Serial 接続（USB-UART や board の debug 口）

## 動作内容

- `EspUsbDevice` を MSC デバイスとして開始する（S2/S3 は FS、P4 は HS）
- 16 block × 512 byte の RAM disk を公開する。中身は 0 で、block 0 の末尾だけ `55 AA`
- SCSI INQUIRY の vendor / product / revision 文字列を設定する
- `INQUIRY`、`TEST UNIT READY`、`READ CAPACITY(10)`、`READ(10)`、`WRITE(10)` など（一覧は [MSC ガイド](../../docs/classes/msc.ja.md)）に応答する
- `START STOP UNIT`（取り出し）を Serial に表示する

## 使い方

1. スケッチを書き込み（`arduino-cli compile --profile esp32s3` など）、Serial monitor を 115200 で開く。
2. native USB の口をホストへつなぐ。`USB MSC RAM disk ready` が出る。
3. PC では 8 KiB の未フォーマットのリムーバブルドライブに見える。Linux なら `lsblk` で `sdX` を探し、`sudo dd if=/dev/sdX bs=512 count=1 | xxd | tail -2` で読むと、最後の 2 byte が `55 aa`。
4. ドライブを取り出す（`eject /dev/sdX`、ファイルマネージャーの「取り出し」）と `MSC_START_STOP ...` が出る。

## 重要な制限と方向性

この RAM disk はフォーマットされていません。PC につなぐと、OS が初期化やフォーマットを求めることがあります。中身の問題であり、USB 接続の失敗ではありません。このスケッチは MSC の転送を確かめるもので、ファイル単位の読み書きの例ではありません。内容は reset で消えます。

MSC は block device と filesystem を分けて扱います。ホストが mount できる storage にするには次のどちらかを使います。

- RAM に正しい FAT image を置く — [MSCFatRamDisk](../MSCFatRamDisk/)
- `onRead()` / `onWrite()` の先を SD カードなどの block storage にする — [MSCSdCard](../MSCSdCard/)

内蔵 flash、SPIFFS、LittleFS を MSC として直接公開しないでください。MSC はホストに生の sector を渡しますが、SPIFFS や LittleFS は ESP32 が mount する filesystem です。内蔵 flash には firmware の partition、消去単位、書込み寿命の問題もあります。
例外として意図的に用意しているのが [FirmwareMSC](../FirmwareMSC/) で、data 領域が OTA partition そのものになっており、firmware image だけを受け付けます。

どの媒体でも、所有者は一つです。ホストが使っている間、スケッチは同じ filesystem を mount したり書いたりしてはいけません。取り出し（`START STOP UNIT`）の後でスケッチ側へ戻します。

## RAM disk の実用的な使い道

RAM disk は試験専用ではありません。FAT の補助クラスと組み合わせると、ホストから機器へファイルを渡す小さな一時ドライブになります。

- 設定ファイルを置いてもらい、ESP32 側で読む
- PC から受け取ったファイルを Wi-Fi で転送する
- ログや診断データをホストへ見せる

FAT image を解析するのは、ホストが同期・取り出し・停止した後にしてください。書込み中に読んではいけません。

| 補助クラス | 用途 |
|---|---|
| `EspUsbDeviceMscRamDisk` | 生の block I/O、試験、一時 buffer（この例） |
| `EspUsbDeviceMscFatRamDisk` | ファイル受け渡し用の小さな RAM 上の FAT image |
| `EspUsbDeviceMscSdCard` | SPI 接続の SD カードを MSC の block device にする |
| `EspUsbDeviceMscFirmwareDisk` | ドラッグ＆ドロップでの firmware 更新 |

## 主な API

- `EspUsbDeviceMsc msc(device)` — MSC Function を登録する。
- `msc.configure(EspUsbMscConfig)` — Function 名と DMA の staging 容量（`transferBytes`、既定 512）を設定する。停止中に呼び、`esp_err_t` を返す。
- `msc.vendorID()`、`productID()`、`productRevision()` — INQUIRY の文字列（8 / 16 / 4 文字まで）。
- `msc.mediaPresent(true)` — 媒体を「挿入済み」にする。RAM disk の補助クラスは**これを呼ばない**ので自分で呼ぶ。
- `msc.isWritable(true)` — ホストからの書込みを許す。`false` で書込み保護。
- `msc.onStartStop(callback)` — 取り出し・開始・停止の要求を受ける。USB worker 上で呼ばれるので、すぐ戻る。
- `EspUsbDeviceMscRamDisk disk(storage, blocks, blockSize)` — 外部の buffer（内部 RAM でも PSRAM でもよい）を block device にする。
- `disk.attach(msc)` — read/write callback を登録し、`msc.begin(blocks, blockSize)` を呼ぶ。これは容量の設定だけで、staging buffer は `device.begin()` が確保する。
- `disk.clear()`、`readBlock()`、`writeBlock()`、`writeByte()` — 試験用の初期化に便利。
- `device.begin()` が失敗したら、`device.lastErrorName()` と `msc.startError()` で理由が分かる。

## Serial の出力例

```text
USB MSC RAM disk ready blocks=16 block_size=512 bytes=8192 staging=512
MSC_START_STOP pc=0 start=0 eject=1
```

2 行目はホストや OS によって出ないことがあります。

## 確認の手順

- 最初は例を変更せずに動かし、ログを上の手順と照らし合わせます。filesystem の mount を期待するのではなく、列挙・容量・生の読み出しのどこまで成功したかを見ます。
- block は LBA と媒体の block 長で扱い、ファイルにはその上の filesystem が必要です。USB の接続や boot 署名だけでは、filesystem が正しいとは言えません。
- HS（ESP32-P4）では `transferBytes` を 512 の倍数にします。そうでないと `device.begin()` が `ESP_ERR_INVALID_SIZE` で失敗します。
- この例のエラー表示は説明用で、再試行や回復の方針ではありません。

## v2 からの変更点

- staging buffer は `CFG_TUD_MSC_EP_BUFSIZE` ではなく、Function ごとの `msc.configure()`（`transferBytes`）で指定します。旧 macro はコンパイルエラーになります。
- `onStartStop()`、`vendorID()` などの setter は `esp_err_t` を返し、停止中だけ成功します。
- 開始の失敗時に `device.lastErrorName()` に加えて `msc.startError()` も表示します。準備完了の行には確保した staging 容量（`msc.transferCapacity()`）も出します。
- 媒体を挿し直す（`mediaPresent(false)` → `true`）と、ホストへ UNIT ATTENTION（06/28/00）を 1 回返すようになりました。
- それ以外の動作は v2 と同じです（VID/PID 303a:4007、同じ disk）。

## 関連

- [MSCFatRamDisk](../MSCFatRamDisk/) — RAM の FAT image でファイルを受け渡す
- [MSCSdCard](../MSCSdCard/) — SD カードを USB storage にする
- [MscCdcBlocks](../MscCdcBlocks/MscCdcBlocks.ino) — callback を手で書き、CDC と併用する
- [MSC API ガイド](../../docs/classes/msc.ja.md)
- [スケッチ](MSC.ino) / [全サンプル](../README.md) / [サンプルの進め方](../../docs/example-workflow.ja.md)
