# EspUsbDeviceBringUpCheck

> English: [README.md](README.md)

新しい board で **最初に動かす sketch** です。USB device が開始できるか、Host に認識されるかを順に確かめ、
認識されないときは確認項目の一覧を出します。

HID keyboard を登録しますが、キーは一度も送らないので、PC につないだままでも安全です。

## ハードウェア

- USB device に対応した Arduino-ESP32 board（ESP32-S2 / S3 / P4）
- PC などの USB host と、データ通信できる USB ケーブル
- ログ用の別の Serial monitor 接続（書込みに使う UART / USB Serial-JTAG 側）

## 動作内容

- target 名、arduino-esp32 の version、ライブラリの version を表示します。
- 指定した controller（`Auto` / `FullSpeed` / `HighSpeed`）と、実際に使う controller を表示します。
- `device.begin()` の成否と、失敗時は `lastErrorName()` とよくある原因を表示します。
- `begin()` 後に configuration descriptor の大きさと HID endpoint の大きさを表示します。
- `device.ready()` の変化で `MOUNTED` / `UNMOUNTED` を、`device.suspended()` の変化で `SUSPENDED` / `RESUMED` を表示します。
- 10 秒たっても認識されなければ、確認項目の一覧を一度だけ表示します。
- Host で CapsLock / NumLock を押すと `HOST_OUTPUT_REPORT` を表示します。これで **Host → device** の方向が動くことが分かります。

## 読み方

| 出力 | 意味 |
|------|------|
| `BEGIN failed` | Host は関係していません。board 側の問題です（descriptor が大きすぎる、endpoint の予算超過、未対応の target、不正な設定） |
| `BEGIN ok` だが `MOUNTED` が出ない | 電気的な問題（ケーブル、コネクタ、VBUS）か、Host が descriptor を受け取れていない |
| `MOUNTED` が出る | 列挙は成功。ここから先は class ごとの問題 |
| `MOUNTED` / `UNMOUNTED` を繰り返す | 電力不足、ケーブル不良、Host の再列挙 |
| `SUSPENDED` | Host が bus を休止した（スリープ、selective suspend）。構成は保たれ、`ready()` は true のまま |
| `HOST_OUTPUT_REPORT` が出る | 両方向の通信が動いている |

`BEGIN failed` のよくあるエラー：

- `ESP_ERR_INVALID_SIZE` - descriptor か endpoint の予算がこの controller に収まらない。Function を減らすか、ESP32-P4 なら HS controller を使います。
- `ESP_ERR_NOT_SUPPORTED` - この target、または指定した `controller` に USB device controller がない。
- `ESP_ERR_NO_MEM` - descriptor や Function の buffer を確保する heap が足りない。
- `ESP_ERR_INVALID_ARG` - `EspUsbDeviceConfig` の値か Function の設定が不正。
- `ESP_ERR_INVALID_STATE` - 既に開始している、または動作中に Function を登録した。

## ESP32-P4 の場合

`config.controller` の既定は `Auto` で、HighSpeed（UTMI PHY を持つ HS port）になります。ケーブルを FS 側のコネクタにつなぐ場合は、
sketch の `config.controller = EspUsbController::FullSpeed;` を有効にしてください。どのコネクタがどの PHY につながるかは board 次第なので、回路図で確認してください。

## Serial 出力の例

```text
=== EspUsbDevice bring-up check ===
TARGET ESP32-S3
CORE arduino-esp32 3.3.12
LIBRARY EspUsbDevice 3.0.0
CONTROLLER requested=Auto resolved=FullSpeed (Auto)
BEGIN ok
DESCRIPTOR config_bytes=34
VID_PID 303a:4050
Now connect the device connector to a host and watch for MOUNTED.
MOUNTED t=4120ms speed=full hid_packet=8
HOST_OUTPUT_REPORT leds=0x02 num=0 caps=1 scroll=0
STATUS mounted=1 suspended=0 t=9000ms leds=0x02
```

## 関連

- [EspUsbDeviceDescriptorDump](../EspUsbDeviceDescriptorDump/) - descriptor が疑わしいときに次に動かす
- [EspUsbDeviceConsole](../EspUsbDeviceConsole/) - 認識できたら手で転送を試す
- [docs/usb-device-guide.ja.md](../../../docs/usb-device-guide.ja.md) - 立ち上げの全手順
- [Core API（v3）](../../../docs/usb-device-advanced.ja.md) - controller と endpoint の上限

## 読み方と確認の手順

### 実行前の準備

Device内部の開始とホストによる認識完了を分ける。

上のハードウェアの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

USB Functionを変える前に、`beginOk`、readyとsuspendの変化、表示された構成を確認します。開始に成功してもreadyがfalseのままなら、data用コネクタ・ケーブル・ホストの列挙を確認します。ケーブルをつないだだけでは、構成済みのHID deviceにはなりません。

### APIの使い方と改造時の注意

開始（`started()`）・列挙（`ready()`）・interfaceの発見・実際のclass通信を、別々の確認点として扱います。descriptorはdeviceが何を提示するかを示すだけで、転送の完了やprotocolの実装を証明しません。interface番号・endpoint address・USB address・物理portはそれぞれ別のものを指します。

まず同じdeviceを直結して再接続で比べます。hubやFunctionの追加は、単純な場合を理解してからにします。エラー名と、それを出した操作を記録します。

### 一つずつ確認する

最初は例を変更せず、上の操作とログを照合します。ホストがない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。設定を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

## v2 からの変更点

- **交渉した bus 速度は表示しなくなりました。** v2 は TinyUSB の `tud_speed_get()` で読んでいましたが、v3 は TinyUSB を使わず、現在の速度を返す公開 API もありません。`Auto` がどの port を選ぶかは、引き続き `resolved=` の行で分かります。
- `tusb.h` を include しなくなりました。
- 新しく示すもの：`device.suspended()` による `SUSPENDED` / `RESUMED`、ライブラリの version、失敗時の説明の `INVALID_ARG` / `INVALID_STATE`。
- `onOutputReport()` は `esp_err_t` を返すので、結果を確認します。
- 確認項目の build の項は TinyUSB mode に触れず、Core の USB on boot 機能を使わないよう注意する内容にしました。

- [スケッチ](EspUsbDeviceBringUpCheck.ino)
- [全サンプル](../../README.md)
- [サンプルの進め方](../../../docs/example-workflow.ja.md)
