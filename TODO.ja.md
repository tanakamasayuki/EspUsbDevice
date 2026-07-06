# TODO

- [ ] （任意）P4 2台 HS peer 構成で UAC2/HS の Audio を自動テスト化する。
- [ ] 実 PC/HS 対応マイルストーンで endpoint の per-speed descriptor 化（FS=64 / HS bulk=512、device_qualifier / other_speed 対応）。現状は全 class FS 固定で、P4 を実 HS ホストに繋ぐと bulk が非準拠。`docs/DESIGN_NOTES.ja.md`「bulk エンドポイントサイズと HS 準拠」参照。
- [ ] USB Audio composite device。HID / CDC / MIDI などとの複合化可否と制約を確認する。
- [ ] 複合時の endpoint 採番衝突の修正。HID の endpoint が core のビットマスクに登録されず（`reserve_endpoints=false` + 独自採番 EP1/EP2）、MSC/MIDI/Vendor の動的採番と衝突して `begin()`==ESP_FAIL になる。HID を core アロケータに載せる（EP1 duplex + `reserve_endpoints=true`、または `tinyusb_get_free_*` 採番）。unit/descriptor の HID EP 期待値も更新。`docs/DESIGN_NOTES.ja.md`「複合時の endpoint 採番衝突」、`tests/peer/composite_hid_msc`（現状 xfail）参照。
- [ ] USBVendor の custom vendor code / Microsoft OS 2.0 descriptor 差し替え API。
- [ ] WebUSB / libusb / WinUSB の手動確認手順とサンプル Host 側コード。
- [ ] FirmwareMSC。FAT RAM disk 上の `firmware.bin` を安全に扱う helper / example。
- [ ] CDC + HID + MSC + Vendor などの all-in-one composite example。
- [ ] Keyboard macro / Serial-to-keyboard / Button mouse などの応用 example。
