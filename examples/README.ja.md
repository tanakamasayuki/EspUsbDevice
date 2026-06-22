# Examples

`EspUsbDevice` の基本的な使い方を確認するための Arduino sketches です。
このライブラリを使う sketch では Arduino-ESP32 標準の `USB.begin()`、
`USBHIDKeyboard`、`USBHIDMouse` は使いません。

## Keyboard

HID boot keyboard device の例です。

- `EspUsbDeviceConfig` で port、speed、VID/PID、string descriptor を設定します。
- `EspUsbDeviceHidKeyboard::pressUsage()` で HID usage ID を送信します。
- `onOutputReport()` で NumLock / CapsLock / ScrollLock などの LED 状態を受け取ります。

## Mouse

HID boot mouse device の例です。

- `EspUsbDeviceHidMouse::move()` で移動、wheel、button 状態を送信します。
- `click()` で press / release の組を送信します。
- left / right / middle / back / forward button を raw button bit として扱います。

## KeyboardMouse

keyboard と mouse を同時に登録する composite HID device の例です。
現時点の composite HID は、単一 HID interface と report ID で構成します。

- keyboard report ID: `1`
- mouse report ID: `2`
- composite HID endpoint MPS: `16 bytes`

## 注意

- USB device として使う側の ESP32-S3 などを USB host に接続してください。
- Serial monitor はログ確認用です。USB HID device としての接続先とは別の経路で確認してください。
- 既存の Arduino USB class と同時に使う設計ではありません。
