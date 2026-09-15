# s3_single_cable

> English: [README.md](README.md)

**スケッチが使っていたコネクタは、ROM の loader として戻ってくるか。**

[docs/ota-over-usb.ja.md 2.3節](../../../docs/ota-over-usb.ja.md#23-romがどのusb-interfaceで応答するか)
の主張——コネクタが 1 つの ESP32-S3 で、USB デバイスを運んでいたのと同じケーブルが
`rebootToBootloader()` の後に ROM の serial loader を運ぶ——を確かめるものです。
**native USB が PC に配線された**ボードと、ホスト側を見る人が要るので、
`tests/single` ではなくここにあります。

`rebootToRomDfu()` が、誰も届かないコネクタへ再起動するのではなく**拒否する**ことも
確認します。

## 必要なもの

- native USB（GPIO19/20）が PC に繋がった ESP32-S3
- 書き込みとログ用の**別系統の** UART。native USB は試験対象そのものなので、
  復旧には使えません
- ホストが何を列挙したか見る手段（Windows なら `usbipd list`、Linux なら
  `lsusb` / `dmesg`）

## 別系統の UART が必須である理由

このテストの本質は、native USB をホストから取り上げて返すことです。返却が壊れて
いると——まさにこのテストが一度捕まえた不具合です——コネクタが沈黙し、他の入り口は
BOOT ボタンだけになります。

## 実行

```sh
# 書き込みは必ず UART 経由。native USB は使わないこと。
arduino-cli compile --profile esp32s3 .
arduino-cli upload --profile esp32s3 --port /dev/ttyACM3 .
```

スケッチは vendor + DFU デバイスを起動し、20 秒待ってから再起動します。
`-DS3_CABLE_ACTION=1` で `rebootToBootloader()` の代わりに `rebootToRomDfu()` を
選びます。

**20 秒の間に UART を開かないでください。** auto-reset が DTR/RTS に配線された
ボードでは、開いた時点でチップがリセットされ、何も測れません。ホスト側を見てください。

## 期待される結果

`rebootToBootloader()`、同じコネクタを見ていて:

| いつ | ホストが見るもの |
|---|---|
| スケッチ動作中 | スケッチ自身の VID:PID（このまま焼けば `303a:4095`） |
| 再起動後 | `303a:1001`、USB Serial/JTAG。そのまま安定 |

続いて `esptool --port <そのポート> --before default-reset chip-id` が stub flasher を
転送して実行できること。Windows なら
`uv run --with esptool esptool --port COM12 ...`、WSL からなら先に attach します。

`USB_PHY_SEL` eFuse を焼いていないボードでの `rebootToRomDfu()`:

```
S3CABLE_ROM_DFU_UNSUPPORTED ESP_ERR_NOT_SUPPORTED
```

そして**デバイスは動作を続けます**——コネクタにはスケッチが見えたままです。
ここでコネクタが沈黙したら、それがこのテストの存在理由である回帰です。

## このテストが書かれた原因の不具合

修正前の `rebootToBootloader()` は、PHY の選択を USB-OTG に向けたまま再起動して
いました。その選択は `RTC_CNTL_USB_CONF_REG` にあり、**RTC ドメインなので software
reset を生き延びます**。そのため ROM の loader は、自分が駆動していない controller に
pin が繋がった状態で起動していました。実測: ホストから USB ポートが完全に消え、
UART 経由の `esptool --before no-reset` は接続しました——チップは loader にいて、
誰も届かない状態でした。
