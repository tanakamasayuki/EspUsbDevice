# TODO

- [ ] USB Audio speaker sink の実機長時間確認。PC からの連続再生、volume / mute、stream stop / restart を見る。
- [ ] `AudioSinkM5Speaker` の実音確認。PCMFlowDevice profile、drop / gap / wait の実測値を整理する。
- [x] USB Audio の loopback test を追加する（`loopback/usb_audio`）。ただし P4 では現状 `xfail`。manual test は自動化範囲と人手確認範囲を分けて別途整理する。
- [ ] P4 の USB Audio 速度対応。1台 loopback は FS リンクだが device が UAC2 を出し EspUsbHost(UAC1のみ)が解釈できない。宣言速度(`config_.speed`)で UAC1/UAC2 を出し分けるか、EspUsbHost に UAC2 パーサを追加する。`docs/DESIGN_NOTES.ja.md`「P4 USB ポート/PHY の実測整理」参照。`loopback/usb_audio` の xfail を外すのが完了条件。
- [ ] USB Audio microphone path。`writeMic()` の example / test と、Device -> Host PCM の仕様を固める。
- [ ] USB Audio composite device。HID / CDC / MIDI などとの複合化可否と制約を確認する。
- [ ] USBVendor の custom vendor code / Microsoft OS 2.0 descriptor 差し替え API。
- [ ] WebUSB / libusb / WinUSB の手動確認手順とサンプル Host 側コード。
- [ ] FirmwareMSC。FAT RAM disk 上の `firmware.bin` を安全に扱う helper / example。
- [ ] CDC + HID + MSC + Vendor などの all-in-one composite example。
- [ ] Keyboard macro / Serial-to-keyboard / Button mouse などの応用 example。
