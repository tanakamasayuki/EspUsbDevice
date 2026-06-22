# EspUsbDevice Keyboard

> English: [README.md](README.md)

USB HID boot keyboard device を作り、文字列、1文字、raw HID usage ID、
keyboard LED output report を扱う例です。

## ハードウェア

- USB device 対応の ESP32-S3 など Arduino-ESP32 board
- PC、EspUsbHost を動かす別 ESP32、またはテスト治具などの USB host
- ログ確認用の Serial monitor 接続

## 動作内容

- `EspUsbDevice` を full-speed HID keyboard として起動します。
- `keyboard.write()` で layout 対応 ASCII 文字列を送信します。
- `keyboard.setLayout()` で `EN_US` と `JA_JP` などの layout を切り替えます。
- ASCII wrapper に含まれないキーは raw HID usage ID として送信します。
- Host から送られる NumLock / CapsLock / ScrollLock の LED output report を受け取ります。

## 主要 API

- `keyboard.setLayout(layout)` は文字から usage への変換 layout を選びます。
- `keyboard.write(text)` は null 終端 ASCII 文字列を送信します。
- `keyboard.tapKey(key)` は layout 対応の ASCII 1文字を押下して解放します。
- `keyboard.pressKey(key)` は layout 対応の ASCII 1文字を押下状態にし、
  `releaseAll()` または `releaseUsage()` まで保持します。
- `keyboard.tapUsage(usage, modifiers)` は raw HID usage を1回送信します。
- `keyboard.pressUsage(usage, modifiers)` / `keyboard.releaseUsage(usage)` は
  boot keyboard report を直接制御します。
- `keyboard.onOutputReport(callback)` は Host からの keyboard LED report を受け取ります。

## Layout

`EspUsbDeviceKeyboardLayout` は EspUsbHost と同じ数値 ID を使います。
Device 側は Host 側 keymap table を逆引きし、Host が `usage/modifier` から ASCII へ
変換するのと対になる形で ASCII から `usage/modifier` へ変換します。

対応 layout 定数:

| 定数 | ロケール | 備考 |
|------|----------|------|
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_ZH_TW` | zh_TW | 繁体字中国語、記号は US QWERTY |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_DA_DK` | da_DK | デンマーク語 |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_DE_DE` | de_DE | ドイツ語 QWERTZ |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US` | en_US | 英語 US、デフォルト |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_FI_FI` | fi_FI | フィンランド語 |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_FR_FR` | fr_FR | フランス語 AZERTY |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_HU_HU` | hu_HU | ハンガリー語 QWERTZ |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_IT_IT` | it_IT | イタリア語 |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP` | ja_JP | 日本語 |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_KO_KR` | ko_KR | 韓国語、記号は US QWERTY |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_NL_NL` | nl_NL | オランダ語 |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_NB_NO` | nb_NO | ノルウェー語 Bokmal |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_PT_BR` | pt_BR | ブラジルポルトガル語 |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_SV_SE` | sv_SE | スウェーデン語 |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_ZH_CN` | zh_CN | 簡体字中国語、記号は US QWERTY |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_GB` | en_GB | 英語 UK |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_PT_PT` | pt_PT | ポルトガル語 |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_ES_ES` | es_ES | スペイン語 |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_FR_CH` | fr_CH | スイスフランス語 |

## 想定 Serial 出力

```text
USB keyboard ready
LEDS num=0 caps=1 scroll=0 raw=0x02
last_leds=0x02
```

## 関連

- [KeyboardMouse](../KeyboardMouse/) - keyboard と mouse の composite device
- [Mouse](../Mouse/) - HID mouse device
