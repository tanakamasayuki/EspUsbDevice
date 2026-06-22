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

## Matrix

| Device | Host | 期待 |
|--------|------|------|
| FS device | FS host | SDK が許すなら最重要ターゲット。 |
| HS device | HS host | P4 high-speed 経路の baseline。 |
| FS device | HS host | probe / diagnostic 用。 |
| HS device | FS host | 失敗または未対応想定。明示的に記録する。 |
