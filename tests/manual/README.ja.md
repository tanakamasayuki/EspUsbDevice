# 手動テスト

> English: [README.md](README.md)

手動テストは、pytest だけでは完全に制御できない挙動に限定します。
例: ホスト OS の列挙表示、LED の目視確認、外部 USB analyzer、物理的な配線変更。

## `examples/USBVendor`

目的:

- Host OS が vendor-specific interface を認識できることを確認する。
- bulk IN / OUT の echo が動くことを確認する。
- vendor control request に Device が応答できることを確認する。
- WebUSB BOS descriptor と landing URL が Host / browser から見えることを確認する。

手順:

1. `examples/USBVendor` を USB device 側 board に書き込む。
2. Serial monitor を開き、`USB vendor device ready` を確認する。
3. USB device port を PC に接続する。
4. Linux では `lsusb -d 303a:4019 -v` で以下を確認する。
   - `bInterfaceClass 255 Vendor Specific Class`
   - bulk OUT endpoint
   - bulk IN endpoint
   - BOS descriptor に WebUSB platform capability があること
5. libusb / WinUSB / WebUSB などの Host 側 tool から interface を claim する。
6. bulk OUT に短い byte列を送信し、bulk IN で `echo: ...` が返ることを確認する。
7. control IN request `bRequest = 0x01` を送り、`EspUsbDeviceVendor` が返ることを確認する。
8. control OUT request `bRequest = 0x02` を送り、status stage が成功することを確認する。
9. WebUSB 対応 browser で device を選択し、landing URL が期待どおり見えるか確認する。

期待:

- Serial monitor に `VENDOR_RX` と `VENDOR_CONTROL` が出る。
- Host 側で `bInterfaceClass = 0xff` の interface を開ける。
- bulk OUT の payload が bulk IN の echo と一致する。
- WebUSB URL は `example.com/espusbdevice` として返る。

注意:

- Host OS によっては kernel driver detach、permission、udev rule、WinUSB driver binding が必要。
- `EspUsbDevice` は WebUSB URL を設定できるが、vendor code や Microsoft OS 2.0 descriptor の
  内容差し替え API はまだ持たない。
- Arduino-ESP32 TinyUSB core は WebUSB 有効時に Microsoft OS 2.0 descriptor も返す。
  GUID などの内容は core 側既定値になる。
- この確認は Host OS / browser / driver の状態に依存するため、自動テストではなく manual に置く。

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
