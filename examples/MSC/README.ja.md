# EspUsbDevice MSC

> English: [README.md](README.md)

USB Mass Storage Class device を作り、RAM 上の小さな block device を USB host へ公開する例です。

この example は SCSI / READ(10) / WRITE(10) の疎通確認用です。`EspUsbDeviceMscRamDisk`
が read/write callback と block 範囲チェックを受け持つため、最小構成ではユーザーが
MSC callback をすべて書く必要はありません。

## ハードウェア

- USB device 対応の ESP32-S3 など Arduino-ESP32 board
- PC、EspUsbHost を動かす別 ESP32、またはテスト治具などの USB host
- ログ確認用の Serial monitor 接続

## 動作内容

- `EspUsbDevice` を full-speed MSC device として起動します。
- 16 block x 512 bytes の RAM disk を公開します。
- `INQUIRY` の vendor / product / revision を設定します。
- `TEST UNIT READY`、`READ CAPACITY(10)`、`READ(10)`、`WRITE(10)` を処理します。
- `START STOP UNIT` を受け取ったときに Serial へログを出します。

## 重要な制限と方針

この example の RAM disk は FAT でフォーマットされていません。PC に接続すると OS が
初期化やフォーマットを求める場合があります。これは MSC transport の疎通確認用であり、
通常の USB メモリとしてファイルを読み書きする example ではありません。

MSC は block device と filesystem が分かれます。OS からマウントできるストレージに
するには、次のどちらかが必要です。

- RAM buffer に有効な FAT image を用意する。
- SD card などの block storage を `onRead()` / `onWrite()` に接続する。

このライブラリ側では、まず block device helper を用意して MSC の準備負担を下げます。
FAT image を組み立てる helper は別段階で追加する候補です。

内蔵 flash、SPIFFS、LittleFS を直接 USB MSC として公開する方針は避けます。USB MSC は
sector 単位の block device を Host に見せる仕組みですが、SPIFFS / LittleFS は ESP32
側の filesystem API であり、抽象度が合いません。また内蔵 flash は firmware 領域や
erase block、書き換え耐性との兼ね合いが大きく、ユーザー向け example として安全に
見せにくいためです。

実用的な永続ストレージ example は SD card を優先します。SD は元から block device で、
FAT と USB MSC の相性もよいです。ただし Host が SD を MSC として使っている間は、
ESP32 側から同じ filesystem を同時に mount / 書き込みしないでください。eject や
`START STOP UNIT` を受けた後に ESP32 側へ制御を戻す排他設計が必要です。

## RAM disk の実用用途

RAM disk はテスト専用ではありません。FAT image helper を追加すると、Host から一時的に
ファイルを置ける小さな USB drive として使えます。

- PC から firmware image を置き、eject 後に ESP32 側で firmware update に使う。
- PC から設定ファイルを置き、ESP32 側で読み取って反映する。
- PC から受け取ったファイルを Wi-Fi 経由で別サーバーへ転送する。
- ESP32 側で生成したログや診断データを RAM 上の FAT image に置き、Host へ渡す。

安全に扱うため、Host が書き込み中の FAT image を ESP32 側が同時に読む設計にはしません。
ファイル一覧や内容の解析は、Host 側の sync / eject / stop を確認した後に行う方針です。

今後の helper は次の分担を想定します。

- `EspUsbDeviceMscRamDisk`: raw block I/O、テスト、一時 buffer。
- `EspUsbDeviceMscFatRamDisk`: RAM 上に小さい FAT image を作り、Host とファイル受け渡しする。
- `EspUsbDeviceMscSdCard`: SD card を USB MSC block device として公開する。
- flash / SPIFFS / LittleFS 直接公開: 標準 example では扱わない。

## 主要 API

- `EspUsbDeviceMsc msc(device)` は MSC function を登録します。
- `msc.vendorID()`、`productID()`、`productRevision()` は SCSI inquiry 文字列を設定します。
- `msc.mediaPresent(true)` は media が挿入済みであることを Host に返します。
- `msc.isWritable(true)` は Host からの write を許可します。
- `msc.onStartStop(callback)` は eject / start / stop 要求を受け取ります。
- `EspUsbDeviceMscRamDisk disk(storage, blocks, blockSize)` は外部 buffer を block device として扱います。
- `disk.attach(msc)` は MSC read/write callback を設定し、`msc.begin(blocks, blockSize)` を呼びます。
- `disk.clear()`、`readBlock()`、`writeBlock()`、`writeByte()` は example やテスト用の小さな初期化に使えます。

## 想定 Serial 出力

```text
USB MSC RAM disk ready blocks=16 block_size=512 bytes=8192
MSC_START_STOP pc=0 start=0 eject=1
```

Host や OS によっては `START STOP UNIT` が送られないため、2 行目は出ない場合があります。

## 関連

- [Keyboard](../Keyboard/) - HID keyboard device
- [Mouse](../Mouse/) - HID mouse device
- [KeyboardMouse](../KeyboardMouse/) - keyboard と mouse の composite device
