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

## 読み方と確認の手順

### 実行前の準備

Device内部の開始とホストによる認識完了を分ける。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

classを変える前にbeginOk・readyの変化・表示された設定を見ます。開始成功でもreadyがfalseならデータ端子・ケーブル・ホストの列挙を調べます。物理的に接続しただけでは、設定済みHID機器にはなりません。

### APIの使い方と改造時の注意

内部開始、列挙、interfaceの発見、classの実通信を別の確認点にします。descriptorは相手が提示した内容であり、実転送やprotocolの実装を保証しません。descriptorの走査を改造するときも受信長と容量検査を残します。interface番号・endpoint address・USB address・物理portは別の識別子です。

最初は同じ機器の直接接続と再接続を比べ、理解してからhubや機能を増やします。異常ではエラー名と失敗した操作を一緒に控えます。大量dumpは診断用であり、高頻度callbackの処理例としてそのまま使わないようにします。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](EspUsbDeviceBringUpCheck.ino)
- [全サンプル](../../README.ja.md)
- [サンプルの進め方](../../../docs/example-workflow.ja.md)
