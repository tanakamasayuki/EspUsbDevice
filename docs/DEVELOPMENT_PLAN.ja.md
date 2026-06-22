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
cd /home/mt/dev/EspUsbDevice/tests
uv run --env-file .env pytest unit/compile_smoke --run-mode=build -vv
```

結果:

- Arduino-ESP32 3.3.10 を `sketch.yaml` 経由で解決。
- `EspUsbDevice.h` の公開型と定数を含む smoke sketch が build-only で通る。
- build-only mode のため pytest test body は skip される。これは期待どおり。

`EspUsbHost` 側:

```sh
cd /home/mt/dev/EspUsbHost/tests
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

### Phase 4: Keyboard + Mouse Composite

目的:

- class 合成 API を固める。
- 初期実装では keyboard と mouse を別 interface にする。

合格条件:

- host 側で keyboard と mouse が同時に認識される。
- それぞれの callback / report が混線しない。

### Phase 5: P4 Probe / Loopback

目的:

- P4 device port / speed / rhport / PHY の実際の制約を確認する。
- FS device + FS host loopback が可能かを判断する。

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
2. `unit/descriptor` の仕様とテストを追加する。
3. descriptor builder を実装する。
4. HID keyboard の device sketch と peer test を作る。
5. HID mouse と composite に広げる。
6. P4 probe で FS / HS device 初期化方式を確定する。
