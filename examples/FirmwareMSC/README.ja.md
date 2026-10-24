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

## 読み方と確認の手順

### 実行前の準備

仮想firmware volumeへコピーしたapplication imageを受け付ける。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`disk.begin()`は更新partitionに対応するmetadataを準備し、RAM配列へimage全体を載せるわけではありません。記載のapplication .binを順にコピーし、完了・取り出しの手順に従います。コピー画面の終了・cache反映・image検証・boot先選択は別の段階です。

### APIの使い方と改造時の注意

選んだtargetとpartition構成のapplication imageを使い、結合Flash dumpやbootloader binaryと混同しないようにします。利用可否・容量を確認してから送信します。転送の応答、受信byte数、image検証、boot先選択、次の起動成功は別段階です。ROM loaderの例はboot modeの変更であり、同じアプリ内更新ではありません。

試験前に記載の復旧・書き込み経路を確保します。通常のfirmware更新でNVSが残る場合があるので、新しいアプリが初期設定状態とは仮定しません。`markValid()`はアプリの検証状態に関わるもので、任意の受信imageの真正性を証明するAPIではありません。実製品では認証・image検証の方針も必要です。Flash・再起動は例の実行経路に置き、新たな長時間USB callbackへ移さないようにします。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](FirmwareMSC.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
