# EspUsbDevice MSC FAT RAM Disk

> English: [README.md](README.md)

RAM 上に小さな FAT12 image を作り、USB Mass Storage Class device として Host に公開する例です。

`MSC` example は raw block I/O の疎通確認用です。この example は一段上の使い方として、
PC からファイルを置き、eject 後に ESP32 側でそのファイルを読む流れを示します。

## ハードウェア

- USB device 対応の ESP32-S3 など Arduino-ESP32 board
- PC などの USB host
- ログ確認用の Serial monitor 接続

## 動作内容

- S2の内部DRAMにも収まる64 KBのRAM bufferをFAT12 diskとして初期化します。
- `README.TXT` を初期ファイルとして配置します。
- Host から `CONFIG.TXT` を書き込める writable MSC device として公開します。
- Host 側で eject / stop した後、ESP32 側で `CONFIG.TXT` を探して Serial に出力します。

## 使い方

1. sketch を書き込み、Serial monitor を開きます。
2. USB device port を PC に接続します。
3. PC 側で `ESPUSB` drive が見えたら、root に `CONFIG.TXT` をコピーします。
4. OS の eject / unmount を実行します。
5. Serial monitor に `CONFIG_SIZE` とファイル内容が出ます。

## 主要 API

- `EspUsbDeviceMscFatRamDisk disk(storage, sizeof(storage))` は RAM buffer を FAT disk として扱います。
- `disk.format("ESPUSB")` は最小 FAT12 image を生成します。
- `disk.addTextFile("README.TXT", text)` は Host に最初から見えるファイルを追加します。
- `disk.attach(msc)` は MSC read/write callback を設定します。
- `disk.onEject(callback)` は Host の eject / stop 後の処理を登録します。
- `disk.exists()`、`fileSize()`、`readFile()` は eject 後に Host が置いたファイルを読むために使います。

## 制限

- 512 byte sector 固定です。
- FAT12 の小容量 RAM disk です。
- long file name は扱わず、8.3 filename を使います。
- root directory 直下の通常 file のみを想定します。
- Host が書き込み中の FAT image を ESP32 側で同時に読まないでください。
- RAM disk なので電源を切ると内容は消えます。
- example は通常 DRAM に収まる小さな容量にしています。大きい firmware を扱う場合は
  PSRAM や SD card、streaming update を検討してください。

firmware update や Wi-Fi 転送に使う場合も、まず Host 側で eject / unmount してから
ESP32 側で file scan / read を行う設計にしてください。

## 関連

- [MSC](../MSC/) - raw block I/O の最小 MSC example

## 読み方と確認の手順

### 実行前の準備

FAT RAM diskを取り出した後、ホストが書いたファイルを読む。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`begin()`前にformat()・`addTextFile()`・`attach()`で準備します。PCからCONFIG.TXTをコピーして書き込みを終え、取り出した後にonEject()からloop()のreadFile()へ進む流れを確認します。表示は長さを制限しており、任意の大きさのファイル全体を読むわけではありません。

### APIの使い方と改造時の注意

blockはLBAと媒体のblock長で扱い、ファイルにはその上のfilesystemが必要です。容量取得、mount、ファイル操作の成否を分けます。返却byte数を確認し、handleを閉じ、ホストの未完了書き込みを終えてから取り外し・再利用します。USB接続やFAT署名だけではfilesystemの正常を保証しません。

同じ媒体の書き手は一つにします。ホストがmountしたvolumeを、スケッチも独立に書き換えないようにします。RAM例はresetで変化が失われますが、実媒体の例は永続データを変えます。残したいファイルがある媒体を使う前に、どの操作を行う例かを確認します。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](MSCFatRamDisk.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
