# EspUsbDeviceBringUpCheck

> English: [README.md](README.md)

新しいボードで**最初に動かす**スケッチです。USB device が起動するか、Host が列挙するか、
どの速度でつながったかを順番に確認し、列挙されない場合はチェックリストを表示します。

HID keyboard として登録しますが、キーは一切送りません。PC に挿したままにしても安全です。

## ハードウェア

- USB device 対応の Arduino-ESP32 board（ESP32-S2 / S3 / P4）
- PC などの USB host と、データ通信対応の USB ケーブル
- ログ確認用の Serial monitor 接続（書き込みに使う UART / USB Serial-JTAG 側）

## 動作内容

- ターゲット名と arduino-esp32 のバージョンを表示します。
- 要求した controller（`Auto` / `FullSpeed` / `HighSpeed`）と、実際に選ばれる controller を表示します。
- `device.begin()` の成否を表示し、失敗時は `lastErrorName()` と代表的な原因を表示します。
- `device.ready()` の変化を `MOUNTED` / `UNMOUNTED` として表示します。
- `MOUNTED` 時にネゴシエートされた速度（Full / High）を表示します。
- 10 秒経っても列挙されなければ、切り分け用のチェックリストを 1 回だけ表示します。
- Host 側で CapsLock / NumLock を押すと `HOST_OUTPUT_REPORT` が出ます。
  これは **Host → Device 方向が通っている**ことの証明になります。

## 読み方

| 表示 | 意味 |
|------|------|
| `BEGIN failed` | Host は無関係。ボード側の問題（descriptor が大きすぎる、endpoint 予算超過、ターゲット非対応など） |
| `BEGIN ok` のまま `MOUNTED` が出ない | 電気的な問題（ケーブル、コネクタ、VBUS）か、Host が descriptor を受け取れていない |
| `MOUNTED` が出る | 列挙成功。ここから先はクラスごとの問題 |
| `MOUNTED` / `UNMOUNTED` を繰り返す | 電源不足、ケーブル不良、Host 側の再列挙 |
| `HOST_OUTPUT_REPORT` が出る | 双方向で通信できている |

`BEGIN failed` の代表的なエラー:

- `ESP_ERR_INVALID_SIZE` — descriptor か endpoint 予算がこの controller に収まらない。
  クラスを減らすか、ESP32-P4 なら HighSpeed controller を使う。
- `ESP_ERR_NOT_SUPPORTED` — このターゲットに使える USB device controller がない。
- `ESP_ERR_NO_MEM` — descriptor buffer 用の heap が足りない。

## ESP32-P4 の場合

`config.controller` は既定で `Auto` = HighSpeed（rhport 1、外部 UTMI PHY）です。
FS コネクタに挿す場合は、スケッチ内の `config.controller = EspUsbController::FullSpeed;`
のコメントを外してください。どのコネクタがどちらに配線されているかはボード回路図で確認します。

## 想定 Serial 出力

```text
=== EspUsbDevice bring-up check ===
TARGET ESP32-S3
CORE arduino-esp32 3.3.11
CONTROLLER requested=Auto resolved=FullSpeed (Auto)
BEGIN ok
DESCRIPTOR config_bytes=34 hid_endpoint_size=8
VID_PID 303a:4050
Now connect the device connector to a host and watch for MOUNTED.
MOUNTED t=4120ms speed=Full (12 Mbps)
HOST_OUTPUT_REPORT leds=0x02 num=0 caps=1 scroll=0
STATUS mounted=1 t=9000ms leds=0x02
```

## 関連

- [EspUsbDeviceDescriptorDump](../EspUsbDeviceDescriptorDump/) — 列挙されない原因が descriptor 側にある場合に次に動かす
- [EspUsbDeviceConsole](../EspUsbDeviceConsole/) — 列挙後に手打ちで転送を試す
- [docs/usb-device-guide.ja.md](../../../docs/usb-device-guide.ja.md) — 実験の進め方全体
