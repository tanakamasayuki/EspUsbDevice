# EspUsbDevice FirmwareMSC

> English: [README.md](README.md)

**ドライブにファイルを放り込む**ファームウェア更新です。ボードが小さな USB ディスクとして
現れ、そこへファームウェアの `.bin` をコピーすると、デバイスが空いている OTA partition へ
書き込み、検証し、そのイメージで再起動します。host 側に入れるものはありません。
ファイルマネージャがそのまま更新ツールになります。

## ハードウェア

- USB device 対応の ESP32-S2 / ESP32-S3 / ESP32-P4 board
- USB host となる PC
- ログ確認用の Serial 接続

## 前提

**application partition が 2 つある** partition scheme が必要です。Arduino の
「Default」にはありますが「Huge APP」にはありません。1 つしか無い場合
`disk.begin()` は false を返します。

## 動作内容

- データ領域が OTA partition そのものである FAT12 ボリュームを提供
- 何を置けばよいかを書いた `README.TXT` を表示
- ESP application イメージを magic byte `0xE9` で検出し、到着した順に flash へ流し込む
- ファイルの directory entry が示す長さに達した時点、または eject 時に commit
- boot partition を切り替える前に検証し、再起動

## 使い方

1. スケッチを書き込み、Serial monitor を開きます。
2. USB device port を PC に接続すると、ドライブが現れます。
3. `.bin` をコピーします。進捗が 1 KiB ごとに表示されます。
4. ボードが新しいファームウェアで再起動します。何も起きない場合はドライブを
   eject してください。それが 2 つ目の commit 点です。

`.bin` は Arduino の素のビルド（スケッチ → コンパイル済みバイナリをエクスポート、
または `arduino-cli compile` が残す `build/`）です。

## 主な API

- `EspUsbDeviceMscFirmwareDisk disk(storage, size)` — `storage` が抱えるのは
  ボリュームのメタデータとスクラッチ領域だけで、イメージは載りません。16 KB が
  快適、8 KB が下限です。
- `disk.begin(label)` — このファームウェアが更新する OTA partition に合わせて
  ボリュームを構成します。FAT12 に収まる cluster size を自動で選びます。
- `disk.addTextFile(name, text)` — 何もコピーされていない状態で見えるファイルを置きます。
- `disk.attach(msc)` — `EspUsbDeviceMsc` に接続します。
- `disk.onProgress()` / `onComplete()` / `onError()` / `onEject()` — hook。
  いずれも usbd task 上で動きます。
- `disk.restartWhenComplete(false)` — 成功しても再起動せずスケッチを続けます。
- `disk.ramSectorCount()` — ボリュームのうち RAM 側がどこで終わり partition が
  始まるか。`storage` のサイズを決めるときに役立ちます。

## 注意

- **host はファイルを昇順に書く必要があります。** 空のボリュームへコピーする限り、
  主要なファイルマネージャはすべてそうします。逆戻りや穴あきの書き込みは
  `ESP_ERR_INVALID_STATE` で拒否し、更新を中止します。中途半端に書かれたイメージが
  完成品に見える状態は作りません。
- **スクラッチ領域をケチらないでください。** `System Volume Information`、
  `.fseventsd`、`.Spotlight-V100` を吸収するのがここです。埋まると host は
  firmware 領域の cluster を割り当て始めますが、そこでは ESP イメージの先頭でない
  書き込みは無視されます。
- ESP application でないファイルは flash されず黙って捨てられます。壊れたイメージは
  最後の検証で落ち、boot partition は動きません。
- firmware 領域の read は partition の実内容を返すので、コピーしたものを host が
  読み返して検証しても一致します。
- [docs/ota-over-usb.ja.md](../../docs/ota-over-usb.ja.md) の各経路の中で、これは
  UX が最も良く、host 依存の挙動が最も多い経路です。更新する人が端末を使えるなら
  [`FirmwareDFU`](../FirmwareDFU/) の方が確実です。
- このライブラリは Arduino 標準の USB device class と同時には使えません。
