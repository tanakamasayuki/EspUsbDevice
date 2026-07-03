# Changelog / 変更履歴

## Unreleased
- (EN) **Breaking:** remove `EspUsbDeviceConfig::port` / `speed` and the `EspUsbDevicePort` / `EspUsbDeviceSpeed` enums. The device no longer selects its USB port/speed: on ESP32-P4 the Arduino core fixes the device to the high-speed (UTMI) controller and the actual link speed is negotiated with the host. Remove any `config.port` / `config.speed` assignments from sketches.
- (JA) **破壊的変更:** `EspUsbDeviceConfig::port` / `speed` と `EspUsbDevicePort` / `EspUsbDeviceSpeed` を削除しました。デバイスは USB ポート/速度を選択しません（ESP32-P4 では Arduino core がデバイスを High Speed(UTMI) コントローラに固定し、実速度はホストとのネゴで決まります）。スケッチから `config.port` / `config.speed` の代入を削除してください。
- (EN) Add the `tests/loopback/hid_keyboard_layout` test, verifying that EN_US / JA_JP symbol keys round-trip through the host on a single ESP32-P4.
- (JA) `tests/loopback/hid_keyboard_layout` を追加し、ESP32-P4 1台で EN_US / JA_JP の記号キーがホストまで往復することを検証します。
- (EN) Define USB Audio on ESP32-P4 as UAC2 / high-speed only, and remove the P4 loopback audio test: one-board loopback runs at full speed (single UTMI PHY), which cannot carry a UAC2 descriptor. Audio stays covered by the ESP32-S3 peer test (UAC1) plus manual high-speed checks.
- (JA) ESP32-P4 の USB Audio を UAC2 / High Speed 専用と定め、P4 の loopback audio テストを削除しました。1台 loopback は Full Speed（UTMI PHY は1個）で UAC2 記述子を扱えないためです。Audio は ESP32-S3 の peer テスト（UAC1）と実機 HS 手動確認でカバーします。
- (EN) Document ESP32-P4 USB port/PHY behavior and a known limitation: endpoint sizes are currently full-speed-fixed (bulk 64), so a P4 device on a real high-speed host is non-compliant for bulk (HS requires 512). The proper fix is per-speed descriptors, deferred to a real-PC / high-speed milestone. See `docs/DESIGN_NOTES.ja.md`.
- (JA) ESP32-P4 の USB ポート/PHY 挙動と既知制約を明記しました：endpoint サイズは現状 Full Speed 固定（bulk 64）で、実 High Speed ホストでは bulk が非準拠になります（HS は 512 必須）。正しい解決は per-speed descriptor で、実 PC / HS 対応マイルストーンに先送りします。詳細は `docs/DESIGN_NOTES.ja.md`。

## 1.0.0
- (EN) Initial release
- (JA) 初期リリース
