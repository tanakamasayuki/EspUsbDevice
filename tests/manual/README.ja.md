# 手動テスト

> English: [README.md](README.md)

手動テストは、pytest だけでは完全に制御できない挙動に限定します。
例: ホスト OS の列挙表示、LED の目視確認、外部 USB analyzer、物理的な配線変更。

## `examples/MSCFatRamDisk`

目的:

- Host OS が `EspUsbDeviceMscFatRamDisk` の FAT12 RAM disk を mount できることを確認する。
- Host から `CONFIG.TXT` をコピーし、eject / unmount 後に Device 側で読めることを確認する。

手順:

1. `examples/MSCFatRamDisk` を USB device 側 board に書き込む。
2. Serial monitor を開き、`USB FAT RAM disk ready` を確認する。
3. USB device port を PC に接続する。
4. PC 側で `ESPUSB` drive が見えることを確認する。
5. drive の root に `CONFIG.TXT` をコピーする。
6. OS の eject / unmount を実行する。
7. Serial monitor に `MSC_EJECT`、`CONFIG_SIZE`、`CONFIG_BEGIN` / `CONFIG_END` が出ることを確認する。

期待:

- 初期ファイル `README.TXT` が Host 側で見える。
- `CONFIG.TXT` の内容が Serial に出る。
- eject 前に ESP32 側が file scan しない。

注意:

- RAM disk なので reset / power cycle で内容は消える。
- Host OS が format を要求した場合は、その OS が小容量 FAT12 image を mount できていない可能性がある。
- Host が書き込み中に ESP32 側で FAT を読む設計にはしない。
- 大きい firmware image の受け渡しは、この example ではなく PSRAM、SD card、または streaming update で扱う。

## `examples/MSCSdCard`

目的:

- SPI 接続の SD card を USB MSC として Host OS から読み書きできることを確認する。
- Host の eject / unmount 後に Device 側が所有権を戻せることを確認する。

手順:

1. board に合わせて `examples/MSCSdCard/MSCSdCard.ino` の `SD_CS_PIN` を変更する。
2. SD card を挿入する。内容は Host から変更されるため、必要なら backup しておく。
3. `examples/MSCSdCard` を USB device 側 board に書き込む。
4. Serial monitor を開き、`USB SD MSC ready` を確認する。
5. USB device port を PC に接続する。
6. PC 側で SD card が USB storage として見えることを確認する。
7. 小さい test file を作成、読み戻し、削除する。
8. OS の eject / unmount を実行する。
9. Serial monitor に `SD_EJECT` が出ることを確認する。

期待:

- Host から SD card の既存 FAT filesystem を mount できる。
- Host からの write が SD card に反映される。
- eject 前に ESP32 側で `SD.open()` などの file API を使わない。

注意:

- Host と ESP32 が同時に同じ SD filesystem を書くと破損しやすい。
- この example は `SD.begin()` により Arduino 側 filesystem も mount するが、MSC 所有中は file API を使わない。
- SD card socket、CS pin、SPI pin は board ごとに異なる。
