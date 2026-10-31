# EspUsbDevice FirmwareBootMode

> English: [README.md](README.md)

動作中のスケッチから、チップの **ROM download loader**（BOOT を押しながら reset したときの状態＝いわゆる boot mode）へ入る例です。誰もボードに触らずに `esptool` で flash 全体を書き換えられます。

これは他のすべての更新経路の背後にある復旧手段です。OTA partition が無いファームウェアでも、自分を更新できないほど壊れたファームウェアでも通用します。loader は mask ROM にあり、壊すことができないからです。

## ハードウェア

- ESP32-S2 / ESP32-S3 / ESP32-P4 の board。**native USB OTG** の口でつなぐ
- `esptool`（必要なら `dfu-util` も）の入った PC
- ログ用の別の Serial 接続

## 動作内容

CDC serial port（「Console」）と DFU runtime interface（「Bootloader」）を提供し、loader を要求する方法を 3 通り用意します。行き先はすべて同じです。

| 操作 | ホスト側 |
|---|---|
| CDC port を 1200 bps で開いて DTR を落とす | `arduino-cli upload …`、Arduino IDE、`stty -F /dev/ttyACM0 1200` |
| `DFU_DETACH` | `dfu-util -e`（複数の device があるときは `dfu-util -d 303a:4000 -e`） |
| CDC port に `b` を送る | `printf b > /dev/ttyACM0`、または自前の script |

あとは通常どおり書き込みます。

```sh
esptool --port <port> write_flash 0x0 firmware.merged.bin
# または arduino-cli upload と同じく、各 image をそれぞれの offset へ
```

4 つ目の命令として、CDC port に `d` を送ると S2/S3 の **ROM DFU** device を要求します（下の `rebootToRomDfu()`）。ESP32-P4 と、`USB_PHY_SEL` eFuse を焼いていない ESP32-S3 ではライブラリが拒否するので、スケッチは `ROM_DFU_UNSUPPORTED` を表示し、CDC port にも返答して、そのまま動き続けます。

## loader がどのポートで応答するか

ここがチップごとに違い、いちばん驚かれる部分です。

| Chip | 再起動後 |
|---|---|
| ESP32-S3 | 共有 PHY が USB Serial/JTAG に戻るので、コネクタ 1 つのボードでは**同じケーブルがそのまま使えます**（新しい `/dev/ttyACM*` / COM port が現れる） |
| ESP32-S2 | OTG port 上の ROM 自身の CDC |
| ESP32-P4 | USB Serial/JTAG port。デバイスが載っていた HS の OTG コネクタ**ではありません** |

詳細（Secure Boot と `USB_PHY_SEL` eFuse の影響を含む）は [USB 経由のファームウェア更新 2.3 節](../../docs/ota-over-usb.ja.md#23-romが答えるusbの口)にあります。

## 主な API

- `device.rebootToBootloader()` — USB device を detach し、ターゲットの download-boot フラグを立てて再起動します。戻りません。フラグのレジスタはチップごとに違い、ESP32-P4 ではソフトウェアリセットのビットと同じレジスタに同居しています。これがライブラリの API になっている理由です。
- `device.rebootToRomDfu()` — S2/S3 の ROM を、serial loader ではなく USB-OTG 上の DFU デバイスとして立ち上げます。`dfu-util`（と `idf.py dfu` が作る `dfu.bin`）を使うホスト向け。ESP32-P4 では再起動せず `false` を返します。ESP32-S3 でも `USB_PHY_SEL` eFuse が焼かれていなければ同様に拒否します（実測：ROM が DFU をパッドの無いコントローラに載せるため、コネクタが沈黙します）。そのとき `device.lastErrorName()` は `ESP_ERR_NOT_SUPPORTED` です。改造していないボードで `dfu-util` を使いたい場合は [FirmwareDFU](../FirmwareDFU/) を使ってください。
- `port.onLineCoding(cb)` — ホストが要求した baud rate が渡ります。1200 bps touch の検出はこれです。
- `EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Runtime, "Bootloader")` — `dfu-util -e` が話しかける interface を追加します。handler がなければ、DETACH 要求の完了から約 100 ms 後に自分で ROM loader へ再起動します。
- `dfu.setDetachHandler(cb)` — その再起動を置き換えます。handler は要求の status 段の後に 1 回だけ呼ばれ、以後 bus から離れるのは handler 側の責任です。このスケッチは flag を立て、`loop()` で再起動します。
- `dfu.onDetach(cb)`（この例では使わない） — SETUP の時点で DETACH を受け付けるかを決めます。`false` を返すと STALL し、何も変えません。

## Serial の出力例

```text
Ready. Touch the CDC port at 1200 baud, run dfu-util -e, or send 'b' ('d' asks for the S2/S3 ROM DFU instead).
Restarting into the ROM download loader
```

## 注意

- **callback ではなく `loop()` で実行してください。** class の callback は USB worker 上で動き、その中から再起動すると、ホストがまだ終えていない control 転送を切ってしまいます。このスケッチは flag を立て、`loop()` で再起動します。CDC の入力も `loop()` で読みます。
- **意図的な操作の後ろに置いてください。** 紛れ込んだ 1 byte でユーザーの作業が bootloader に落ちるべきではありません。1200 bps touch はホストが既に話せる作法です。1 文字の命令は試験台向けです。
- Arduino-ESP32 の `usb_persist_restart()` は同じ仕事をしますが、このライブラリを使うスケッチからは **link できません**。`esp32-hal-tinyusb.c` を引き込み、同じ TinyUSB callback を 2 つ二重定義するためです。しかも ESP32-P4 では何もしません。`rebootToBootloader()` を使ってください（[ガイド 2.5 節](../../docs/ota-over-usb.ja.md#25-usb_persist_restartは使わない)）。
- download-boot フラグは残りません。`esptool` が書き込んで reset すると、ボードは通常どおり新しいアプリケーションを起動します。
- これは **flash 全体**を置き換えます。[FirmwareDFU](../FirmwareDFU/) や [FirmwareHTTP](../FirmwareHTTP/) はスケッチを動かしたまま OTA partition を 1 つ書く方式で、そこが違います。この例に OTA partition は要りません。
- Secure Boot や download mode を無効にする eFuse があると、この経路は使えません（[ガイド 2.7 節](../../docs/ota-over-usb.ja.md#27-これらを無効にするもの)）。

## 確認の手順

再起動の後は USB の識別と port が変わることを想定し、チップの書き込み経路を使ってください。この例は boot mode を変えるもので、application image を自分で書くものではありません。

## v2 からの変更点

- DFU Runtime：既定の再起動を置き換えていた v2 の `dfu.onDetach(void の callback)` は `dfu.setDetachHandler()` になりました。`onDetach()` は、SETUP の時点で受け付けるかを返す `bool` の判断になりました。
- `b` の命令は `port.onRx()` の中ではなく `loop()` で読みます。
- `device.rebootToRomDfu()` と、拒否されたときの経路を示す `d` 命令を加えました（v2 は README で説明するだけでした）。
- CDC の ring は `port.configure()` で決めます。DFU のログは callback から `loop()` へ移しました。

## 関連

- [FirmwareDFU](../FirmwareDFU/) — DFU Download mode（application の更新）
- [FirmwareMSC](../FirmwareMSC/)、[FirmwareHTTP](../FirmwareHTTP/)、[FirmwareCDC](../FirmwareCDC/)、[FirmwareVendor](../FirmwareVendor/) — 他の更新経路
- [USB 経由のファームウェア更新](../../docs/ota-over-usb.ja.md)
- [DFU API ガイド](../../docs/classes/dfu.ja.md)、[CDC API ガイド](../../docs/classes/cdc.ja.md)
- [スケッチ](FirmwareBootMode.ino) / [全サンプル](../README.md) / [サンプルの進め方](../../docs/example-workflow.ja.md)
