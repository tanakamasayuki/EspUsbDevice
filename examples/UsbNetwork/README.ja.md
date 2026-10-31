# EspUsbDevice UsbNetwork

> English: [README.md](README.md)

ボードを USB ネットワークデバイス（CDC-NCM）にし、内蔵の DHCP サーバと小さな Web ページを
提供する例です。PC に挿すと USB ネットワークアダプタとして現れ、PC は `192.168.7.0/24` の
アドレスを受け取り、`http://192.168.7.1/` にアクセスできます。Wi-Fi 不要、ドライバインストールも
不要です（Windows / macOS / Linux は NCM を標準対応）。

「USB 設定ポータル」のパターンです。PC がデバイス上のページや API に、ネットワーク設定なしで
USB 経由で直接アクセスします。

```cpp
EspUsbDevice device;
EspUsbDeviceNet net(device);

EspUsbNetConfig config;            // NTBの大きさとbuffer数
config.name = "EspUsbDevice NCM";
net.configure(config);
net.ipConfig(IPAddress(192, 168, 7, 1), IPAddress(192, 168, 7, 1), IPAddress(255, 255, 255, 0));
net.dhcpServer(true);
device.begin(identity);
net.beginNetwork();                // esp_netif + lwIP。device.begin()の後
```

## ハードウェア

- USB device 対応の ESP32-S3 など Arduino-ESP32 board（ESP32-S2、ESP32-P4 も可）
- PC などの USB host
- ログ確認用の Serial monitor 接続

## 動作内容

- `EspUsbDeviceNet` で CDC-NCM インターフェースを起動
- `beginNetwork()` で ESP-IDF のネットワーク層（esp_netif / lwIP）をつなぐ
- DHCP サーバを動かし、USB host に `192.168.7.x` を自動配布
- Arduino `WebServer` で `http://192.168.7.1/` に状態ページ（IP・MAC・稼働時間・閲覧回数・
  USB の frame 数）を配信
- USB link の up / down を表示

## 使い方

1. sketch を書き込み、通常の Serial monitor を開きます。
2. USB device port を PC につなぎます。
3. 新しいネットワークインターフェースが現れ `192.168.7.x` を取得します。
4. ブラウザで `http://192.168.7.1/` を開きます。

## 主要 API

- `EspUsbDeviceNet net(device)` は CDC-NCM function を登録します。
- `net.configure(config)` は NTB の大きさ（`ntbInBytes`、`ntbOutBytes`、既定 3200）、buffer 数
  （`txBuffers` 3、`rxBuffers` 2）、NTB あたりの frame 数、interface 名を決めます。buffer は
  `device.begin()` で確保します。IP 層には `maxFrameBytes` が 1514 以上で、送受信とも buffer が
  1 個以上必要です。
- `net.ipConfig(local, gateway, subnet)` はインターフェースのアドレスを設定します（既定 `192.168.7.1`）。
- `net.dhcpServer(true)` は DHCP サーバを動かします。代替: `net.dhcpClient(true)`（PC がブリッジした
  LAN からアドレス取得）、または両方呼ばず静的アドレス。
- DHCP サーバは既定では gateway と DNS を通知しないので、PC は自分の既定経路を保ちます。
  実際に転送する device なら `net.dhcpAdvertiseGateway(true)`、`net.dhcpDns(ip)` で通知します。
  `net.defaultRoute(true)` は USB link を ESP 自身の既定経路にします。
- `net.beginNetwork()` は esp_netif / lwIP インターフェースを起動します。`device.begin()` の後に
  スケッチの task から呼びます。`net.endNetwork()` は IP 層だけを止めます。
- `net.localIP()` / `net.macAddress()` はアドレスと Host へ通知する MAC を返します。
  `net.linkUp()` は USB のデータ interface の状態、`net.stats()` は frame 数と捨てた NTB の数です。
- IP スタックではなく生の Ethernet フレームを扱うなら `net.onFrame()` / `net.sendFrame()` を使い、
  `beginNetwork()` は呼びません。

## 注意

- デバイス側は CDC-NCM のみです。CDC-ECM はありませんが、NCM は最近のホスト OS が標準対応しています。
- デバイスが自前 DHCP サーバを持つ構成は「PC からデバイスへ」（設定ポータル・ローカル API）に最適です。
  デバイスからインターネットへ抜けるには PC 側のブリッジ/共有が必要でホスト依存なので、その用途は
  ESP 自身の Wi-Fi を使う方が適しています。
- `WebServer` は全インターフェースで listen するため、Wi-Fi があれば同じコードがそちらでも動きます。
- 受信 buffer は bulk packet（full speed 64 byte、high speed 512 byte）の倍数に切り上げるので、
  P4 の HS port では同じ `ntbOutBytes` でも RAM を少し多く使います。
- 既存 Arduino USB class と同時に使う設計ではありません。

## 関連

- [CompositeHidCdcMsc](../CompositeHidCdcMsc/) - 複数の USB function を複合
- [CdcConsole](../CdcConsole/) - CDC ACM serial device
- [v2 tests/manual/usb_ncm](https://github.com/tanakamasayuki/EspUsbDevice/tree/3fed50d0d777f600432b71381faccd93adb9a202/tests/manual/usb_ncm/) - v2 の最小構成の
  NCM + DHCP 手動テスト（host からの ping）

## 読み方と確認の手順

### 実行前の準備

boardに合うprofileを選びます（既定は`esp32s3`。ほかに`esp32s2`、v1.xのP4は`esp32p4`、
v3.1以上のP4は`esp32p4x`。v3.0は対象外）。診断Serialの接続先も確認します。
[サンプルの進め方](../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と
相手の利用可能状態の違いを説明しています。

### コードを追う

`net.beginNetwork()`の前にUSB deviceを開始し、`loop()`でweb serverのhandlerを回します。表示された
URLを開く前に、Host側の新しいnetwork interfaceとそのaddressを確認します。USB NCMはInternet接続、
Wi-Fi、routing、他のinterfaceへのbridgeを自動では提供しません。

### APIの使い方と改造時の注意

USB列挙、USB link（`linkUp()`）、IP設定、route/DNS、アプリの接続の順に確認します。IP addressが
付いてもInternetへ出られるとは限りません。直結のUSB subnetを試すだけのために、主に使うPCの
既定経路を変えないでください。

`onFrame`はUSB workerで動き、web serverのloopとは別です。時間のかかるHTTP・DNS・file処理を
USBのcallbackに入れないでください。network開始の戻り値を確認し、抜き差しの後に状態をどう
取り直すかを決めます。

### 一つずつ確認する

最初は例を変更せずに動かし、ログとHostのnetwork設定を照合します。addressや容量は一つずつ変えます。
例のエラー表示は、再試行や回復の方針ではありません。

## v2からの変更点

- NTBの大きさとbuffer数はFunctionごとに`net.configure()`で決め、`begin()`で確保します。固定の
  NTB poolと送信queueはなくなりました。
- `ipConfig()`、`dhcpServer()`などのsetterは`esp_err_t`を返し、スケッチで確認します。
- ページに`net.stats()`のUSB frame数を加え、USB linkの状態も表示します。

Arduino-ESP32 3.3.12で`sketch.yaml`の全profileのcompileを確認しています。このスケッチ自体の
実機試験はしていません。

- [スケッチ](UsbNetwork.ino)
- [全サンプル](../README.md)
- [NCM API](../../docs/classes/ncm.ja.md)
- [サンプルの進め方](../../docs/example-workflow.ja.md)
