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

1. `unit/compile_smoke` を維持する。
2. `unit/descriptor` の仕様とテストを維持する。
3. HID keyboard peer を維持する。
4. HID mouse peer を維持する。
5. keyboard + mouse composite に広げる。
6. P4 loopback を peer と同等の keyboard / mouse / composite coverage に保つ。
7. Host 詳細挙動テストとして、追加 Device class なしで可能な `onHIDInput()` と
   `onHIDReportDescriptor()` の assert を loopback / peer に追加する。
8. custom/vendor HID Device class を追加し、Host の `sendHIDReport()`、
   `onVendorInput()`、HID parser field decode を検証する。
9. consumer control / system control / gamepad HID Device class を追加し、Host 側 callback を検証する。
10. P4 probe で FS / HS device 初期化方式を確定する。

Host 側で怪しい挙動が見つかった場合:

- Device 側 workaround で隠さない。
- Device 側は妥当な descriptor / report / control 応答を維持する。
- 再現用 peer / loopback sketch、期待値、実ログを残す。
- `EspUsbHost` 側の修正対象として切り出す。
- Device 側の回避は USB 仕様、Arduino-ESP32、TinyUSB runtime の制約として説明できる場合に限る。
