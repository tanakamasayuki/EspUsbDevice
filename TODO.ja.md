# TODO

## 仕様・設計

- [ ] TinyUSB / ESP-IDF device 初期化経路を確定する。
- [ ] ESP32-P4 の device port / speed 選択可否を probe で確認する。
- [ ] FS / HS の descriptor と endpoint MPS の期待値を unit test 化する。
- [ ] HID class の interface 構成を決める。初期は keyboard / mouse を別 interface にする。

## MVP

- [ ] device core: config、descriptor、string descriptor、lastError。
- [ ] HID keyboard: boot report、raw usage API、output report callback。
- [ ] HID mouse: boot report、raw move/click API。
- [ ] peer sketch: `hid_keyboard`。
- [ ] peer sketch: `hid_mouse`。
- [ ] peer sketch: `hid_keyboard_mouse`。
- [ ] P4 probe: FS device / HS device。
- [ ] P4 loopback: HID keyboard。

## 次フェーズ

- [ ] custom HID / vendor HID。
- [ ] consumer control / system control / gamepad。
- [ ] CDC ACM。
- [ ] USB MIDI。
- [ ] MSC。
- [ ] USB Audio。
