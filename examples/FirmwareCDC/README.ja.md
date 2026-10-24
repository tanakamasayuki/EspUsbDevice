# EspUsbDevice FirmwareCDC

> English: [README.md](README.md)

最小のファームウェア更新です。素の CDC serial port に、長さを送ってからその分の
バイトを送るだけ。host 側を標準ツールではなく自前スクリプトにしたい場合や、
すでに serial port を持つ機器に更新コマンドを足すときの雛形として使えます。

## ハードウェア

- USB device 対応の ESP32-S2 / ESP32-S3 / ESP32-P4 board
- Python と `pyserial` が使える PC
- ログ確認用の Serial 接続

## 前提

**application partition が 2 つある** partition scheme。

## プロトコル

```
host -> device   "FW <size>\n"
device -> host   "READY\n"          （または "ERR <reason>\n"）
host -> device   <size> バイト
device -> host   "OK\n"             （または "ERR <reason>\n"）、その後再起動
```

長さを先に送るのは、CDC が framing の無いバイトストリームだからです。長さが無いと
device はイメージの終わりと単なる間を区別できません。

## 使い方

```sh
uv run --with pyserial python3 firmware_cdc.py /dev/ttyACM0 build/FirmwareCDC.ino.bin
```

## 主な API

- `EspUsbDeviceCdcSerial port(device, "Firmware")` が転送路です。
- `EspUsbDeviceFirmwareUpdate update` が flash 側を担当します
  （`begin(size)` / `write()` / `end()` / `abort()`）。
- 実際のサイズを渡す `update.begin(size)` は、partition より大きいイメージを
  **host が 1 バイトも送る前に**拒否します。
- `update.end()` は boot partition を移す前に検証するので、途中で切れた upload は
  次回起動時ではなくそこで落ちます。

## 注意

- **flash への書き込みは USB callback ではなく `loop()` で行っています。** この
  ライブラリの class callback はすべて usbd task 上で動き、flash の erase は
  device 全体から数ミリ秒を奪います。このスケッチは代わりに `port.available()` を
  polling します。これは書き方の偶然ではなく、真似すべきパターンです。
- full-speed bulk の上限は理論値 1.216 MB/s で、CDC の framing と chunk ごとの flash
  書き込みでそれを大きく下回ります。300KB のイメージなら十分ですが、P4 で 1MB なら
  [`FirmwareVendor`](../FirmwareVendor/) の方が速く、
  [`FirmwareDFU`](../FirmwareDFU/) なら標準ツールが使えます。
- このプロトコル自体には認証も整合性チェックもありません。検証するのはイメージ自身の
  checksum を見る `end()` だけです。port を開ける相手なら誰でも書き換えられます。
- このライブラリは Arduino 標準の USB device class と同時には使えません。

## 読み方と確認の手順

### 実行前の準備

CDCから長さを指定したfirmware imageを受け取る。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

HostはFWとサイズの命令を送り、受付応答を待ってから、その長さのbinaryを送ります。`readCommand()`とreadImage()は別の状態です。テキスト端末はbinary uploaderの代わりになりません。受信進捗とupdate.`end()`の検証・再起動を分けます。

### APIの使い方と改造時の注意

選んだtargetとpartition構成のapplication imageを使い、結合Flash dumpやbootloader binaryと混同しないようにします。利用可否・容量を確認してから送信します。転送の応答、受信byte数、image検証、boot先選択、次の起動成功は別段階です。ROM loaderの例はboot modeの変更であり、同じアプリ内更新ではありません。

試験前に記載の復旧・書き込み経路を確保します。通常のfirmware更新でNVSが残る場合があるので、新しいアプリが初期設定状態とは仮定しません。`markValid()`はアプリの検証状態に関わるもので、任意の受信imageの真正性を証明するAPIではありません。実製品では認証・image検証の方針も必要です。Flash・再起動は例の実行経路に置き、新たな長時間USB callbackへ移さないようにします。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](FirmwareCDC.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
