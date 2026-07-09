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
- `hid_keyboard_nkro`: NKRO keyboard を EspUsbHost host で駆動。EspUsbHost リポジトリ側コピーとは観点を変え、押下キーの「数」ではなく「同一性(どのキーか)」を厳密検証し、高usage の International / LANG(JIS)キー(0x87-0x91)がすべて届くこと=ビットマップが 0x00-0xDF 全域を張ることを確認する。
- `custom_hid`: 固定 custom report descriptor と raw input。
- `hid_vendor`: interrupt IN/OUT と feature report。
- `usb_serial`: CDC ACM serial。Device -> Host、Host -> Device、line coding callback が S3 2台構成で通過済み。
- `usb_midi`: USB MIDI。channel voice message と短い SysEx の Host -> Device packet 分割が S3 2台構成で通過済み。
- `usb_msc`: USB Mass Storage。単一 LUN RAM disk の capacity / inquiry / read / write / error path が S3 2台構成で通過済み。
- `usb_vendor`: vendor-specific interface。interface / bulk endpoint 列挙、bulk echo、application vendor control IN/OUT、WebUSB landing URL 読み出しが S3 2台構成で通過済み。
- `usb_ncm`: USB CDC-NCM ネットワークデバイス（device）を EspUsbHost host（DUT）で駆動。EspUsbHost リポジトリ側のコピーとは観点を変え、(1) 列挙ディスクリプタの詳細（control/data インターフェース分離・active alt・方向付き3エンドポイント）、(2) トランスポート層のフレームカウンタ（1回の転送で双方向にフレームが動き TX 失敗ゼロ、DHCP リースがゲートウェイ .1 でない実クライアントアドレス）、(3) デバイス側視点（デバイス自身の web server が host のリクエストを処理したこと）を検証する。
- `usb_audio_speaker`: USB Audio speaker sink（Host → Device）。Host から Device への speaker PCM 受信が S3 2台構成で通過済み（UAC1 / FS）。
  `test_usb_audio_speaker_volume_flood` は、実 Windows で volume スライダーをドラッグしたときのように volume / mute の
  SET_CUR を高速連打し、デバイスが再起動せず動き続けることを検証する（実機不具合の再現テスト）。
  P4 の loopback 版は用意しない：P4 の Audio は UAC2 / High Speed 専用で、1台 loopback は Full Speed の
  ため原理的に噛み合わないため。P4 Audio(UAC2/HS) は実機 HS 手動確認でカバーする。

- `usb_audio_microphone`: USB Audio source（マイク、Device → Host）。device が生成した sawtooth を Host へストリームし、
  Host 側で入力ストリームを開始して device → Host の PCM が届き無音でないことを検証する。S3 2台構成の UAC1 / FS。

- `usb_audio_headset`: USB Audio headset（1台で speaker + microphone）。両方向を同時に検証する：OUT と IN の両ストリームが
  enumerate/開始でき、Host が送った speaker PCM を device が受信し、device の mic ストリームが Host に届き無音でないこと。
  S3 2台構成の UAC1 / FS。

Audio の残作業は長時間再生、実音確認、実マイク入力の取り込み、（任意で）UAC2 検証用の P4 2台 HS peer です。
