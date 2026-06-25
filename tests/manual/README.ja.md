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
