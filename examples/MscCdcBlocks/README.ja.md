# EspUsbDevice MscCdcBlocks

> English: [README.md](README.md)

自分で書いた raw な MSC block device と、Host がそれを使った回数を知らせる CDC port を 1 台に載せる例です。

MSC 部分は helper class を使いません。sketch が 512 byte の block を 16 個 RAM に持ち、自分の `onRead()` / `onWrite()` callback を渡します。独自の保存先をつなぐときは、どれもこの形になります。CDC port は各 callback が動いた回数を表示します。

## ハードウェア

- native USB OTG port を持つ ESP32-S2 / S3 / P4 board
- USB storage を扱え、serial port を開ける USB host（PC、または EspUsbHost を動かす別 ESP32）
- 開始時のエラー用の別 Serial monitor 接続（任意）

## 動作内容

- 8 KiB の配列（`blockCount = 16`、`blockBytes = 512`）を持ちます。reset で 0 になり、filesystem はありません。
- 範囲を確かめてから配列との間で写す read / write callback を登録し、呼ばれた回数を `std::atomic` の counter で数えます。
- SCSI INQUIRY の文字列（`ESPUSB` / `RAM blocks`）を設定し、medium ありにし、`transferBytes` を 512 にし、`disk.begin(16, 512)` で容量を決めます。
- CDC port（interface 名 `disk statistics`、受信 ring 512 byte、送信 ring 256 byte、`transferBytes = 64`）を設定します。
- product 文字列 `RAM blocks and console` で `EspUsbDevice` を開始します。
- terminal が CDC port の DTR を立てている間、1 秒ごとと `?` が届いたときに `reads=N writes=M` を表示します。
- 失敗した段階のエラー番号を Serial monitor に表示します。

## Host 側での確かめ方

1. sketch を書き込み、USB device port を接続します。
2. Host には partition table も filesystem もない 8 KiB の removable disk が見えます。OS が format を勧めることがあります。それは中身の問題で、USB の失敗ではありません。raw の内容を比べたいときは format しないでください。
3. CDC port を開きます（Linux では `picocom /dev/ttyACM0`）。counter が表示されます。OS が disk を調べるときに読むので、`reads` は多くの場合すでに 0 より大きくなっています。
4. Linux では `lsblk` で disk（大きさ 8K）を探し、raw で読み書きします。device 名はよく確かめてください。別の disk に書くとその data が壊れます。

   ```sh
   sudo dd if=/dev/sdX bs=512 count=1 iflag=direct | xxd | head -2
   printf 'hello' | sudo dd of=/dev/sdX bs=512 seek=1 conv=sync oflag=direct
   sudo dd if=/dev/sdX bs=512 skip=1 count=1 iflag=direct | xxd | head -1
   ```

   最初の読み出しは 0 です。書いた後は block 1 が `hello` で始まり、CDC 出力の `writes` が増えます。
5. board を reset すると、中身は 0 に戻ります。

Windows と macOS はこの例で未確認です。

## 主要 API

- `EspUsbDeviceMsc disk(device)` は MSC（Bulk-Only Transport）function を登録します。
- `disk.onRead(cb)` / `disk.onWrite(cb)`：`int32_t(lba, offset, buffer, size)`。1 回の呼び出しは block `lba` の `offset` byte 目から `size` byte です。`size` は最大 `transferBytes` で、block の境界をまたぐことがあります。成功したらちょうど `size` を返します。それ以外は command の失敗（Medium Error）です。
- `disk.vendorID()` / `productID()` は INQUIRY の文字列です（最大 8 文字と 16 文字）。`disk.mediaPresent(true)` で medium ありを知らせます。
- `disk.configure(EspUsbMscConfig)`：`transferBytes` は中継 buffer の大きさです。64 の倍数が必要です。`device.begin()` の時点では bulk packet の大きさ（FS は 64、HS は 512）の倍数でもある必要があります。512 なら両方で動きます。
- `disk.begin(blockCount, blockSize)` は容量を決めるだけです。read callback がないと失敗します。USB は `device.begin()` で始まります。
- `disk.startError()` はどちらの `begin()` の失敗理由も返します。
- `console.connected()` は「構成済みかつ DTR=1」です。

## 注意

- counter は callback の呼び出し回数です。SCSI command の数でも、USB packet や byte の数でもありません。
- callback は USB worker で動きます。短く保ち、`device.end()` を呼ばないでください。`loop()` は別の task から counter を読むので、counter は atomic にしています。
- mount している間、medium は Host のものです。同時に sketch から `storage` を変えないでください。
- file のある disk には [MSCFatRamDisk](../MSCFatRamDisk/)、SD card には [MSCSdCard](../MSCSdCard/) を使います。

## 関連

- [MSC](../MSC/) - raw block の RAM disk helper
- [MSCFatRamDisk](../MSCFatRamDisk/) - RAM 上の FAT image
- [CompositeHidCdcMsc](../CompositeHidCdcMsc/) - HID、CDC、MSC を 1 台に
- [CdcConsole](../CdcConsole/) - `Stream &` を通した CDC
- [MSC API](../../docs/classes/msc.ja.md)、[CDC API](../../docs/classes/cdc.ja.md)

## 読み方と確認の手順

### 実行前の準備

上のハードウェアの条件を確認し、board と例の profile を選びます。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USB の役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

Host と配列の間の安全確認は `inRange()` だけです。末尾を越える block 番号と、最後の byte を越える要求を拒みます。`setup()` の各段階は、最初のエラーで表示して止まります。

### API の使い方と改造時の注意

ほかの保存先を使うには、2 つの `memcpy()` を置き換え、約束を守ります。書き込みは戻る前に終え、ちょうど `size` を返します。遅い保存先は、その間 USB worker を止めます。

### 一つずつ確認する

まず raw の読み書きを確かめ、それから block 数や保存先を一つずつ変えます。

## v2 からの変更点

この例は v3 で新しく作りました。v2 には対応する例がありません。

- [スケッチ](MscCdcBlocks.ino)
- [全サンプル](../README.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
