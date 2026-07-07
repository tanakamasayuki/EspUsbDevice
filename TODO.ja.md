# TODO

- [ ] （任意）P4 2台 HS peer 構成で UAC2/HS の Audio を自動テスト化する。
- [ ] 実 PC/HS 対応マイルストーンで endpoint の per-speed descriptor 化（FS=64 / HS bulk=512、device_qualifier / other_speed 対応）。現状は全 class FS 固定で、P4 を実 HS ホストに繋ぐと bulk が非準拠。`docs/DESIGN_NOTES.ja.md`「bulk エンドポイントサイズと HS 準拠」参照。
- [ ] USB Audio composite device。HID / CDC / MIDI などとの複合化可否と制約を確認する。
- [x] 複合時の HID 採番衝突の修正（実機確認済み）。(1) HID を EP1 duplex + `reserve_endpoints=true` で core のビットマスクに登録、(2) `espUsbDeviceLoadHidDescriptor` で HID interface number を core 採番値へ書き換え。HID+MSC が 3/3 pass。`docs/DESIGN_NOTES.ja.md`「複合時の HID 採番衝突」、`tests/peer/composite_hid_msc` 参照。
- [ ] 複合時に vendor RX callback（`tud_vendor_rx_cb` → `onRx`）が発火しない件の調査。transport は正常でデータは FIFO に届く（`available()`/`read()` は可、ポーリングで動く）が、CDC/MSC と複合すると onRx が呼ばれない（standalone では発火）。EspUsbDevice で救えるか TinyUSB/core 制約かを切り分け、当面は vendor RX ポーリング推奨を注記。`docs/DESIGN_NOTES.ja.md`「複合時の vendor RX callback」、`tests/peer/composite_cdc_msc_vendor` 参照。
- [ ] HID + bulk Vendor（`EspUsbDeviceVendor`）の複合対応。`espUsbDeviceLoadHidDescriptor` が `configurationDescriptor(0)` の HID 以降全体をコピーするため、bulk vendor interface が二重記述される疑い。HID を含む複合の bulk Vendor は当面スコープ外。`docs/DESIGN_NOTES.ja.md` 参照。
- [ ] USBVendor の custom vendor code / Microsoft OS 2.0 descriptor 差し替え API。
- [ ] WebUSB / libusb / WinUSB の手動確認手順とサンプル Host 側コード。
- [ ] FirmwareMSC。FAT RAM disk 上の `firmware.bin` を安全に扱う helper / example。
- [ ] CDC + HID + MSC + Vendor などの all-in-one composite example。
- [ ] Keyboard macro / Serial-to-keyboard / Button mouse などの応用 example。
