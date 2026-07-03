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

## 実行メモ

`EspUsbHost` のリリース更新後や `s3_peer_host` / `s3_peer_local` の切り替え後は、
古い build cache が残ると起動時クラッシュや timeout に見えることがあります。
リリース確認では `--clean` を付けて実行してください。

```sh
uv run --env-file .env pytest peer/ --profile=s3_peer_host --clean
```

## 初期テスト

- `hid_keyboard`: raw boot keyboard report と LED output report。S3 2台構成で通過済み。
- `hid_mouse`: raw boot mouse report。move / wheel / left / right / middle / back / forward が mouse callback として S3 2台構成で通過済み。
- `hid_keyboard_mouse`: keyboard + mouse composite device。S3 2台構成で通過済み。
- `custom_hid`: 固定 custom report descriptor と raw input。
- `hid_vendor`: interrupt IN/OUT と feature report。
- `usb_serial`: CDC ACM serial。Device -> Host、Host -> Device、line coding callback が S3 2台構成で通過済み。
- `usb_midi`: USB MIDI。channel voice message と短い SysEx の Host -> Device packet 分割が S3 2台構成で通過済み。
- `usb_msc`: USB Mass Storage。単一 LUN RAM disk の capacity / inquiry / read / write / error path が S3 2台構成で通過済み。
- `usb_vendor`: vendor-specific interface。interface / bulk endpoint 列挙、bulk echo、application vendor control IN/OUT、WebUSB landing URL 読み出しが S3 2台構成で通過済み。
- `usb_audio`: USB Audio speaker sink。Host から Device への speaker PCM 受信が S3 2台構成で通過済み（UAC1 / FS）。
  P4 1台の `loopback/usb_audio` は同じ経路を再現するが `xfail`：P4 loopback は FS リンクなのに
  デバイスが UAC2 descriptor を出し、EspUsbHost が UAC1 のみ対応のため。

Audio の残作業は P4 の UAC1/UAC2 速度対応、microphone path、長時間再生、実音確認です。
