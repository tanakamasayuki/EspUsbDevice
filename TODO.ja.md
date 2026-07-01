# TODO

## 初回リリースで対応済み

- [x] USB device core: config、descriptor、string descriptor、lastError。
- [x] HID keyboard: layout、文字列 helper、raw usage、LED output report、protocol callback。
- [x] HID mouse: move、wheel、button press / release。
- [x] keyboard + mouse composite HID。
- [x] custom HID descriptor。
- [x] HID vendor input / output / feature report。
- [x] consumer control / system control / gamepad。
- [x] CDC ACM serial。
- [x] USB MIDI。
- [x] MSC raw block device。
- [x] MSC RAM disk、FAT RAM disk、SD card helper。
- [x] HID ではない USBVendor bulk IN/OUT、control request、WebUSB landing URL。
- [x] unit / peer / loopback / examples compile の pytest 構造。

## リリース前確認

- [x] `uv run --env-file .env pytest --clean` を実機環境で通す。
- [x] `library.properties`、`keywords.txt`、README、examples README の公開内容を確認する。
- [ ] release bump / changelog 生成は共通スクリプトに任せる。

## 次フェーズ候補

- [ ] USB Audio class。まず最小 Audio sink / source の仕様確認から始める。
- [ ] USBVendor の custom vendor code / Microsoft OS 2.0 descriptor 差し替え API。
- [ ] WebUSB / libusb / WinUSB の手動確認手順とサンプル Host 側コード。
- [ ] FirmwareMSC。FAT RAM disk 上の `firmware.bin` を安全に扱う helper / example。
- [ ] CDC + HID + MSC + Vendor などの all-in-one composite example。
- [ ] Keyboard macro / Serial-to-keyboard / Button mouse などの応用 example。
