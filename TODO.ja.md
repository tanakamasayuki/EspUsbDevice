# TODO

- [ ] USB Audio speaker sink の実機長時間確認。PC からの連続再生、volume / mute、stream stop / restart を見る。
- [ ] `AudioSpeakerM5` の実音確認。PCMFlowDevice profile、drop / gap / wait の実測値を整理する。
- [x] P4 の USB Audio は UAC2/HS 専用と決定。1台 loopback は FS 限定で噛み合わないため loopback audio テストは置かない（`tests/loopback/usb_audio` 削除）。S3 peer(UAC1) + 実機 HS 手動でカバー。`docs/DESIGN_NOTES.ja.md`「USB Audio への影響と決定」参照。
- [ ] （任意）P4 2台 HS peer 構成で UAC2/HS の Audio を自動テスト化する。
- [ ] 実 PC/HS 対応マイルストーンで endpoint の per-speed descriptor 化（FS=64 / HS bulk=512、device_qualifier / other_speed 対応）。現状は全 class FS 固定で、P4 を実 HS ホストに繋ぐと bulk が非準拠。`docs/DESIGN_NOTES.ja.md`「bulk エンドポイントサイズと HS 準拠」参照。
- [ ] USB Audio の manual test 手順を整理する（自動化範囲と人手確認範囲を分ける）。
- [ ] USB Audio microphone path。`writeMic()` の example / test と、Device -> Host PCM の仕様を固める。
- [ ] USB Audio composite device。HID / CDC / MIDI などとの複合化可否と制約を確認する。
- [ ] USBVendor の custom vendor code / Microsoft OS 2.0 descriptor 差し替え API。
- [ ] WebUSB / libusb / WinUSB の手動確認手順とサンプル Host 側コード。
- [ ] FirmwareMSC。FAT RAM disk 上の `firmware.bin` を安全に扱う helper / example。
- [ ] CDC + HID + MSC + Vendor などの all-in-one composite example。
- [ ] Keyboard macro / Serial-to-keyboard / Button mouse などの応用 example。
