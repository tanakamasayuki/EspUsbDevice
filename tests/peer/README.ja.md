# ピアテスト

> English: [README.md](README.md)

`tests/peer` には2台構成の自動テストを置きます。

- Host board: EspUsbHost を実行する ESP32-S3。
- Device board: EspUsbDevice を実行する ESP32-S3。

device sketch は serial command で制御します。USB の挙動を決定的にし、host 側の
assertion を安定させるためです。

## ハードウェア接続

ホストボードとデバイスボードの USB data pin を接続します。

| Host board | Device board |
|------------|--------------|
| GPIO19 (D-) | GPIO19 (D-) |
| GPIO20 (D+) | GPIO20 (D+) |
| GND | GND |

両方のボードを PC などから別々に給電している場合、VBUS は接続しないでください。

## 初期テスト

- `hid_keyboard`: raw boot keyboard report と LED output report。S3 2台構成で通過済み。
- `hid_mouse`: raw boot mouse report。move / wheel / left / right / middle / back / forward が mouse callback として S3 2台構成で通過済み。
- `hid_keyboard_mouse`: keyboard + mouse composite device。S3 2台構成で通過済み。
- `custom_hid`: 固定 custom report descriptor と raw input。
- `hid_vendor`: interrupt IN/OUT と feature report。

以降のフェーズで consumer control、system control、gamepad、CDC ACM、MIDI、MSC、
Audio を追加します。
