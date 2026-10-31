# EspUsbDevice FirmwareMSC

> English: [README.md](README.md)

**ドライブへファイルをドラッグする**だけで firmware を更新する例です。board は小さな USB ディスクとして見え、firmware の `.bin` をコピーすると、予備の OTA partition へ書き込み、検証してから新しい firmware で再起動します。
ホストに何もインストールする必要はなく、ファイルマネージャーが更新ツールになります。

## ハードウェア

- ESP32-S2 / ESP32-S3 / ESP32-P4 の board。**native USB OTG** の口でつなぐ
- USB ホストとなる PC
- ログ用の別の Serial 接続

## 必要な条件

**application partition が 2 つある**（OTA 用の）partition scheme が必要です。`sketch.yaml` のすべての profile が使う Arduino の「Default」scheme にはあります（4 MB flash で `app0` / `app1` が各 1.25 MB）。「Huge APP」にはなく、partition が 1 つしかないと `disk.begin()` が false を返します。

試す前に復旧の経路を用意してください。ROM loader（[FirmwareBootMode](../FirmwareBootMode/)、または BOOT＋reset）なら、いつでも flash 全体を書き直せます。

## 動作内容

- data 領域が OTA partition **そのもの**である FAT12 volume を見せる
- 何を置けばよいかを説明する `README.TXT` を置く
- `0xE9` の magic byte で ESP の application image を見分け、届いた順に flash へ書く。`.uf2` ファイルも受け付ける（書込み順は自由）
- ファイルの directory entry が示す長さまで届いたとき、またはドライブが取り出されたときに確定する
- boot partition を切り替える前に検証し、再起動する
- ホストが構成を済ませたら `markValid()` で実行中の image を確定する

## 使い方

1. スケッチを書き込み、Serial monitor を 115200 で開く。
2. native USB の口を PC へつなぐ。`ESPUSB` というラベルのドライブが現れる。
3. `.bin` をコピーする。1 KiB ごとに進捗が出る。
4. board が新しい firmware で再起動する。何も起きなければドライブを取り出す（2 つ目の確定の契機）。

`.bin` は普通の Arduino の application build です。IDE の「コンパイルしたバイナリをエクスポート」、または `arduino-cli compile` の `<build-path>/<sketch>.ino.bin` を使います。
`*.merged.bin`、`*.bootloader.bin`、`*.partitions.bin` では**ありません**。

### UF2 ファイルを使う

生の `.bin` は昇順に書かれる必要があります。`.uf2` ならこの制約がなく、別の chip 向けの image は拒否され、長さも自分で持っています。
スケッチは起動時に family ID を表示します。Microsoft の [uf2conv.py](https://github.com/microsoft/uf2/blob/master/utils/uf2conv.py) で変換します。

```sh
python3 uf2conv.py --convert --family 0xc47e5767 --base 0x0 \
  --output firmware.uf2 build/FirmwareMSC.ino.bin   # 0xc47e5767 = ESP32-S3
```

`UF2 family ID` として表示された、自分の chip の値を使ってください。詳しくは [USB 経由のファームウェア更新 3.6 節](../../docs/ota-over-usb.ja.md#36-firmware-drive)を参照してください。

## 主な API

- `EspUsbDeviceMscFirmwareDisk disk(storage, size)` — `storage` は volume の metadata と scratch 領域で、image を入れる場所ではない。16 KiB あれば余裕があり、最小は 8 KiB。
- `disk.begin(label)` — この firmware が更新する OTA partition に合わせて volume を配置する。FAT12 に収まる cluster 長を選ぶ。
- `disk.addTextFile(name, text)` — 何もコピーされる前からドライブに見えるファイルを置く（`begin()` と `attach()` の間に呼ぶ）。
- `disk.attach(msc)` — `EspUsbDeviceMsc` へつなぐ。RAM disk と違い、媒体の挿入と書込み可も設定する。
- `disk.onProgress()` / `onComplete()` / `onError()` / `onEject()` — すべて USB worker 上で呼ばれるので短く保つ。`onComplete()` が `false` を返すと image を拒否し、boot partition を元に戻す。
- `disk.restartWhenComplete(false)` — 更新が成功しても再起動せず、スケッチを動かし続ける。
- `disk.ramSectorCount()` — volume のうち RAM にある部分の終わり（＝partition の始まり）。`storage` の大きさを決めるのに使う。
- `EspUsbDeviceMscFirmwareDisk::uf2FamilyId()` — この build が受け付ける UF2 の family。
- `msc.configure()` で `transferBytes = 4096`（512 の倍数なので FS/HS どちらでも有効）。
- `EspUsbDeviceFirmwareUpdate::targetLabel()` / `capacity()` / `markValid()`。

## Serial の出力例

```text
Update target: app1 (1310720 bytes), volume <n> sectors, <m> of them in RAM
UF2 family ID for uf2conv.py: 0xc47e5767
Ready - a drive should appear on the host
1 KiB
...
301 KiB
Verified - restarting into the new firmware
```

（sector 数は partition と `storage` の大きさで変わります。）

## 注意

- **生の `.bin` はホストが昇順に書く必要があります。** 空の volume へコピーするとき、主なファイルマネージャーはそうします。後ろへ戻る書込みや隙間のある書込みは `ESP_ERR_INVALID_STATE` で拒否し、中途半端に適用せず更新を中止します。`.uf2` にはこの制限がありません。
- **scratch 領域を切り詰めないでください。** `System Volume Information`、`.fseventsd`、`.Spotlight-V100` を吸収する場所です。ここが埋まると、ホストは firmware 領域の cluster を使い始め、ESP image の先頭でない書込みは無視されます。
- ESP の application でないファイルは flash に書かず黙って捨てます。壊れた image は最後の検証で失敗し、boot partition は変わりません。
- firmware 領域の読み出しは partition そのものから返すので、コピーした内容を照合するホストには実際に書かれた内容が見えます。
- **コピーした image がこのスケッチを置き換えます。** firmware ドライブ（または別の更新経路）を持つ build を送らないと、次の更新は boot mode 経由になります。
- `markValid()` が意味を持つのは `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` のときだけで、真正性の確認ではありません。製品では認証と image 署名の方針が別に必要です。ドライブへ書ける人なら誰でも書き換えられます。
- 通常の firmware の書き換えでは NVS が残ります。新しい application が初期設定の状態だとは仮定しないでください。
- [USB 経由のファームウェア更新](../../docs/ota-over-usb.ja.md)の経路のうち、最も使い勝手がよく、最もホストの挙動に左右される方法です。更新する人が terminal を使えるなら [FirmwareDFU](../FirmwareDFU/) が適しています。
- Arduino 内蔵の USB class（USB MSC / DFU / CDC on boot）をこのライブラリと同時に有効にしないでください。

## v2 からの変更点

- Function 名と 4 KiB の staging buffer（`transferBytes`。512 の倍数なので ESP32-P4 の HS でも有効）を、ビルド時の `CFG_TUD_MSC_EP_BUFSIZE` ではなく `msc.configure()` で指定します。
- 起動時に UF2 の family ID を表示し、README に `.uf2` の手順を書きました（2.x のドライブも UF2 を受け付けていましたが、例では示していませんでした）。
- 開始の失敗時に `msc.startError()` も表示します。
- 動作、partition の条件、確定の契機は変わりません。

## 関連

- [FirmwareDFU](../FirmwareDFU/)、[FirmwareHTTP](../FirmwareHTTP/)、[FirmwareCDC](../FirmwareCDC/)、[FirmwareVendor](../FirmwareVendor/) — 他の更新経路
- [FirmwareBootMode](../FirmwareBootMode/) — ROM loader による復旧経路
- [USB 経由のファームウェア更新](../../docs/ota-over-usb.ja.md)
- [MSC API ガイド](../../docs/classes/msc.ja.md)
- [スケッチ](FirmwareMSC.ino) / [全サンプル](../README.md) / [サンプルの進め方](../../docs/example-workflow.ja.md)
