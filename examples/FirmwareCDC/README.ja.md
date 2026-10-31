# EspUsbDevice FirmwareCDC

> English: [README.md](README.md)

最も小さな firmware 更新です。普通の CDC serial port で、長さを送り、その byte 数だけ送ります。
ホスト側を標準ツールではなく自分で管理する script にしたい場合や、別の用途の serial port を持つ device に更新命令を足すときの手本として使えます。

## ハードウェア

- ESP32-S2 / ESP32-S3 / ESP32-P4 の board。**native USB OTG** の口でつなぐ
- Python 3 と `pyserial` の入った PC
- ログ用の別の Serial 接続

## 必要な条件

**application partition が 2 つある** partition scheme が必要です。`sketch.yaml` の全 profile が使う Arduino の「Default」scheme にはあります。1 つしかないとスケッチは `NO_OTA_PARTITION` を表示し、`ERR no OTA partition` と応答します。試す前に復旧の経路（ROM loader）を用意してください。

## プロトコル

```
host -> device   "FW <size>\n"
device -> host   "READY\n"          （または "ERR <reason>\n"）
host -> device   <size> byte
device -> host   "OK\n"             （または "ERR <reason>\n"）。その後再起動する
```

CDC は区切りのない byte stream なので、長さを先に送ります。長さがないと、device は image の終わりと送信の途切れを区別できません。
`<reason>` は短い文か、`ESP_ERR_INVALID_SIZE`（partition より大きい image）のような `esp_err_t` の名前です。

## 使い方

```sh
uv run --with pyserial python3 firmware_cdc.py /dev/ttyACM0 build/FirmwareCDC.ino.bin
# pyserial を入れてあれば
python3 firmware_cdc.py /dev/ttyACM0 build/FirmwareCDC.ino.bin   # Windows では COM5 など
```

[`firmware_cdc.py`](firmware_cdc.py) は、ファイルが `0xE9`（ESP の application image）で始まることを確かめ、4 KiB ずつ送り、速度を表示します。プロトコルが同じなので、2.x のものをそのまま使っています。

image は普通の Arduino の `.bin`（`<build-path>/<sketch>.ino.bin`）で、`*.merged.bin` や `*.bootloader.bin` ではありません。

## 主な API

- `EspUsbDeviceCdcSerial port(device, "Firmware")` — 転送路。`port.configure(EspUsbBulkStreamConfig)` で容量を決める。この例は受信 ring 8 KiB、送信 ring 256 byte。
- `EspUsbDeviceFirmwareUpdate update` — flash 側。`begin(size)` / `write()` / `end()` / `abort()` と `lastErrorName()`。
- `update.begin(size)` に本当の長さを渡すと、partition より大きい image を**ホストが 1 byte も送る前に**拒否できる。
- `update.end()` は boot partition を切り替える前に検証するので、途中で切れた upload は次の起動ではなくここで失敗する。
- `port.flush()` は応答の送信が終わるまで（`setTxTimeoutMs()`、既定 1000 ms まで）待つ。再起動の前に `OK` がホストへ届く。

## Serial の出力例

```text
Update target: app1 (1310720 bytes)
Ready. python3 firmware_cdc.py <port> <firmware.bin>
Receiving 301456 bytes into app1
1 KiB
...
294 KiB
Verified - restarting into the new firmware
```

## 注意

- **flash への書込みは USB callback ではなく `loop()` で行います。** class の callback は USB worker で動き、flash の消去にかかる数 ms が device 全体から奪われます。このスケッチは `port.available()` を poll します。これは偶然ではなく、真似すべき形です。`loop()` が消去している間に届いたデータは受信 ring が吸収し、あふれそうになるとホストへ NAK を返すので、データは失われません。
- FS の bulk は理論上 1.216 MB/s が上限で、CDC の枠組みと chunk ごとの flash 書込みでかなり下回ります。300 KB の image なら十分ですが、P4 で 1 MB の image なら [FirmwareVendor](../FirmwareVendor/) のほうが速く、[FirmwareDFU](../FirmwareDFU/) は標準のホストツールで使えます。
- プロトコル自体には認証も整合性の確認もありません。`end()` が確かめるのは image 自身の checksum です。port を開ける人なら誰でも書き換えられます。`markValid()` も真正性の確認ではありません。
- 送った image がこのスケッチを置き換えます。更新経路を持つ image を送ってください。
- Arduino 内蔵の USB class（USB MSC / DFU / CDC on boot）をこのライブラリと同時に有効にしないでください。

## 確認の手順

ホストは `FW <size>` を送り、`READY` を待ってから、ちょうどその byte 数の binary を送ります。`readCommand()` と `readImage()` は別の状態を扱います。文字を送る serial terminal は binary の uploader の代わりになりません。受信の進捗、`update.end()` の検証、再起動はそれぞれ別の段階です。

## v2 からの変更点

- 受信・送信の ring をビルド時の CDC buffer の macro ではなく、実行時に `port.configure()` で決めます（受信 8 KiB。`loop()` が flash を消去している間も USB が止まりにくい）。`loop()` は 1 KiB ずつ読みます。
- `port.flush()` は応答の送信が終わるまで（TX timeout の範囲で）待つようになりました。再起動前の `OK` はこれに頼っています。
- プロトコルとホストの script は 2.x と同じです。

## 関連

- [FirmwareVendor](../FirmwareVendor/) — 同じことを vendor bulk で、より速く
- [FirmwareDFU](../FirmwareDFU/) — 同じことを `dfu-util` で
- [USB 経由のファームウェア更新](../../docs/ota-over-usb.ja.md)
- [CDC API ガイド](../../docs/classes/cdc.ja.md)
- [スケッチ](FirmwareCDC.ino) / [全サンプル](../README.md) / [サンプルの進め方](../../docs/example-workflow.ja.md)
