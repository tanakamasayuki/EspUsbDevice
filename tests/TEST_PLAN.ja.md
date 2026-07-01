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

### EspUsbHost とのテスト分担

`EspUsbHost` 側の peer テストは、原則として Arduino Core 標準 USB Device 実装を使った
現状の構成を維持します。これは Host 側が `EspUsbDevice` だけに過適応することを避け、
複数の USB Device 実装に対して動作を確認するためです。

`EspUsbDevice` 側では、released `EspUsbHost` を使って Host / Device 間の詳細テストを行います。
report descriptor、report ID、output / feature report、複合 HID、raw input callback など、
Arduino Core 標準 USB Device では制御しづらい項目をこのリポジトリの peer / loopback で検証します。

`EspUsbHost` の未リリース修正が必要な場合は、切り分け目的でローカルの Host checkout を
差し替えて任意確認してよいです。ただし通常の合格条件は released `EspUsbHost` を対象にします。
`EspUsbHost` がリリースされたら、このリポジトリ側の対応 Host バージョンを上げて詳細テストを
再実行し、互換性を確認します。

このため、peer / loopback の default profile は released `EspUsbHost` を使います。
通常の確認では default profile だけを使います。`s3_peer_local` と `p4_loopback_local` は、
Host 側の未リリース修正をこのリポジトリでリリース前検証する場合だけ使います。
例: `uv run --env-file .env pytest peer/ --profile=s3_peer_local`、
`uv run --env-file .env pytest loopback/ --profile=p4_loopback_local`。

```text
tests/
  unit/       自動 - descriptor builder と report helper。
  examples_compile/ 自動 - examples sketch の build-only smoke。
  peer/       自動 - EspUsbHost host + EspUsbDevice device の2台構成。
  loopback/   自動 - ESP32-P4 1台で host / device role を同時実行。
  probe/      初期切り分け - port / speed / PHY / OS認識の確認。
  manual/     手動 - 物理デバイスまたは人の判断が必要。
```

## カバレッジ計画

| 機能 | unit | peer | loopback | probe | manual |
|------|------|------|----------|-------|--------|
| device descriptor config | ✅ `descriptor` | | | 予定 | |
| FS/HS endpoint MPS | ✅ `descriptor` | 予定 | 予定 | 予定 | |
| HID keyboard raw report | ✅ `descriptor` | ✅ `hid_keyboard` | build済み `hid_keyboard` | | |
| HID keyboard LED output report | ✅ callback変換 | ✅ `hid_keyboard` | build済み `hid_keyboard` | | 任意 |
| HID mouse raw report | ✅ descriptor | ✅ `hid_mouse` | build済み `hid_mouse` | | |
| keyboard + mouse composite | ✅ descriptor | ✅ `hid_keyboard_mouse` | build済み `hid_keyboard_mouse` | | |
| custom HID report descriptor | 予定 | ✅ `custom_hid` | | | |
| HID vendor IN/OUT/Feature | 予定 | ✅ `hid_vendor` | | | |
| consumer control HID | 予定 | ✅ `hid_consumer_control` | | | |
| system control HID | 予定 | ✅ `hid_system_control` | | | |
| gamepad HID | 予定 | ✅ `hid_gamepad` | ✅ `hid_gamepad` | | |
| CDC ACM | | ✅ `usb_serial` | ✅ `usb_serial` | | |
| USB MIDI | | ✅ `usb_midi` | ✅ `usb_midi` | | |
| USB MSC | ✅ `fat_ramdisk` | ✅ `usb_msc` | ✅ `usb_msc` | | |
| USBVendor / WebUSB | ✅ `descriptor` / compile | ✅ `usb_vendor` bulk/control/WebUSB URL | 予定 | | ✅ `examples/USBVendor` |
| USB Audio | | 予定 | | | |
| examples compile | ✅ `examples_compile` | | | | |

## EspUsbHost 詳細挙動テスト計画

このライブラリを作る理由の1つは、既存 USB device 実装では report descriptor、
report ID、output / feature report、複合 HID などの細かい挙動を十分に制御できず、
対応する `EspUsbHost` 側も細かい自動テストを作りづらかったことです。

`EspUsbDevice` では、Host 側の callback / parser / control transfer を意図的に刺激できる
device sketch を作り、Host 側の細かい挙動を peer / loopback の両方で検証します。

Host 側で怪しい挙動、仕様漏れ、parser の取りこぼし、callback 不整合が見つかった場合は、
Device 側の workaround で隠さない方針です。Device 側は USB device として妥当な descriptor /
report / control 応答を出し、再現できる peer / loopback sketch、期待値、実ログを残します。
そのうえで `EspUsbHost` 側の修正対象として切り出します。Device 側で回避を入れるのは、
USB 仕様上または Arduino-ESP32 / TinyUSB runtime 上の制約として説明できる場合に限ります。

### 追加実装なしで先に確認する項目

現在の keyboard / mouse / composite 実装だけで確認できる Host 側 API です。

| Host 側機能 | Device 側刺激 | 期待 |
|-------------|---------------|------|
| `onKeyboard()` | `keyboard.write()` / `tapKey()` / `pressUsage()` | `pressed` / `released` / `keycode` / `ascii` / `modifiers` が期待通り |
| keyboard layout | ✅ `hid_keyboard_layout` | `EN_US` / `JA_JP` で記号キーが期待通り |
| keyboard lock state | Host `setKeyboardLeds()`、Device `onOutputReport()` | NumLock / CapsLock / ScrollLock の output report を受けられる |
| `onMouse()` | `mouse.move()` / `wheel()` / `press()` / `release()` | `x` / `y` / `wheel` / `buttons` / `previousButtons` / `moved` / `buttonsChanged` が期待通り |
| `onHIDInput()` | keyboard / mouse input report | raw input report の address / interface / subclass / protocol / length / bytes が期待通り |
| `onHIDReportDescriptor()` | keyboard / mouse / composite descriptor | report descriptor の interface、reported length、実 length、先頭/末尾 byte が期待通り |
| report ID 分岐 | keyboard + mouse composite | report ID 1 keyboard、report ID 2 mouse が混線しない |

最初に追加する具体テスト:

1. ✅ `loopback/hid_keyboard` に `onHIDInput()` と `onHIDReportDescriptor()` の assert を追加する。
2. ✅ `loopback/hid_mouse` に raw input report byte と parsed mouse event の対応 assert を追加する。
3. ✅ `loopback/hid_keyboard_mouse` に report ID 1 / 2 の raw input assert を追加する。
4. ✅ 同じ観点を `peer/*` にも戻し、S3 2台構成と P4 1台構成の差を記録する。

### Device 側に小さな class 追加で確認する項目

| Host 側機能 | 必要な Device 側追加 | 期待 |
|-------------|----------------------|------|
| `sendSetProtocol()` | ✅ `hid_keyboard` の `onProtocol()` | report protocol request を観測できる |
| `sendHIDReport(... OUTPUT)` | ✅ `hid_vendor` | Host からの Output report の report ID / payload / length を検証 |
| `sendHIDReport(... FEATURE)` | ✅ `hid_vendor` | Host からの Feature report を検証 |
| `onVendorInput()` | ✅ `hid_vendor` | report ID 6 の vendor input が callback へ届く |
| HID parser fields | ✅ `custom_hid` の descriptor / input から開始 | usage page / usage / bit offset / bit size / logical min/max を検証 |

### HID class 追加で確認する項目

| Host 側機能 | 必要な Device 側 class | 期待 |
|-------------|------------------------|------|
| `onConsumerControl()` | ✅ `hid_consumer_control` | play/pause、mute、volume、next/previous が press/release で届く |
| `onSystemControl()` | ✅ `hid_system_control` | power / sleep が press/release で届く |
| `onGamepad()` | ✅ `hid_gamepad` | fields / fieldCount / changed / rawData / reportData が期待通り |

### HID 以外の Host 詳細テスト

CDC ACM、MIDI、MSC、Audio は Device 側 class 実装が必要です。HID の詳細テストが安定してから、
Host 側既存 `peer/usb_serial`、`peer/usb_midi`、`peer/usb_msc`、`peer/usb_audio` 相当を
EspUsbDevice 実装へ置き換えます。

`peer/usb_serial` は `EspUsbDeviceCdcSerial` の最初の CDC ACM テストです。Host 側は
released `EspUsbHost` の `EspUsbHostCdcSerial` を使い、Device -> Host、Host -> Device、
line coding callback を検証します。default profile は released Host を使い、
`s3_peer_local` は Host 側未リリース修正の任意確認にだけ使います。

`loopback/usb_serial` は同じ観点を P4 1台構成で確認します。CDC endpoint MPS は
FS Host で確保できるよう notification 8 bytes、bulk data 64 bytes とします。

`peer/usb_midi` は `EspUsbDeviceMidi` の最初の USB MIDI テストです。Device -> Host /
Host -> Device の channel voice message と、Host -> Device の短い SysEx packet 分割を
検証します。default profile は released Host を使い、`s3_peer_local` は Host 側未リリース
修正の任意確認にだけ使います。

`loopback/usb_midi` は同じ観点を P4 1台構成で確認します。SysEx は複数 packet が同じ
poll で読めるため、packet 順の逐次待ちではなく、必要な packet を両方観測したことを
assert します。

`peer/usb_msc` は `EspUsbDeviceMsc` の最初の USB Mass Storage テストです。単一 LUN の
RAM disk として、capacity / inquiry / max LUN / sense / test unit ready / synchronize cache /
read / write / multi-block / chunked transfer / out-of-range / failed write を検証します。

`loopback/usb_msc` は同じ観点を P4 1台構成で確認します。RAM disk の全体容量は
16 blocks x 512 bytes とし、chunked transfer では Host 側の 4096 byte chunk 境界も確認します。

`EspUsbDeviceVendor` は HID ではない vendor-specific interface として、descriptor unit test、
`examples/USBVendor` の build-only 確認、`peer/usb_vendor` の interface / bulk endpoint 列挙、
bulk OUT -> Device -> bulk IN echo、application vendor control IN/OUT、WebUSB landing URL 読み出しを
確認します。default profile は released Host を使い、`s3_peer_local` は Host 側未リリース修正の
任意確認にだけ使います。Microsoft OS 2.0 descriptor は Host OS / browser / driver の影響が
大きいため、まず `tests/manual` の手順で確認します。

MSC の transport テストとファイル受け渡しテストは分けます。`peer/usb_msc` /
`loopback/usb_msc` は raw block I/O、SCSI、error path を確認するテストとして維持します。
FAT や SD の使いやすさは別テストにします。

`EspUsbDeviceMscFatRamDisk` を追加したら、最初は Host が mount できる小さい FAT image と、
eject / stop 後に ESP32 側で 8.3 filename の file を scan / read できることを確認します。
firmware update や Wi-Fi 転送は、まず「Host が書いた file を eject 後に Device 側が
byte 列として取り出せる」ことを合格条件にします。

`unit/fat_ramdisk` は、Host mount 前に FAT12 image の基本構造、root entry、cluster chain、
`exists()` / `fileSize()` / `readFile()`、MSC attach と eject callback を検証します。
PC mount / file copy / OS eject は別途 manual または peer テストで確認します。

`EspUsbDeviceMscSdCard` は SD card を block device として公開する手動または任意自動テストから
始めます。Host が MSC として SD を所有している間、ESP32 側は同じ filesystem を mount /
書き込みしないことを仕様条件にします。flash / SPIFFS / LittleFS の直接 MSC 公開は標準
テスト対象にしません。
`examples/MSCSdCard` は build を通すことを最低条件とし、Host OS mount / file write /
eject は `tests/manual` の手順で確認します。

`examples_compile` は `examples/*/*.ino` を列挙し、各 sketch を `arduino-cli compile
--profile s3` で build-only 確認します。examples はユーザー向け API の入口なので、
peer / loopback の実機テストとは別に、全 example が常にコンパイルできることを合格条件にします。

## 初期移行順

1. `unit/compile_smoke`
2. `unit/descriptor`
3. ✅ `peer/hid_keyboard`
4. ✅ `peer/hid_mouse`
5. ✅ `peer/hid_keyboard_mouse`
6. `probe/p4_device_fs_probe`
7. `probe/p4_device_hs_probe`
8. ✅ `loopback/hid_keyboard`
9. ✅ `loopback/hid_mouse`
10. ✅ `loopback/hid_keyboard_mouse`
11. ✅ `peer/hid_keyboard_layout`
12. ✅ `peer/custom_hid`
13. ✅ `peer/hid_vendor`
14. ✅ `peer/hid_consumer_control`
15. ✅ `peer/hid_system_control`
16. ✅ `peer/hid_gamepad`
17. ✅ `loopback/hid_gamepad`
18. ✅ `peer/usb_serial`
19. ✅ `loopback/usb_serial`
20. ✅ `peer/usb_midi`
21. ✅ `loopback/usb_midi`
22. ✅ `peer/usb_msc`
23. ✅ `loopback/usb_msc`
24. ✅ `peer/usb_vendor` bulk/control/WebUSB URL
25. `loopback/usb_vendor`
26. `peer/usb_audio`

## 合格条件

- descriptor テストはログ確認ではなく byte 列を assert する。
- `unit/compile_smoke` は build-only で Arduino CLI、sketch.yaml、ESP32 board package、ライブラリ解決を確認する。
- peer テストは serial command で device board の挙動を制御する。
- device sketch は Arduino-ESP32 標準の `USB.begin()` を呼ばない。
- P4 テストは selected port、requested speed、TinyUSB rhport、取得できる場合は
  connected speed、VID/PID、interface count、endpoint MPS を出力する。
- 未対応の P4 port / speed 組み合わせは、無言 skip ではなく `xfail` または probe
  結果として明示する。
