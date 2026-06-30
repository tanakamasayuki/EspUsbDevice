# 開発計画

この文書は、現在の実機・ツール環境を確認したうえで、`EspUsbDevice` の仕様と実装をどの順序で固めるかをまとめます。

## 確認済み環境

確認日: 2026-06-22

- `arduino-cli`: 1.5.0
- `uv`: 0.6.17
- pytest 実行環境: Python 3.13.3
- pytest plugin:
  - `pytest-embedded`: 2.8.1
  - `pytest-embedded-arduino-cli`: 1.2.2
- 接続ポート:
  - `/dev/ttyUSB0`: S3 peer host 想定
  - `/dev/ttyUSB1`: S3 peer device 想定
  - `/dev/ttyACM0`: P4 loopback / probe 想定

`arduino-cli config dump` は空設定で、通常の `arduino-cli core list` には `esp32:esp32` が表示されません。
このリポジトリでは `sketch.yaml` の `platforms` に Arduino-ESP32 3.3.10 と package index URL を明示し、`pytest-embedded-arduino-cli` 経由で build します。
直接 `arduino-cli compile --fqbn esp32:esp32:...` を実行する前提にはしません。

## 実行確認

`EspUsbDevice` 側:

```sh
cd tests
uv run --env-file .env pytest unit/compile_smoke --run-mode=build -vv
```

結果:

- Arduino-ESP32 3.3.10 を `sketch.yaml` 経由で解決。
- `EspUsbDevice.h` の公開型と定数を含む smoke sketch が build-only で通る。
- build-only mode のため pytest test body は skip される。これは期待どおり。

`EspUsbHost` 側:

```sh
cd tests
uv run --env-file .env pytest peer/hid_logic --run-mode=build -vv
```

結果:

- 既存 `EspUsbHost` の S3 build-only 経路が通る。
- build-only mode のため pytest test body は skip される。これは期待どおり。

## 開発の進め方

仕様は実機確認の結果に基づいて段階的に固定します。
先に大きな API を宣言しすぎず、各段階で unit / peer / loopback のどれで合格とするかを決めてから実装します。

`EspUsbHost` 側の peer テストは Arduino Core 標準 USB Device 実装を使う構成を維持し、
Host が `EspUsbDevice` に過適応しないようにします。このリポジトリでは released `EspUsbHost`
を使って詳細な Host / Device 相互テストを行い、Host 側の未リリース修正は必要時だけ
ローカル差し替えで任意確認します。`EspUsbHost` リリース後は対応バージョンを上げて
詳細テストを再実行します。

ローカル差し替え用に `s3_peer_local` と `p4_loopback_local` profile を用意します。
通常確認では default profile の released `EspUsbHost` だけを使います。local profile は
Host 側の未リリース修正をリリース前検証する場合だけ使い、`../EspUsbHost` checkout を参照します。

### Phase 0: 環境とリポジトリ基盤

目的:

- Arduino CLI / pytest / board profile / library resolution が動くことを確認する。
- CI、version bump、テストディレクトリ構造、設計メモを整える。

合格条件:

- `unit/compile_smoke --run-mode=build` が通る。
- `tests/.env.example` が peer / loopback 用 profile 名と対応している。
- `tests/TEST_PLAN.*` に初期移行順と合格条件がある。

### Phase 1: Descriptor 仕様

目的:

- USB descriptor をライブラリ所有にする。
- speed と class に応じた endpoint MPS を byte 列として固定する。

最初に仕様化するもの:

- `EspUsbDeviceConfig` から device descriptor への変換。
- FS / HS の configuration descriptor 差分。
- HID keyboard interface descriptor。
- HID mouse interface descriptor。
- EP0 MPS と interrupt endpoint MPS。

合格条件:

- `unit/descriptor` で device / configuration / HID report descriptor の byte 列を assert する。
- descriptor の期待値を README ではなくテストに置く。
- 実装が TinyUSB runtime に依存しない形で descriptor builder を単体実行できる。

確認結果:

- 2026-06-22: `unit/descriptor` を S3 実機で実行し、device / configuration / HID report descriptor の byte assertion が通過。
- HID keyboard interrupt IN/OUT と HID mouse interrupt IN の MPS は 8 bytes として固定。
- keyboard + mouse composite は、実機 runtime では単一 HID interface + report ID 構成に寄せる。
- この時点の `sendHidReport()` は TinyUSB runtime 未接続のため `ESP_ERR_NOT_SUPPORTED` を返す。peer 実装時に runtime 送信へ接続する。

### Phase 2: HID Keyboard Peer

目的:

- 既存 `EspUsbHost` の `peer/hid_keyboard` を Arduino-ESP32 `USBHIDKeyboard` 依存から外す。
- raw HID usage と output report callback を最初の実用 API として固める。

device serial command 案:

```text
SEND_KEY_USAGE 04
SEND_KEY_DOWN 04
SEND_KEY_UP 04
RELEASE_ALL
PRINT_LED
```

合格条件:

- host 側で key press / release を検出できる。
- NumLock / CapsLock / ScrollLock output report を device 側 callback で受けられる。
- device sketch は `USB.begin()` を呼ばない。

確認結果:

- 2026-06-22: `peer/hid_keyboard` を S3 2台構成で実行し、text input と LED output report が通過。
- device 側は Arduino-ESP32 標準 `USB.begin()` / `USBHIDKeyboard` を使わず、`EspUsbDevice` から TinyUSB runtime を起動。
- host 側で `HOST_CONNECTED vid=303a pid=4001` を確認。

### Phase 3: HID Mouse Peer

目的:

- boot mouse raw report API を固める。

device serial command 案:

```text
MOVE 40 0 0
MOVE 0 -40 0
WHEEL 1
CLICK 1
```

合格条件:

- host 側で移動、wheel、button press / release を検出できる。
- endpoint MPS が descriptor unit test と一致する。

確認結果:

- 2026-06-22: `peer/hid_mouse` を S3 2台構成で実行し、move / wheel / left / right / middle / back / forward が Host mouse callback で通過。

### Phase 4: Keyboard + Mouse Composite

目的:

- class 合成 API を固める。
- Arduino TinyUSB runtime で安定して列挙できるよう、初期実装では単一 HID interface + report ID にする。

合格条件:

- host 側で keyboard と mouse が同時に認識される。
- それぞれの callback / report が混線しない。

確認結果:

- 2026-06-22: `peer/hid_keyboard_mouse` を追加し、host / device sketch の build が通過。
- 2026-06-22: 複数 HID interface 構成では peer 実機で `CHECK_CONFIG` 中に EP0 STALL したため、keyboard report ID 1、mouse report ID 2 の単一 HID interface 構成へ変更。
- composite keyboard report は report ID を含めると 9 bytes になるため、composite HID endpoint MPS は 16 bytes とする。
- 2026-06-22: `unit/compile_smoke`、`peer/hid_keyboard_mouse` を含む実機テスト 7 件が通過。

### Phase 5: P4 Probe / Loopback

目的:

- P4 device port / speed / rhport / PHY の実際の制約を確認する。
- FS device + FS host loopback が可能かを判断する。

確認結果:

- 2026-06-22: `tests/loopback/hid_keyboard` smoke を追加。P4 1台で `EspUsbHost` と
  `EspUsbDeviceHidKeyboard` を同時起動し、Device 側の keyboard report を Host 側
  `onKeyboard()` で検出する構成。
- 2026-06-22: `tests/loopback/hid_keyboard` を peer 同等の text + LED output report
  coverage に拡張。
- 2026-06-22: `tests/loopback/hid_mouse` と `tests/loopback/hid_keyboard_mouse` を追加し、
  peer と同じ mouse move/buttons と keyboard+mouse composite を P4 1台構成で確認する構成にした。
- `arduino-cli compile --profile p4_loopback` は `hid_keyboard` / `hid_mouse` /
  `hid_keyboard_mouse` すべて通過。
- 2026-06-23: `loopback/hid_keyboard` / `hid_mouse` / `hid_keyboard_mouse` に
  `onHIDInput()` と `onHIDReportDescriptor()` の assert を追加し、Host 側 raw input /
  descriptor callback を検証。3 tests passed。
- 2026-06-23: `peer/hid_keyboard` / `hid_mouse` / `hid_keyboard_mouse` にも同じ Host 詳細
  assert を追加。`peer/hid_mouse` / `peer/hid_keyboard_mouse` は通過、`peer/hid_keyboard`
  は HID input ログが文字列出力に割り込まないよう Host sketch を buffer 出力に変更して通過。
- 2026-06-23: `EspUsbDeviceHidCustom` を追加し、`peer/custom_hid` で Host の
  `onHIDReportDescriptor()` と `onHIDInput()` を custom report descriptor / 8 byte input report
  で検証。1 test passed。
- 2026-06-23: `EspUsbDeviceHidVendor` を追加し、`peer/hid_vendor` で Host の
  `onVendorInput()`、`sendVendorFeature()`、`sendVendorOutput()` を検証。vendor HID endpoint
  MPS は report ID + 63 byte payload のため 64 bytes に設定。1 test passed。
- 2026-06-23: `EspUsbDeviceHidConsumerControl` と `EspUsbDeviceHidSystemControl` を追加し、
  `peer/hid_consumer_control` / `peer/hid_system_control` で Host の `onConsumerControl()` と
  `onSystemControl()` を検証。report ID 4 + 16bit usage、report ID 5 + 8bit usage の
  press/release が通過。2 tests passed。
- 2026-06-25: `EspUsbDeviceHidGamepad` を追加し、`peer/hid_gamepad` で Host の
  `onGamepad()` を検証。report ID 3 + 11 byte payload で axes / hat / buttons と
  HID field decode を確認。`s3_peer_local` で 3 tests passed。
- 2026-06-25: `loopback/hid_gamepad` を追加。P4 1台構成で gamepad raw input /
  `onGamepad()` / HID field decode を確認。default `p4_loopback` で 1 test passed。
- 2026-06-25: `EspUsbDeviceHidKeyboard::onProtocol()` を追加し、`peer/hid_keyboard` で
  Host の `sendSetProtocol()` が Device 側 TinyUSB Set_Protocol callback へ届くことを確認。
  default `s3_peer_host` で 3 tests passed。
- 2026-06-25: `peer/hid_keyboard_layout` を追加し、Host / Device の layout を `EN_US` /
  `JA_JP` に揃えた状態で記号キーの round-trip を検証。default `s3_peer_host` で
  2 tests passed。
- 2026-06-25: `EspUsbDeviceCdcSerial` を追加し、`peer/usb_serial` を作成。
  Device -> Host、Host -> Device、line coding callback を検証する構成にした。
  host / device sketch は build 通過。default `s3_peer_host` で 3 tests passed。
- 2026-06-25: `loopback/usb_serial` を追加し、P4 1台構成で CDC ACM の Device -> Host、
  Host -> Device、line coding callback を確認。CDC endpoint MPS は P4 FS Host が
  確保できる notification 8 bytes / bulk 64 bytes に修正。default `p4_loopback` で
  1 test passed。`peer/usb_serial` も再実行して 3 tests passed。
- 2026-06-25: `EspUsbDeviceMidi` を追加し、`peer/usb_midi` を作成。Device -> Host /
  Host -> Device の channel voice message と Host -> Device の短い SysEx packet 分割を確認。
  default `s3_peer_host` で 5 tests passed。
- 2026-06-25: `loopback/usb_midi` を追加し、P4 1台構成で USB MIDI の channel voice
  message と短い SysEx packet 分割を確認。default `p4_loopback` で 1 test passed。
- 2026-06-25: `EspUsbDeviceMsc` を追加し、`peer/usb_msc` を作成。単一 LUN RAM disk の
  capacity / inquiry / max LUN / sense / test unit ready / synchronize cache / read /
  write / multi-block / chunked transfer / out-of-range / failed write を確認。
  default `s3_peer_host` で 18 tests passed。
- 2026-06-25: `loopback/usb_msc` を追加し、P4 1台構成で USB MSC の単一 LUN RAM disk を確認。
  default `p4_loopback` で 1 test passed。`peer/usb_msc` も再実行して 18 tests passed。
- 2026-06-25: MSC のユーザー向け方針を整理。`EspUsbDeviceMscRamDisk` は raw block I/O /
  テスト / 一時 buffer、次段の `EspUsbDeviceMscFatRamDisk` は Host からの firmware /
  設定ファイル / Wi-Fi 転送用の一時ファイル受け渡し、`EspUsbDeviceMscSdCard` は実用的な
  永続ストレージ example とする。flash / SPIFFS / LittleFS の直接 MSC 公開は標準方針にしない。
- 2026-06-29: `EspUsbDeviceMscRamDisk` helper と `examples/MSC` を追加。raw block I/O の
  最小 MSC example とし、FAT でフォーマットされた USB drive ではないことを README に明記。
  `arduino-cli compile --profile s3 examples/MSC` は通過。
- 2026-06-29: `EspUsbDeviceMscFatRamDisk` helper と `examples/MSCFatRamDisk` を追加。
  RAM 上に小さい FAT12 image を生成し、Host から `CONFIG.TXT` を置いて eject 後に
  Device 側で `exists()` / `fileSize()` / `readFile()` する導線を作成。通常 DRAM に
  収まるよう example は 96 KB の RAM disk とした。`arduino-cli compile --profile s3
  examples/MSCFatRamDisk` は通過。
- 2026-06-29: `unit/fat_ramdisk` を追加し、FAT12 boot sector、volume label、root entry、
  FAT12 cluster chain、8.3 filename、duplicate / invalid name reject、zero-byte file、
  partial read、missing file、MSC attach、read/write callback、eject callback を検証。
  `uv run --env-file .env pytest unit/fat_ramdisk -vv` で 1 test passed。
- 2026-06-29: `EspUsbDeviceMscSdCard` helper と `examples/MSCSdCard` を追加。Arduino `SD`
  の `readRAW()` / `writeRAW()` を MSC read/write callback に接続する SPI SD card 用
  helper とし、Host 所有中は ESP32 側 file API を使わない方針を README / manual に明記。
  `arduino-cli compile --profile s3 examples/MSCSdCard` は通過。SD card 実機での Host OS
  mount / file write / eject は未確認。
- 2026-06-29: `tests/manual/README.*` に `examples/MSCFatRamDisk` と `examples/MSCSdCard` の
  手動確認手順を追加。PC mount、file copy/write、OS eject、Serial log の期待値を記載。
- 2026-06-29: examples のユーザー向けカバレッジを拡張。`examples/Serial`、`MIDI`、
  `Gamepad`、`MediaKeys`、`VendorHID`、`CustomHID`、`MIDIController`、`MIDIInterface` を
  追加し、各 README を日本語 / 英語で作成。MSC は準備コストが高く利用頻度も相対的に
  低いため、追加強化の優先度は下げる。
- 2026-06-29: `tests/examples_compile` を追加。`examples/*/*.ino` を列挙し、
  `arduino-cli compile --profile s3` で build-only 確認する。`uv run --env-file .env
  pytest examples_compile/ -vv` で examples 全体が通過。実行時間は約 3 分のため、push 時の
  CI ではなく、example 追加時 / API 変更時 / release 前の手動実行を基本方針にする。
- 2026-06-29: Arduino-ESP32 3.3.10 bundled USB examples と比較。対応状況と不足項目は
  [EXAMPLES_COVERAGE.ja.md](EXAMPLES_COVERAGE.ja.md) に整理した。
- 2026-06-30: USBVendor / WebUSB は実装予定に変更。公式 `USBVendor` 互換ではなく、
  `EspUsbDeviceVendor` として vendor-specific interface、bulk IN/OUT、control request callback
  を最小実装にする。WebUSB BOS / landing URL と Microsoft OS 2.0 descriptor は段階的に追加する。
- 2026-06-30: `EspUsbDeviceVendor` の最小実装と `examples/USBVendor` を追加。bulk IN/OUT と
  control request callback は対応済み。WebUSB BOS / landing URL と Microsoft OS 2.0 descriptor は
  未実装の後続項目として残す。`uv run --env-file .env pytest examples_compile/ -vv` は
  15 examples passed。

probe で必ず出すログ:

- selected device port
- requested device speed
- TinyUSB rhport
- connected speed が取れる場合はその値
- VID/PID
- interface count
- endpoint address / attributes / MPS

合格条件:

- `p4_device_fs_probe` と `p4_device_hs_probe` の結果を設計メモに反映する。
- 可能な組み合わせを loopback 自動テストへ昇格する。
- 不可能な組み合わせは `xfail` または probe 結果として明示する。

## 当面の優先順位

1. `unit/compile_smoke`、`unit/descriptor`、`unit/fat_ramdisk` を維持する。
2. HID keyboard / mouse / keyboard_mouse / custom / vendor / consumer / system / gamepad の
   peer / loopback coverage を維持する。
3. CDC ACM、USB MIDI、USB MSC raw block の peer / loopback coverage を維持する。
4. examples 全体の compile smoke を、example 追加時 / API 変更時 / release 前に手動実行する。
5. `examples/MSCFatRamDisk` を実機 PC mount で確認し、`CONFIG.TXT` copy -> OS eject ->
   Device 側 `readFile()` の結果を記録する。
6. `examples/MSCSdCard` を実機 SD card で確認し、Host OS mount / file write / OS eject /
   `SD_EJECT` log を記録する。Host 所有中に ESP32 側 file API を使わない排他方針も確認する。
7. SD_MMC 対応を追加するか判断する。Arduino `SD_MMC` の raw sector API が SPI `SD` と同じ
   形で使えるなら、`EspUsbDeviceMscSdMmc` または共通 raw block adapter を検討する。
8. USBVendor / WebUSB 相当を `EspUsbDeviceVendor` として育てる。最小実装の bulk IN/OUT +
   control request callback は追加済み。次は WebUSB BOS / landing URL、必要なら Microsoft OS 2.0
   descriptor を追加する。
9. Audio の移行可否を Host 側既存テストから確認し、最小 Audio sink の descriptor /
   endpoint / callback 仕様を先に固める。
10. P4 probe で FS / HS device 初期化方式を確定する。現状 loopback は実用テストが進んでいるが、
   probe 文書化は残っている。

Host 側で怪しい挙動が見つかった場合:

- Device 側 workaround で隠さない。
- Device 側は妥当な descriptor / report / control 応答を維持する。
- 再現用 peer / loopback sketch、期待値、実ログを残す。
- `EspUsbHost` 側の修正対象として切り出す。
- Device 側の回避は USB 仕様、Arduino-ESP32、TinyUSB runtime の制約として説明できる場合に限る。
