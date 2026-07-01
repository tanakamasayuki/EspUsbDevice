# 開発計画

この文書は、`EspUsbDevice` の開発方針、現在の到達点、今後の優先順位をまとめます。
リリース判断や次の作業に必要な最終状態だけを残します。

## 基本方針

`EspUsbDevice` は、Arduino-ESP32 標準 USB Device API との互換を目標にしません。
`EspUsbHost` と同じように、明示的な設定、descriptor 所有、callback ベース API、raw report /
raw transfer 制御を重視します。

開発は実ハードウェアで検証できる peer / loopback テストから進めます。これは、USB descriptor、
endpoint MPS、report ID、control transfer、Host 側 callback の挙動を再現性高く固定できるためです。
テスト向けの機能を先に実装しますが、最終目的は汎用的に使いやすい USB Device ライブラリを提供することです。

## Host 側との分担

`EspUsbHost` 側の peer テストは、原則として Arduino Core 標準 USB Device 実装を使う構成を維持します。
Host が `EspUsbDevice` だけに過適応することを避け、複数の Device 実装で Host の動作を確認するためです。

このリポジトリでは released `EspUsbHost` を使って Host / Device 間の詳細テストを行います。
Host 側に未リリース修正が必要な場合だけ、`s3_peer_local` / `p4_loopback_local` でローカル checkout を参照して任意確認します。
通常の合格条件は default profile の released Host です。

Host 側で怪しい挙動が見つかった場合は、Device 側 workaround で隠しません。
Device 側は USB device として妥当な descriptor / report / control 応答を維持し、再現用 sketch、
期待値、実ログを残したうえで `EspUsbHost` 側の修正対象として切り出します。
Device 側に回避を入れるのは、USB 仕様、Arduino-ESP32、TinyUSB runtime の制約として説明できる場合に限ります。

## 開発・テスト環境

このリポジトリは `sketch.yaml` の `platforms` に Arduino-ESP32 と package index URL を明示し、
`pytest-embedded-arduino-cli` 経由で build / upload / serial assertion を行います。
通常の `arduino-cli core list` やグローバル Arduino CLI 設定に依存しない構成を前提にします。

主なテスト構成:

- `unit/`: descriptor builder、report helper、FAT RAM disk など、Host 不要のテスト。
- `examples_compile/`: examples sketch の build-only smoke。
- `peer/`: ESP32-S3 2台構成。Host 側は `EspUsbHost`、Device 側は `EspUsbDevice`。
- `loopback/`: ESP32-P4 1台構成。Host role と Device role を同時に動かす。
- `manual/`: Host OS、browser、SD card、PC mount など、自動化しにくい確認。
- `probe/`: P4 port / speed / PHY などの初期切り分け。

ライブラリ更新後や release / local profile 切り替え後は、古い build cache による起動時クラッシュや timeout を避けるため、
`--clean` を付けて確認します。

```sh
cd tests
uv run --env-file .env pytest --clean
```

## 現在の実装範囲

初回リリース範囲として、以下を実装済みです。

- USB device core: port / speed / VID / PID / string / power / WebUSB URL 設定。
- descriptor builder: device / configuration / string / HID report descriptor。
- HID keyboard: layout、文字列 helper、raw usage、LED output report、protocol callback。
- HID mouse: move、wheel、button press / release。
- keyboard + mouse composite HID。
- custom HID descriptor。
- HID vendor input / output / feature report。
- consumer control / system control / gamepad。
- CDC ACM serial。
- USB MIDI event packet と note / control change などの helper。
- MSC raw block device と SCSI callback。
- MSC RAM disk、FAT RAM disk、SD card helper。
- HID ではない USBVendor bulk IN/OUT、control request、WebUSB landing URL。

USB Audio class は未実装です。descriptor、isochronous endpoint、alternate setting、sample format、
I2S bridge などの実装量が大きいため、初回リリースからは外し、別マイルストーンとして扱います。

## 確定した設計判断

### Descriptor と endpoint

descriptor はライブラリ側で所有します。Arduino-ESP32 標準 USB class の descriptor に依存しません。
FS / HS の endpoint MPS は unit test で byte 列として固定します。

HID keyboard / mouse 単体は boot protocol を意識した descriptor を提供します。
keyboard + mouse composite は、複数 HID interface ではなく、単一 HID interface + report ID 構成を採用します。
複数 interface 構成では一部の実機組み合わせで列挙が不安定になったため、初期実装では安定性を優先します。

### HID API

HID は文字列入力だけでなく、usage ID と raw report を第一級 API として扱います。
keyboard の文字列 helper は Host 側と同じ keymap table / layout ID を使い、Device 側では ASCII から usage へ逆変換します。

Host 側詳細テストでは、parsed callback だけでなく `onHIDInput()` と `onHIDReportDescriptor()` も確認します。
report ID、raw input bytes、descriptor callback を固定することで、Host parser の挙動も検証対象にします。

### CDC / MIDI / MSC

CDC ACM は Arduino らしい `available()` / `read()` / `write()` / `print()` と、line coding / line state callback の両方を提供します。

USB MIDI は 4 byte USB-MIDI event packet を基準にし、note / control change などの helper を追加します。
SysEx は packet 分割をテスト対象にします。

MSC は block device と filesystem を分けます。
raw `EspUsbDeviceMsc` は SCSI / block I/O の土台、`EspUsbDeviceMscRamDisk` はテストや一時 buffer、
`EspUsbDeviceMscFatRamDisk` は Host からの一時ファイル受け渡し、`EspUsbDeviceMscSdCard` は永続 storage 用です。
flash / SPIFFS / LittleFS の直接 MSC 公開は標準方針にしません。永続 storage は SD card、
一時ファイル受け渡しは FAT RAM disk を優先します。

### USBVendor / WebUSB

`EspUsbDeviceVendor` は HID vendor とは別の、HID ではない vendor-specific interface です。
bulk IN/OUT、stream-like API、EP0 vendor control request callback、WebUSB landing URL を扱います。

WebUSB / Microsoft OS 2.0 descriptor の基礎部分は Arduino-ESP32 TinyUSB core の仕組みを利用します。
`EspUsbDevice` 側で custom vendor code、GUID、Microsoft OS 2.0 descriptor 内容を差し替える API は未実装です。
browser / libusb / WinUSB の挙動は Host OS や driver 状態に依存するため、まず `tests/manual` の確認対象にします。

## 現在の検証範囲

通常のリリース確認は以下を基準にします。

```sh
cd tests
uv run --env-file .env pytest --clean
```

検証済みの主な範囲:

- `unit/compile_smoke`
- `unit/descriptor`
- `unit/fat_ramdisk`
- `examples_compile`
- `peer/hid_keyboard`
- `peer/hid_keyboard_layout`
- `peer/hid_mouse`
- `peer/hid_keyboard_mouse`
- `peer/custom_hid`
- `peer/hid_vendor`
- `peer/hid_consumer_control`
- `peer/hid_system_control`
- `peer/hid_gamepad`
- `peer/usb_serial`
- `peer/usb_midi`
- `peer/usb_msc`
- `peer/usb_vendor`
- `loopback/hid_keyboard`
- `loopback/hid_mouse`
- `loopback/hid_keyboard_mouse`
- `loopback/hid_gamepad`
- `loopback/usb_serial`
- `loopback/usb_midi`
- `loopback/usb_msc`
- `loopback/usb_vendor`

manual 確認に残す範囲:

- `examples/MSCFatRamDisk` の PC mount、file copy、OS eject、Device 側 file read。
- `examples/MSCSdCard` の SD card mount、Host OS read/write、OS eject。
- `examples/USBVendor` の browser / libusb / WinUSB からの claim、bulk echo、control request、WebUSB URL。

## 当面の優先順位

1. 初回リリース範囲の API と examples を安定化する。
2. `uv run --env-file .env pytest --clean` を release 前の基準にする。
3. examples 全体の compile smoke を API 変更時と release 前に維持する。
4. MSC FAT RAM disk / SD card / USBVendor の manual 確認手順を実機で消化する。
5. WebUSB / libusb / WinUSB の Host 側サンプルを追加するか判断する。
6. USBVendor の custom vendor code / Microsoft OS 2.0 descriptor 差し替え API を検討する。
7. FirmwareMSC は FAT RAM disk 上の `firmware.bin` を安全に扱う helper / example として検討する。
8. CDC + HID + MSC + Vendor などの all-in-one composite example を必要に応じて追加する。
9. USB Audio は最小 Audio sink / source の仕様を先に固め、別マイルストーンで実装する。
10. P4 probe の結果を整理し、port / speed / PHY の制約を設計メモへ反映する。
