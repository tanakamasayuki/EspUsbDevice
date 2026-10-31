# EspUsbDevice MSC SD Card

> English: [README.md](README.md)

SPI で接続した SD カードを USB Mass Storage Class デバイスとして公開する例です。

SD カードは最初から 512 byte sector の block device なので、永続 storage の MSC 例として実用的です。Arduino-ESP32 の `SD.readRAW()` / `SD.writeRAW()` を（`EspUsbDeviceMscSdCard` 経由で）使い、ホストの `READ(10)` / `WRITE(10)` を SD の sector I/O へつなぎます。

## ハードウェア

- ESP32-S3 / ESP32-S2 / ESP32-P4 の board。**native USB OTG** の口でつなぐ
- SPI 接続の SD カードソケット（module でも board 上でもよい）と、消えても困らないカード
- USB ホスト（PC など）
- ログ用の別の Serial 接続

## 配線

board の variant が決める既定の SPI ピンを使います。スケッチ先頭の `SD_SCK_PIN`、`SD_MISO_PIN`、`SD_MOSI_PIN`、`SD_CS_PIN` を board に合わせて変えてください。使うピンは起動時に表示します。

| SD ソケット | ESP32 側 | ESP32-S3 既定 | ESP32-S2 既定 | ESP32-P4 既定 |
|---|---|---|---|---|
| CS（DAT3） | `SD_CS_PIN`（`SS`） | GPIO10 | GPIO34 | GPIO26 |
| SCK（CLK） | `SD_SCK_PIN`（`SCK`） | GPIO12 | GPIO36 | GPIO36 |
| MOSI（CMD / DI） | `SD_MOSI_PIN`（`MOSI`） | GPIO11 | GPIO35 | GPIO32 |
| MISO（DAT0 / DO） | `SD_MISO_PIN`（`MISO`） | GPIO13 | GPIO37 | GPIO33 |
| VCC | 3.3 V | | | |
| GND | GND | | | |

- カードには 3.3 V を供給します。5 V 用の module は、regulator とレベル変換を持つものに限ります。
- ソケット単体なら CS、MOSI、MISO に 3.3 V への pull-up（10 kΩ）が要ります。多くの module には付いています。
- 配線は短くします。`SD_SPI_HZ` は 4 MHz です。配線が長くて `SD_BEGIN_FAILED` になるなら下げ、安定したら（例：20 MHz へ）上げてください。
- SDMMC（4 bit）で配線されたスロット（ESP32-P4 の board に多い）は、SPI mode で使うか別の SPI ソケットを使ってください。この補助クラスが扱うのは `SD` で、`SD_MMC` ではありません。

## 使い方

1. ソケットを配線し、必要ならピンの定数を変える。
2. FAT16/FAT32 でフォーマットした SD カードを挿す。`SD.begin()` が mount するので、mount できないカードでは `SD_BEGIN_FAILED` で止まる。
3. スケッチを書き込み、Serial monitor を 115200 で開く。
4. `USB SD MSC ready` を待つ。
5. native USB の口を PC へつなぐ。
6. 普通の USB カードリーダーと同じように PC から読み書きする。
7. 抜く前に OS から取り出す・unmount する。スケッチは `SD_EJECT` を表示し、媒体を「抜去」にする。
8. Serial monitor から `r` を送ると、カードをホストへ戻す（`SD_REINSERTED`）。ホストは媒体の交換を検出して再び mount する。

## 重要な注意

- ホストが MSC でカードを使っている間、`SD.open()` などの ESP32 側のファイル API を使わないでください。
- `sdMsc.begin()` は `SD.begin()` を呼ぶので、Arduino 側の filesystem も mount されます。このスケッチは MSC の使用中、意図してファイル API を使いません。
- ESP32 側で SD の filesystem を読むのは、ホストが取り出し・unmount した後だけにします。その前に mount し直し（`SD.end()` → `sdMsc.begin(...)`）、ホストが書く前の FAT の cache を使わないようにしてください。
- ホストと ESP32 が同時に書くと FAT が壊れます。
- ホストの書込みはカードを直接変更します。必要ならバックアップしてください。

## 主な API

- `EspUsbDeviceMscSdCard sdMsc(SD)` — Arduino の `SD` instance を MSC の block device にする。
- `sdMsc.begin(cs, SPI, frequency, mountpoint, maxFiles)` — カードを初期化し、sector 数と sector 長を読む（512 byte sector のみ）。
- `sdMsc.attach(msc)` — read/write と `START STOP UNIT` の callback、容量を設定する。どれかの登録が拒否されたら（device の稼働中など）false を返す。その後 `msc.mediaPresent(true)` を呼ぶ。
- `sdMsc.onEject(callback)` — ホストの取り出し・停止の後に USB worker 上で呼ばれる。
- `sdMsc.readOnly(true)` — カードを書込み保護（Write Protect）の媒体としてホストへ見せる。MODE SENSE が WP bit を返すので、ホストは読み取り専用で mount する。それでも届いた書込みは DATA PROTECT（sense 07/27/00）で失敗する。`msc.isWritable(false)` は `attach()` の時点で（attach 済みならすぐに）自分で設定するので、別に呼ぶ必要はない。
- `msc.mediaPresent(false)` / `true` — USB を動かしたまま媒体を抜き差しする。挿し直すとホストへ UNIT ATTENTION（媒体交換）を 1 回返す。
- `msc.configure(EspUsbMscConfig)` で `transferBytes = 4096` — DMA の staging を大きくすると、USB の往復が減る。512 の倍数なら FS でも HS でも使える。

## Serial の出力例

```text
SD pins SCK=12 MISO=13 MOSI=11 CS=10
USB SD MSC ready blocks=31116288 block_size=512
SD_EJECT - send 'r' on Serial to give the card back to the host
SD_REINSERTED
```

## 確認の手順

- `sdMsc.begin()` の前に、module の電源、SPI の経路、CS ピンを確かめます。`SD_BEGIN_FAILED` はカードが応答しなかったという意味で、その時点で USB はまだ開始していません。
- 容量（`blocks=`）、PC での mount、ファイル操作の成否を分けて確かめます。
- 慣れるまでは消えてもよい媒体を使い、ケーブルやカードを抜く前に取り出してください。

## v2 からの変更点

- SPI のピンを明示的な定数にし、起動時に表示します。README に chip ごとの配線表を付けました（v2 は既定の bus と `SD_CS_PIN` だけでした）。
- staging buffer をビルド時の `CFG_TUD_MSC_EP_BUFSIZE` ではなく `msc.configure()` で 4 KiB にしました（FS/HS どちらでも有効）。
- 取り出しの後、媒体を「抜去」にし（`msc.mediaPresent(false)`）、`r` でホストへ戻せるようにしました。v2 は `SD_EJECT` を表示するだけでした。
- 取り出しの callback は flag を立てるだけにし、表示は `loop()` で行います。
- 開始の失敗時に `msc.startError()` も表示します。
- `sdMsc.readOnly(true)` は、カードが書込み保護であることをホストへ伝えるようになりました。v2 は書込みを1つずつ失敗させるだけで、ホストには書込みが失敗する書込み可能なカードに見えていました。
- `sdMsc.attach()` は、callback を登録できなかったときに false を返すようになりました。
- VID/PID（303a:4009）は同じです。

## 関連

- [MSC](../MSC/) — 最小の生 block I/O の例
- [MSCFatRamDisk](../MSCFatRamDisk/) — RAM の FAT image でファイルを受け渡す
- [MSC API ガイド](../../docs/classes/msc.ja.md)
- [スケッチ](MSCSdCard.ino) / [全サンプル](../README.md) / [サンプルの進め方](../../docs/example-workflow.ja.md)
