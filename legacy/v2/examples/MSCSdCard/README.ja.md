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

## 読み方と確認の手順

### 実行前の準備

SPIのSDカードをUSBホストへblockストレージとして公開する。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`sdMsc.begin()`の前にSDモジュールの電源・SPI経路・SD_CS_PINを確認します。カード初期化、mscへのattach、USB開始の順です。PCが媒体を使う間はスケッチからfilesystemを書き換えず、学習用の媒体を使い、取り出し操作後に外します。

### APIの使い方と改造時の注意

blockはLBAと媒体のblock長で扱い、ファイルにはその上のfilesystemが必要です。容量取得、mount、ファイル操作の成否を分けます。返却byte数を確認し、handleを閉じ、ホストの未完了書き込みを終えてから取り外し・再利用します。USB接続やFAT署名だけではfilesystemの正常を保証しません。

同じ媒体の書き手は一つにします。ホストがmountしたvolumeを、スケッチも独立に書き換えないようにします。RAM例はresetで変化が失われますが、実媒体の例は永続データを変えます。残したいファイルがある媒体を使う前に、どの操作を行う例かを確認します。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](MSCSdCard.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
