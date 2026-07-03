# Loopback テスト

> English: [README.md](README.md)

`tests/loopback` には、ESP32-P4 1台で EspUsbHost と EspUsbDevice を同時に動かす
テストを置きます。

最初の対象は HID keyboard loopback です。より広い class coverage に進む前に、
descriptor ログで P4 の port / speed 挙動を確認します。

## テスト

- `hid_keyboard`: P4 1台上で `EspUsbHost` と `EspUsbDeviceHidKeyboard` を同時に起動し、
  Device 側から `hello, keyboard` を送信して Host 側 `onKeyboard()` で確認する。
  Host 側から NumLock / CapsLock / ScrollLock / clear の LED output report も送信し、
  Device 側 `onOutputReport()` で確認する。
- `hid_mouse`: P4 1台上で `EspUsbHost` と `EspUsbDeviceHidMouse` を同時に起動し、
  move / wheel / left / right / middle / back / forward を Host 側 `onMouse()` で確認する。
- `hid_keyboard_mouse`: P4 1台上で keyboard + mouse composite device を起動し、
  keyboard report と mouse report が同時に Host 側 callback へ届くことを確認する。
- `custom_hid`: P4 1台上で custom HID report descriptor を持つ device を起動し、
  Host 側 `onHIDReportDescriptor()` と `onHIDInput()` で descriptor 長と raw report を確認する。
- `hid_vendor`: P4 1台上で HID vendor device を起動し、Device -> Host input、
  Host -> Device feature report、Host -> Device output report を確認する。
- `hid_consumer_control`: P4 1台上で consumer control device を起動し、
  volume / media key の press / release を Host 側 `onConsumerControl()` で確認する。
- `hid_system_control`: P4 1台上で system control device を起動し、
  power / standby / wake usage の press / release を Host 側 `onSystemControl()` で確認する。
- `hid_keyboard_layout`: P4 1台上で keyboard device を起動し、Host 側と Device 側の
  keyboard layout を揃えて切り替え、`EN_US` と `JA_JP` の記号キーが同じ ASCII として
  Host 側 `onKeyboard()` に届くことを確認する。
- `usb_serial`: P4 1台上で CDC ACM serial を起動し、Device -> Host、Host -> Device、
  line coding callback を確認する。
- `usb_midi`: P4 1台上で USB MIDI を起動し、channel voice message と短い SysEx の
  Host -> Device packet 分割を確認する。
- `usb_msc`: P4 1台上で USB Mass Storage を起動し、単一 LUN RAM disk の capacity /
  inquiry / read / write / error path を確認する。
- `usb_vendor`: P4 1台上で vendor-specific interface を起動し、bulk echo、application
  control IN/OUT、WebUSB landing URL 読み出しを確認する。
- `usb_audio`: P4 1台上で USB Audio speaker sink を起動し、Host 側の audio output を
  開始して speaker PCM を送信し、Device 側 `onData()` での受信を確認する。
  現状 P4 では `xfail`（FS リンクなのに UAC2 descriptor を出し、host が UAC1 のみ対応のため。下の P4 注記参照）。

## P4 ポート / PHY の実態（2026-07 実機確認）

P4 は OTG コントローラが2個あるが UTMI(HS) PHY は1個だけ
（`SOC_USB_OTG_PERIPH_NUM=2`, `SOC_USB_UTMI_PHY_NUM=1`）。Arduino core がデバイス
スタックを HS/UTMI PHY に固定しており、`EspUsbDeviceConfig.port` / `speed` は
`tinyusb_init` に渡っていない。速度とコントローラは別で、HS 対応デバイスでも相手が
FS ホストなら **Full Speed** でネゴして動く。

1台 loopback での帰結:

- デバイスは HS/UTMI PHY 上にいるが FS ホスト相手なので FS 動作。enumerate は正常。
- 唯一の UTMI PHY をデバイスが握るため、ホストは `ESP_USB_HOST_PORT_FULL_SPEED` 限定。
  ホストを HS にすると PHY 衝突（`usb_phy: selected PHY is in use`）。
- よって 1台では HS リンクは作れない。HS リンクの検証は2台構成の peer で行う。

## Matrix

| Device | Host | 期待 |
|--------|------|------|
| HS(UTMI) device / FS 動作 | FS host | 1台 loopback で実現できる構成。デバイスは HS PHY 固定だが FS でネゴ。 |
| HS device | HS host | 1台 P4 では不可。UTMI PHY を共有できず衝突。HS リンクは2台構成で。 |

> P4 の `usb_audio` は現状 fail で既知制約扱い。FS リンクなのに UAC2 descriptor
> （コンパイル時 `TUD_OPT_HIGH_SPEED`）を出し、EspUsbHost は UAC1 のみ解釈するため。
> 詳細は `docs/DESIGN_NOTES.ja.md`「P4 USB ポート/PHY の実測整理」。
