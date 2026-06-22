# テスト計画

> English: [TEST_PLAN.md](TEST_PLAN.md)

## テスト方針

EspUsbDevice は、最終的には汎用的に使いやすい USB Device ライブラリにすることを
目的とします。テスト計画は `EspUsbHost` の peer / loopback シナリオから始めます。
これは低レベル API と descriptor を実ハードウェアで明確かつ再現性高く検証できるためです。

テストは、ソフトウェアで環境をどこまで制御できるかに基づいて分類します。

**自動テスト**は人の操作なしで実行します。入力はテストコードが生成し、期待値は
assertion で検証します。

**手動テスト**は、物理ハードウェア、ホスト OS 側の認識、配線、目視確認が検証の
本質に含まれる場合だけに使います。

```text
tests/
  unit/       自動 - descriptor builder と report helper。
  peer/       自動 - EspUsbHost host + EspUsbDevice device の2台構成。
  loopback/   自動 - ESP32-P4 1台で host / device role を同時実行。
  probe/      初期切り分け - port / speed / PHY / OS認識の確認。
  manual/     手動 - 物理デバイスまたは人の判断が必要。
```

## カバレッジ計画

| 機能 | unit | peer | loopback | probe | manual |
|------|------|------|----------|-------|--------|
| device descriptor config | 予定 | | | 予定 | |
| FS/HS endpoint MPS | 予定 | 予定 | 予定 | 予定 | |
| HID keyboard raw report | 予定 | MVP | MVP | | |
| HID keyboard LED output report | 予定 | MVP | MVP | | 任意 |
| HID mouse raw report | 予定 | MVP | 予定 | | |
| keyboard + mouse composite | 予定 | MVP | 予定 | | |
| custom HID report descriptor | 予定 | 予定 | | | |
| HID vendor IN/OUT/Feature | 予定 | 予定 | | | |
| consumer/system/gamepad HID | 予定 | 予定 | | | |
| CDC ACM | | 予定 | 予定 | | |
| USB MIDI | | 予定 | | | |
| USB MSC | | 予定 | | | |
| USB Audio | | 予定 | | | |

## 初期移行順

1. `unit/descriptor`
2. `peer/hid_keyboard`
3. `peer/hid_mouse`
4. `peer/hid_keyboard_mouse`
5. `probe/p4_device_fs_probe`
6. `probe/p4_device_hs_probe`
7. `loopback/hid_keyboard`
8. `peer/custom_hid`
9. `peer/hid_vendor`
10. `peer/hid_consumer_control`
11. `peer/hid_system_control`
12. `peer/hid_gamepad`
13. `peer/usb_serial`
14. `peer/usb_midi`
15. `peer/usb_msc`
16. `peer/usb_audio`

## 合格条件

- descriptor テストはログ確認ではなく byte 列を assert する。
- peer テストは serial command で device board の挙動を制御する。
- device sketch は Arduino-ESP32 標準の `USB.begin()` を呼ばない。
- P4 テストは selected port、requested speed、TinyUSB rhport、取得できる場合は
  connected speed、VID/PID、interface count、endpoint MPS を出力する。
- 未対応の P4 port / speed 組み合わせは、無言 skip ではなく `xfail` または probe
  結果として明示する。
