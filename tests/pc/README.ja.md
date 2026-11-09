# PC テスト

> English: [README.md](README.md)

Linux PC を Host にして、Device の動作を wire の上で確かめます。PC 側は pyusb（libusb）で生の control / bulk 転送を組みます。sg3_utils や dfu-util は使いません。

| probe | 目的 |
| --- | --- |
| `msc_media` | MSC の媒体交換と抜去を、PC の BOT client で確かめる（06/28/00、02/3A/00、residue） |
| `msc_fat` | `EspUsbDeviceMscFatRamDisk` の 1 / 4 / 24 MiB の volume を PC が読み、`fsck.fat` で検査する。PSRAM が要る |
| `dfu_detach` | DFU DETACH の受付と、再起動までの時間を測る |

`pc_bot.py` は、MSC の試験が共有する BOT client です。bulk の MPS は、HS なら 512、FS なら 64 を期待します。

## 動く組

| combo | board | link |
| --- | --- | --- |
| `p4x_pc` | P4X の HS 口（bench の 3-3 / 3-4） | HS |
| `primary=s2_pc`（名前のない combo） | S2 の native USB | FS。`msc_media` と `dfu_detach` だけ |

```sh
uv run --locked --env-file .env pytest pc --combo p4x_pc -q -rs
uv run --locked --env-file .env pytest pc/msc_media pc/dfu_detach --combo primary=s2_pc -q -rs
```

- S2 を PC につなぐ組は常設していません。S2 の native USB を PC へつなぎ、`.env` に `TEST_SERIAL_PORT_S2_PC` を書いたときだけ流します。
- OS の class driver（usb-storage など）が先に bind する場合、試験側で明示的に detach します。
- 一般 user で開ける VID/PID は udev 規則で限られています（`1209:0001` など）。PC 試験の sketch はその VID/PID を使います。新しい VID/PID が要るときは、udev 規則の追加を依頼します。
