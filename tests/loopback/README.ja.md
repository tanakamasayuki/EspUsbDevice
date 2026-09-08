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
  Device 側 `onOutputReport()` で確認する。Device HS + Host FSの後、Device FS + Host HSへ
  controllerを反転して両構成を検証する。
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
- `usb_serial_multi`: P4 1台上で CDC ACM を 3 ポート持つ device を high-speed controller
  側で起動する。descriptor unit test は「バイト列が妥当か」までしか見ないが、3 ポートは
  非 control IN endpoint を 6 本使い、IN 1 本ごとに専用 TxFIFO を controller の 1024 word
  から切り出す必要がある。この割り当てが成功するかは SET_CONFIGURATION 時に dcd_dwc2 が
  決めるので、実際に列挙させないと分からない。Host 側で interface 6 / endpoint 9
  （interrupt IN 3 + bulk 6）・アドレス重複なし・`class=ef` を確認し、port 0 の双方向通信と、
  port 1 / 2 へ書いたバイトが port 0 の経路に出てこないことを確認する。port 1 / 2 を host 側から駆動する検証は
  `ESP_USB_HOST_MAX_SERIAL_PORTS` を 3 にする必要があり、その設定方法の仕様が固まるまで保留
  （EspUsbHost 側の複数ポート bind 自体は対応済みで、2 ポートの `peer/usb_serial_multi` は
  per-port で検証済み）。
- `usb_midi`: P4 1台上で USB MIDI を起動し、channel voice message と短い SysEx の
  Host -> Device packet 分割を確認する。
- `usb_msc`: P4 1台上で USB Mass Storage を起動し、単一 LUN RAM disk の capacity /
  inquiry / read / write / error path を確認する。
- `usb_vendor`: P4 1台上で vendor-specific interface を起動し、bulk echo、application
  control IN/OUT、WebUSB landing URL 読み出しを確認する。
- `usb_audio`: Host側Audio PeerがUAC1中心の間は後回しにする。P4 Device Audioも他targetと
  同じくUAC1 defaultで、UAC2は明示選択する。

## P4 ポート / PHY の実態（2026-07 実機確認）

P4 は OTG コントローラが2個あるが UTMI(HS) PHY は1個だけ
（`SOC_USB_OTG_PERIPH_NUM=2`, `SOC_USB_UTMI_PHY_NUM=1`）。EspUsbDeviceはTinyUSB runtimeを
所有し、`FullSpeed`をrhport 0/internal PHY、`HighSpeed`をrhport 1/UTMIへmapする。
EspUsbHostは空いているもう一方のcontrollerを独立して選択できる。

1台 loopback での帰結:

- Device HS + Host FSは有効で、HostがFSなのでFS linkになる。
- Device FS + Host HSも有効で、DeviceがFSなのでFS linkになる。
- Device HS + Host HSはrhport 1/UTMIを共有するため不可。
- Device FS + Host FSはrhport 0を共有するため不可。
- よって1台ではHS linkは作れず、HS link検証は2台構成のPeerで行う。

## Matrix

| Device | Host | 期待 |
|--------|------|------|
| HS/UTMI device | FS host | 対応。FSでネゴする。 |
| FS device | HS/UTMI host | 対応。FSでネゴする。 |
| HS device | HS host | 1台P4ではrhport 1/UTMIが衝突するため不可。 |
| FS device | FS host | 1台P4ではrhport 0が衝突するため不可。 |
