# EspUsbDevice MediaKeys

> English: [README.md](README.md)

USB HID consumer control / system control device を作り、音量、ミュート、再生停止などの
media key を Host へ送信する例です。

## ハードウェア

- USB device 対応の ESP32-S3 など Arduino-ESP32 board
- PC、EspUsbHost を動かす別 ESP32、またはテスト治具などの USB host
- ログ確認用の Serial monitor 接続

## 動作内容

- `EspUsbDevice` を full-speed HID media keys device として起動します。
- 3 秒ごとに Consumer Control usage を送信します。
- volume up / down、mute、play/pause、next / previous track を順番に送ります。
- System Control usage は Host の電源状態に影響するため、自動送信せずコード内コメントの例に留めています。

## 主要 API

- `EspUsbDeviceHidConsumerControl media(device)` は media key 用 HID function を登録します。
- `media.click(usage)` は press / release の組を送信します。
- `media.press(usage)` / `media.release()` で押下状態を明示制御できます。
- `media.sendUsage(usage)` は raw usage 状態を送信します。
- `EspUsbDeviceHidSystemControl systemControl(device)` は power / standby / wake などを扱います。

## Consumer Control Constants

| 定数 | 用途 |
|------|------|
| `ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_UP` | 音量を上げる |
| `ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_DOWN` | 音量を下げる |
| `ESP_USB_DEVICE_CONSUMER_CONTROL_MUTE` | ミュート |
| `ESP_USB_DEVICE_CONSUMER_CONTROL_PLAY_PAUSE` | 再生 / 一時停止 |
| `ESP_USB_DEVICE_CONSUMER_CONTROL_NEXT_TRACK` | 次のトラック |
| `ESP_USB_DEVICE_CONSUMER_CONTROL_PREVIOUS_TRACK` | 前のトラック |

## System Control Constants

| 定数 | 用途 |
|------|------|
| `ESP_USB_DEVICE_SYSTEM_CONTROL_POWER_OFF` | power off |
| `ESP_USB_DEVICE_SYSTEM_CONTROL_STANDBY` | standby / sleep |
| `ESP_USB_DEVICE_SYSTEM_CONTROL_WAKE_HOST` | wake |

## 注意

- media key の挙動は Host OS や active application に依存します。
- volume や play/pause は実際に Host に作用します。
- system control key は sleep / power などを発火する可能性があるため、自動送信しないでください。
- Serial monitor には送信した usage の概要が出ます。

## 関連

- [Keyboard](../Keyboard/) - HID keyboard device
- [Gamepad](../Gamepad/) - HID gamepad device
