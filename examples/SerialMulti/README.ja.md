# EspUsbDevice SerialMulti

> English: [README.md](README.md)

1 台の device に独立した USB CDC ACM serial port を複数作る例です。Host からは port ごとに
COM port（Windows）または `/dev/ttyACM*`（Linux）が見え、ring、line coding、DTR の状態はそれぞれ別です。

機械向けの protocol と人が読む console を分け、ログ出力が命令の stream を壊さないようにする、といった用途を想定しています。

## ハードウェア

- native USB OTG port を持つ ESP32-S2 / S3 / P4 board
- USB host：PC、または EspUsbHost を動かす別 ESP32
- ログ確認用の Serial monitor 接続

## 動作内容

- `Console` と `Data Link` という名前の `EspUsbDeviceCdcSerial` を 2 つ登録します。ESP32-P4 では HS controller に余裕があるので、`Telemetry` を加えた 3 つにします。
- port ごとに `configure()` で ring の大きさを変えます（RX/TX：Console 128/1024、Data Link 1024/1024、Telemetry 64/2048 byte）。
- どの port で受けた文字も、port 名を付けてその port へ返します。
- 3 秒ごとに、開いている port へ `tick=...` を送ります。
- 各 port の DTR / RTS と baud の変化、開始時の port 番号と実効容量を通常の Serial monitor へ出力します。

## 使い方

1. sketch を書き込み、通常の Serial monitor を開きます。
2. USB device port を Host に接続します。
3. serial port が 2 つ（P4 では 3 つ）現れるので、開きます。
4. どれかに入力すると、その port にだけ返事が返り、Serial monitor にどの port が受けたかが出ます。

## 何 port まで置けるか

CDC port は 1 つにつき **IN endpoint を 2 本**（通知用と data 用）使います。これらの controller では IN endpoint が足りなくなりやすい資源です。

| Controller | 非 control の IN endpoint | CDC だけ | HID + Vendor と同居 |
|---|---|---|---|
| ESP32-S2 / S3 | 4 | 2 port | 1 port |
| ESP32-P4 FS controller | 4 | 2 port | 1 port |
| ESP32-P4 HS controller | 7 | 3 port | 2 port |

`begin()` が構成全体を検査します。収まらない数を登録すると、USB PHY を始める前に `ESP_ERR_INVALID_SIZE` で失敗します（`device.lastError()` / `lastErrorName()` で読めます）。そのため Host が壊れた device を見ることはありません。endpoint の予算は構成内のすべての Function で決まるので、port 数だけを答える照会はありません。

## 主要 API

- `EspUsbDeviceCdcSerial consolePort(device, "Console")` は名前付きの CDC port を登録します。名前は function の `iFunction` / `iInterface` 文字列として Host へ届きます。
- `port.configure(EspUsbBulkStreamConfig)` でその port の容量を決めます。ring は port ごとに `begin()` で確保します。
- `port.port()` は `begin()` で決まる、CDC の descriptor 順の番号です。登録順に従います。
- `port.readCapacity()` / `writeCapacity()` は実際に確保した容量です。
- `port.setTxTimeoutMs(50)` は、その port の terminal が読まないときに `write()` が待つ上限です。1 つの port が止まっても他の port を止めないようにします。
- それ以外（`available()`、`read()`、`write()`、`connected()`、`onRx()`、`onLineCoding()`、`onLineState()`、`onBreak()`）は、[Serial](../Serial/) と同じように port ごとに使えます。

## 注意

- port に名前を付けてください。ACM function 同士は名前以外が同じなので、名前がないと Host は区別できません。
- `config.serialNumber` は一意にしてください。Windows は COM port 番号を VID/PID/serial ごとに覚えるので、serial がないと別の差込口へ挿し直したときに番号が変わります。
- CDC port はそれぞれ Interface Association Descriptor（IAD）で囲まれ、IAD を含む構成は `bDeviceClass = 0xEF/0x02/0x01` を宣言します。これにより Windows は全 interface に 1 つではなく、function ごとに driver を割り当てます。
- interface と endpoint の番号は登録順で決まります。順序を変えると番号も変わるので、Host 側の script が番号を固定している場合は注意してください。
- P4 で `config.controller = EspUsbController::FullSpeed` にすると IN endpoint は 4 本になり、`begin()` は 3 つ目の port を拒否します。

## 関連

- [Serial](../Serial/) - CDC ACM port 1 つ
- [CompositeCdcMidi](../CompositeCdcMidi/) - CDC と MIDI を別々の資源で同時に公開
- [USBVendor](../USBVendor/) - bulk の vendor interface。IN endpoint は 2 本ではなく 1 本
- [CDC API](../../docs/classes/cdc.ja.md)

## 読み方と確認の手順

### 実行前の準備

一つのUSB機器に独立した複数のCDC portを公開する。

上のハードウェアの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`ports[]`の表にportごとの名前と容量を並べ、`configurePort()`が`device.begin()`の前に適用します。各portは同じdeviceに属しますが、受信状態とDTRは別です。OSのportをそれぞれ開き、名前付きの返答を比べます。OSの番号が安定していると決めつけず、実際に試して対応を確かめます。3つ目のportは選んだtargetで決まります。

### APIの使い方と改造時の注意

`device.ready()`はUSBの構成済み状態、CDCの`connected()`はホストのDTRに関わり、別の問いに答えます。受信通知の量は一行・一packetの区切りではありません。実際に読めたbyteから自分でメッセージを組み立てます。`write()`はbyte、`print()`/`printf()`はテキスト整形です。`flush()`はPCアプリが消費済みという保証ではありません。

line coding通知はホストが求めたシリアル条件であり、USBの速度を変えません。callbackを短く保ち、大きな解析は同期した受け渡しでloop()へ分けます。確実な命令通信には受付byte数・未送信の後半・切断時の途中入力を扱う方針が必要です。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。port・容量・controllerを一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

## v2 からの変更点

- `EspUsbDevice::maxCdcPorts()` は廃止されました。port 数だけでは他の Function との endpoint の取り合いを表せないためです。`begin()` が構成全体を検証し、開始時の表示は各 port の実効容量に変えました。
- 容量は共通の `CFG_TUD_CDC_*` build flag ではなく、port ごとの `configure()` で決めます。
- `port()` は TinyUSB の instance ではなく、CDC の descriptor 順の番号です。
- `onLineCoding()` も port ごとに示し、各 port の `write()` の待ちを `setTxTimeoutMs()` で区切りました。
- port を表にまとめ、port ごとに同じコードを繰り返さないようにしました。

- [スケッチ](SerialMulti.ino)
- [全サンプル](../README.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
