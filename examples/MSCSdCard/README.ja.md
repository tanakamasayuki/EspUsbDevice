# EspUsbDevice MSC SD Card

> English: [README.md](README.md)

SPI 接続の SD card を USB Mass Storage Class device として Host に公開する例です。

SD は元から 512 byte sector の block device なので、MSC の永続ストレージ example として
RAM disk より実用的です。この example は Arduino-ESP32 の `SD.readRAW()` /
`SD.writeRAW()` を使い、Host からの `READ(10)` / `WRITE(10)` を SD の sector I/O に
接続します。

## ハードウェア

- USB device 対応の ESP32-S3 など Arduino-ESP32 board
- SPI 接続の SD card socket
- PC などの USB host
- ログ確認用の Serial monitor 接続

## 使い方

1. board に合わせて `SD_CS_PIN` を変更します。
2. SD card を挿入します。
3. sketch を書き込み、Serial monitor を開きます。
4. `USB SD MSC ready` を確認します。
5. USB device port を PC に接続します。
6. PC 側から SD card を読み書きします。
7. 取り外す前に OS の eject / unmount を実行します。

## 重要な注意

- Host が MSC として SD card を所有している間、ESP32 側で `SD.open()` などの file API を使わないでください。
- この example は `SD.begin()` により Arduino 側の filesystem も mount しますが、MSC 動作中は file API を使わない前提です。
- eject / unmount 後に ESP32 側で SD filesystem を読む設計にしてください。
- Host と ESP32 が同じ FAT を同時に書くと破損しやすいです。
- SD card の内容は Host から直接変更されます。必要なら事前に backup してください。

## 主要 API

- `EspUsbDeviceMscSdCard sdMsc(SD)` は Arduino `SD` instance を MSC block device として扱います。
- `sdMsc.begin(cs, SPI, frequency)` は SD を初期化し、sector count / sector size を取得します。
- `sdMsc.attach(msc)` は MSC read/write callback を設定します。
- `sdMsc.onEject(callback)` は Host の eject / stop 後の処理を登録します。
- `sdMsc.readOnly(true)` を使うと Host からの write を拒否できます。

## 関連

- [MSC](../MSC/) - raw block I/O の最小 MSC example
- [MSCFatRamDisk](../MSCFatRamDisk/) - RAM 上の FAT image でファイルを受け渡す example
