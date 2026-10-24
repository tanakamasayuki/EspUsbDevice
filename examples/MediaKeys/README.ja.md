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

## 読み方と確認の手順

### 実行前の準備

メディア操作を送り、System Control機能も宣言する。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`media.click()`が各stepで押下と解放を対にします。ホストの音量・再生の反応とUSB送信結果を分けて見ます。コメント化されたSystem Controlはホストの電源状態へ作用するため、実用途の明示的な操作へ結び付けるまでは無効のままにします。

### APIの使い方と改造時の注意

HID reportは構造化された状態や変化であり、任意のテキスト一行ではありません。report descriptorと合わせて、report ID・usage page/usage・符号・数値範囲を読みます。キーボードはキー位置、マウス移動は相対値、ボタンはbitmapです。実際の操作へつなぐ前に、押下と解放の対、または全状態の比較を確認します。

callbackのreportは借りたデータです。別taskへ渡す場合はbyte列や必要な値を自分の領域へコピーします。callbackでは短い診断に留め、生dumpの連続表示で端末速度が律速になることにも注意します。入力が来なければ、列挙、descriptor・report長、アプリ側の解釈の順に調べ、一度に全部を変えないようにします。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](MediaKeys.ino)
- [全サンプル](../README.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
