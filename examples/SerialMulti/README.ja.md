# EspUsbDevice SerialMulti

> English: [README.md](README.md)

1台のデバイスに、独立した USB CDC ACM シリアルポートを2つ作る例です。ホスト側には
COM ポートが2つ（Windows）、または `/dev/ttyACM*` が2つ（Linux）見えます。バッファも
line coding も DTR 状態もポートごとに独立です。

機械向けのプロトコルと人間向けのコンソールを分けて、ログがコマンド列を壊さないように
する、といった使い方が典型です。

## ハードウェア

- USB device 対応の ESP32-S2 / S3 / P4 ボード
- PC、または EspUsbHost を動かす別の ESP32
- ログ確認用の Serial monitor 接続

## 動作内容

- `EspUsbDeviceCdcSerial` を2つ、`Console` と `Data Link` という名前で登録します。
  ESP32-P4 では endpoint に余裕があるので、3本目の `Telemetry` も登録します。
- どちらかのポートで受信した文字を、そのポートへポート名付きで返します。
- 3秒ごとに、開いているポートへ `tick=...` を送ります。
- 両ポートの DTR / RTS の変化を通常の Serial monitor に出力します。

## 使い方

1. sketch を書き込み、通常の Serial monitor を開きます。
2. USB device port をホストに接続します。
3. シリアルポートが2つ増えるので、両方開きます。
4. どちらかに入力すると、そのポートにだけ返信が返り、Serial monitor にはどちらの
   ポートが受けたかが出ます。

## 何本まで載るか

CDC は1ポートにつき **IN endpoint を2本**使います（通知＋データ）。このコントローラ群で
足りなくなるのは常に IN 側です。

| controller | CDC単独 | HID＋Vendorと併用 |
|---|---|---|
| ESP32-S2 / S3 | 2ポート（IN 4本を使い切り） | 1ポート |
| ESP32-P4 full-speed controller | 2ポート | 1ポート |
| ESP32-P4 high-speed controller | 3ポート | 2ポート |

上限を超えて登録すると `begin()` が `ESP_ERR_INVALID_SIZE` で失敗します。USB PHY を
起動する前に落ちるので、ホスト側には壊れたデバイスは見えません。このビルドの容量は
`EspUsbDevice::maxCdcPorts()` で確認できます。

## 主要 API

- `EspUsbDeviceCdcSerial Console(device, "Console")` は名前付きの CDC ポートを登録します。
  名前は IAD の `iFunction` と control インターフェースの `iInterface` としてホストに届きます。
- `Console.port()` はポート番号です。そのオブジェクトが駆動する TinyUSB インスタンス番号でも
  あり、登録順に決まります。
- `EspUsbDevice::maxCdcPorts()` はこの SoC 向けビルドのポート容量です。
- 他（`available()` / `read()` / `write()` / `connected()` / `onRx()` /
  `onLineCoding()` / `onLineState()`）は [Serial](../Serial/) と同じで、ポートごとに動きます。

## 注意

- ポートには名前を付けてください。2つの ACM 機能は名前が無いと descriptor 上まったく
  同じで、ホスト側で区別できません。
- `config.serialNumber` をボードごとに固有の値で設定してください。Windows は
  VID/PID/シリアルの組で COM ポート番号を覚えるので、無いと挿す USB ポートを変えるたびに
  番号が変わります。
- 複数機能のデバイスは `bDeviceClass = 0xEF/0x02/0x01` を自動で宣言します。これが
  Windows で「機能ごとにドライバをバインドする」動作になる条件です。
- ポートの順序が endpoint アドレスを決めます。登録順を変えるとアドレスも変わるので、
  ホスト側スクリプトが直書きしているなら注意してください。

## 関連

- [Serial](../Serial/) - CDC ACM 1ポート
- [CompositeHidCdcMsc](../CompositeHidCdcMsc/) - HID + CDC + MSC の複合デバイス
- [USBVendor](../USBVendor/) - bulk vendor interface。IN endpoint は 2本でなく 1本
