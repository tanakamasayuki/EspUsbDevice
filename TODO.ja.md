# TODO

- [ ] （任意）P4 2台 HS peer 構成で UAC2/HS の Audio を自動テスト化する。
- [ ] 実 PC/HS 対応マイルストーンで endpoint の per-speed descriptor 化（FS=64 / HS bulk=512、device_qualifier / other_speed 対応）。現状は全 class FS 固定で、P4 を実 HS ホストに繋ぐと bulk が非準拠。`docs/DESIGN_NOTES.ja.md`「bulk エンドポイントサイズと HS 準拠」参照。
- [ ] USB Audio composite device。HID / CDC / MIDI などとの複合化可否と制約を確認する。
- [ ] USBVendor の custom vendor code / Microsoft OS 2.0 descriptor 差し替え API。
- [ ] WebUSB / libusb / WinUSB の手動確認手順とサンプル Host 側コード。
- [ ] FirmwareMSC。FAT RAM disk 上の `firmware.bin` を安全に扱う helper / example。
- [ ] CDC + HID + MSC + Vendor などの all-in-one composite example。
- [ ] Keyboard macro / Serial-to-keyboard / Button mouse などの応用 example。
