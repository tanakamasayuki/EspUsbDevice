# EspUsbDevice

EspUsbDevice は、新しい ESP32 Arduino USB Device ライブラリです。

Arduino-ESP32 標準の `USB`、`USBHIDKeyboard`、`USBHIDMouse` API との互換は
目標にしません。port、speed、descriptor、endpoint packet size、raw class
report をスケッチから明示的に制御できる、よりよい小さな USB Device ライブラリを
目指します。

最初の実装対象は `EspUsbHost` の peer / loopback テストです。これは実ハードウェアで
具体的に検証でき、ライブラリが制御すべき低レベル挙動を明確にできるためです。
テスト向けの機能を先に実装しますが、それはプロジェクトの最終的な範囲ではありません。

## 設計目標

- `EspUsbHost` と同じように、明示設定と callback ベースの API にする。
- Arduino USB class descriptor に依存せず、descriptor はこのライブラリで所有する。
- HID は文字入力ではなく usage ID と raw report を第一級 API にする。
- ESP32-S3 2台構成の peer テストと、ESP32-P4 1台構成の loopback テストを
  初期検証ターゲットとして支える。
- Arduino-ESP32 標準 USB Device stack とは排他利用にする。このライブラリを使う
  スケッチでは `USB.begin()` を呼ばない。

## 初期スコープ

最初のマイルストーンは、既存 `EspUsbHost` peer device を置き換え、実ハードウェアで
コア API を検証するための HID MVP です。

- device port / speed / VID / PID / string / power 設定。
- speed に応じた descriptor 生成と endpoint MPS 選択。
- HID boot keyboard の raw report 送信。
- HID keyboard output report callback による LED 状態受信。
- HID boot mouse の raw report 送信。
- pytest-embedded peer テスト用の serial command sketch。

## Examples

ユーザー向けの基本 sketch は [examples/README.ja.md](examples/README.ja.md) にまとめています。

- `Keyboard`: HID usage ID を送信する boot keyboard。
- `Mouse`: 移動、wheel、button を送信する boot mouse。
- `KeyboardMouse`: keyboard + mouse の composite HID。

テスト構造と段階的なカバレッジ計画は [tests/TEST_PLAN.ja.md](tests/TEST_PLAN.ja.md)
を参照してください。
設計背景と `EspUsbHost` 既存テストからの移行メモは [docs/DESIGN_NOTES.ja.md](docs/DESIGN_NOTES.ja.md)
にまとめています。
現在の実機・ツール環境に基づく開発順序は [docs/DEVELOPMENT_PLAN.ja.md](docs/DEVELOPMENT_PLAN.ja.md)
にまとめています。
