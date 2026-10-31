# EspUsbDevice MediaKeys

> English: [README.md](README.md)

USB HID の Consumer Control / System Control device を作り、音量、ミュート、再生/一時停止などのメディアキーを Host へ送る例です。

## ハードウェア

- ESP32-S3、ESP32-S2、ESP32-P4 の board。native USB OTG の口を Host へつなぐ
- PC、EspUsbHost を動かす別の ESP32、試験治具などの USB Host
- ログ用の `Serial`（115200）の別の debug 接続

## 動作内容

- Consumer Control と System Control の Function で `EspUsbDevice` を開始します。2つは1つの HID interface にまとめられます。
- Host が構成を済ませている間、3秒ごとに Consumer Control の usage を1つ送ります。
  音量を上げる / 下げる、ミュート、再生/一時停止、次の曲、前の曲を順に回します。
- System Control の usage は Host の電源状態に影響するので、コメントにした例として残しています。

## 主要 API

- `EspUsbDeviceHidConsumerControl media(device)` はメディアキーの Function を登録します。
- `media.click(usage)` は押下と解放を対で送ります。
- `media.press(usage)` / `media.release()` は押した状態を明示的に操作します。
- `media.sendUsage(usage)` は usage の状態をそのまま送ります。`media.usage()` はその値を返します。
- `EspUsbDeviceHidSystemControl systemControl(device)` は電源断 / スタンバイ / 復帰の usage を扱います。
  メソッドは同じく `press` / `release` / `click` / `sendUsage` / `usage` です。

## Consumer Control の定数

| 定数 | usage | 用途 |
|------|-------|------|
| `ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_UP` | `0x00e9` | 音量を上げる |
| `ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_DOWN` | `0x00ea` | 音量を下げる |
| `ESP_USB_DEVICE_CONSUMER_CONTROL_MUTE` | `0x00e2` | ミュート |
| `ESP_USB_DEVICE_CONSUMER_CONTROL_PLAY_PAUSE` | `0x00cd` | 再生 / 一時停止 |
| `ESP_USB_DEVICE_CONSUMER_CONTROL_NEXT_TRACK` | `0x00b5` | 次の曲 |
| `ESP_USB_DEVICE_CONSUMER_CONTROL_PREVIOUS_TRACK` | `0x00b6` | 前の曲 |

Consumer ページの他の usage も、`0x000`〜`0x3FF` ならそのまま渡せます。report descriptor がこの範囲（Logical Maximum 1023）を宣言しているので、それより大きい値は `ESP_ERR_INVALID_ARG` で失敗し、送りません。

## System Control の定数

| 定数 | usage | 用途 |
|------|-------|------|
| `ESP_USB_DEVICE_SYSTEM_CONTROL_POWER_OFF` | `0x81` | 電源断 |
| `ESP_USB_DEVICE_SYSTEM_CONTROL_STANDBY` | `0x82` | スタンバイ / スリープ |
| `ESP_USB_DEVICE_SYSTEM_CONTROL_WAKE_HOST` | `0x83` | 復帰 |

それ以外の値（解放の 0 を除く）は false で拒否し、`lastError()` は `ESP_ERR_INVALID_ARG` になります。`WAKE_HOST` は普通の HID 入力で、bus の休止中に USB の remote wakeup を行うものではありません。

## 想定 Serial 出力

```text
USB media keys ready
MEDIA_KEY volume up usage=0x00e9
MEDIA_KEY volume down usage=0x00ea
MEDIA_KEY mute usage=0x00e2
```

## 注意

- メディアキーの動作は Host の OS と、操作対象のアプリで変わります。USB の送信結果とは別に確かめます。
- 音量と再生/一時停止は Host に作用します。
- System Control の usage を自動で送らないでください。スリープや電源断が起きることがあります。利用者の明示的な操作につないでください。

## v2 からの変更点

- `device.ready()` が true の間だけ送ります。
- System Control は Generic Desktop の標準 usage ID（`0x81`〜`0x83`）を受け取り、`usage()` もその値を返すようになりました。
  定数名は変わらないので、定数で書いたコードはそのまま動きます。整数 1/2/3 を渡していたコードや、`usage()` を 1/2/3 と比べていたコードは定数に直してください。
  descriptor も実際に Power Down / Sleep / Wake Up を広告するようになりました（v2 は usage 0〜3 を広告していた）。
- Consumer Control で `0x3FF` より大きい usage は `ESP_ERR_INVALID_ARG` で失敗するようになりました。v2 は descriptor で表せない値も送っていました。範囲内の usage は変わりません。

## 関連

- [Keyboard](../Keyboard/) - HID keyboard
- [Gamepad](../Gamepad/) - HID gamepad
- [Remote wakeup](../../docs/usb-device-advanced.ja.md)（suspend と remote wakeup）
- [HID API ガイド](../../docs/classes/hid.ja.md)
- [全サンプル](../README.md)
