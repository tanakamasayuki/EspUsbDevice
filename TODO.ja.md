# TODO

- [ ] USB Audio speaker sink の実機長時間確認。PC からの連続再生、volume / mute、stream stop / restart を見る。
- [ ] `AudioSinkM5Speaker` の実音確認。PCMFlowDevice profile、drop / gap / wait の実測値を整理する。
- [x] USB Audio の loopback test を追加する（`loopback/usb_audio`）。manual test は自動化できる範囲と人が確認する範囲を分けて別途整理する。
- [ ] USB Audio microphone path。`writeMic()` の example / test と、Device -> Host PCM の仕様を固める。
- [ ] USB Audio composite device。HID / CDC / MIDI などとの複合化可否と制約を確認する。
- [ ] USBVendor の custom vendor code / Microsoft OS 2.0 descriptor 差し替え API。
- [ ] WebUSB / libusb / WinUSB の手動確認手順とサンプル Host 側コード。
- [ ] FirmwareMSC。FAT RAM disk 上の `firmware.bin` を安全に扱う helper / example。
- [ ] CDC + HID + MSC + Vendor などの all-in-one composite example。
- [ ] Keyboard macro / Serial-to-keyboard / Button mouse などの応用 example。
