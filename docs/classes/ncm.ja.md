# USB ネットワーク（CDC-NCM）

> English: [ncm.md](ncm.md)

`EspUsbDeviceNet` は、USB Host から「USB 接続のネットワークアダプター」に見える Function です。
USB 規格の CDC-NCM（Network Control Model）で、Ethernet frame をそのまま送受信します。
必要なら `beginNetwork()` で ESP-IDF のネットワーク層（esp_netif／lwIP）につなぎ、IP で通信できます。

## 概要と使う場面

| 使い方 | 内容 |
| --- | --- |
| IP 層を使う | `beginNetwork()` で netif を作る。DHCP server を動かせば、PC は USB をつなぐだけでアドレスを受け取り、ESP32 の Web ページや API に届く（USB 設定画面の形） |
| raw frame | `onFrame()`／`sendFrame()` で Ethernet frame を直接扱う。独自の bridge や試験に使う。IP 層はリンクされない |

- Wi-Fi なしで PC と ESP32 を IP でつなげます。
- 提供するのは CDC-NCM だけです。CDC-ECM や RNDIS はありません。
- USB でつないでも、ESP32 がインターネットへ出られるわけではありません。PC 側の bridge や共有が必要です。

## 基本の使い方

### IP 層を使う

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceNet net(device);

void setup() {
  EspUsbNetConfig config;          // 既定のままでよければ configure() は省略できる
  config.name = "USB network";
  if (net.configure(config) != ESP_OK) return;

  net.ipConfig(IPAddress(192, 168, 7, 1), IPAddress(192, 168, 7, 1),
               IPAddress(255, 255, 255, 0));
  net.dhcpServer(true);            // PC へ 192.168.7.x を配る

  if (!device.begin()) return;     // device.lastError() を確認
  net.beginNetwork();              // device.begin() の後、アプリの task から
}

void loop() {}
```

### raw frame を扱う

```cpp
net.onFrame([](const uint8_t *frame, size_t bytes) {
  // USB worker task 上で呼ばれる。frame は callback の中だけ有効
  net.sendFrame(frame, bytes);     // コピーして送信待ちに入れる。満杯なら false
});
device.begin();                    // beginNetwork() は呼ばない
```

設定を変えるときは `device.end()` → `configure()` → `device.begin()` の順です。再ビルドは要りません。

## 設定

### `EspUsbNetConfig`

`configure()` は停止中だけ受け付けます。

| 項目 | 既定値 | 範囲・意味 | FS/HS の違い |
| --- | --- | --- | --- |
| `ntbInBytes` | 3200 | Device→Host の NTB の最大 byte 数。2048〜65535 | なし |
| `ntbOutBytes` | 3200 | Host→Device の NTB の最大 byte 数。2048〜65535 | 実際の上限は FS 65472、HS 対応 controller 65024（受信 buffer を MPS の倍数に切り上げるため） |
| `txBuffers` | 3 | 送信用 NTB buffer の数。0 なら送信しない | なし |
| `rxBuffers` | 2 | 受信用 NTB buffer の数。0 なら受信しない | なし |
| `maxDatagrams` | 8 | 1 つの NTB に入れる frame 数の上限。1〜16377 | なし |
| `maxFrameBytes` | 1514 | 1 frame の最大 byte 数（Ethernet header 込み、FCS なし）。14 以上、かつ両方向の NTB 容量 − 28 以下 | なし |
| `txBuffer` / `rxBuffer` | `nullptr` | アプリが用意する buffer。`nullptr` なら begin で確保 | 受信側の必要量が変わる |
| `txBufferBytes` / `rxBufferBytes` | 0 | 外部 buffer の byte 数。外部 buffer では必須 | なし |
| `name` | `nullptr` | interface の名前（文字列 descriptor） | なし |

NTB（NCM Transfer Block）は、複数の Ethernet frame を 1 回の USB 転送にまとめた単位です。
IN は Device→Host、OUT は Host→Device です。

### IP 層の設定

すべて `esp_err_t` を返します。IP 層の稼働中は `ESP_ERR_INVALID_STATE` です。

| 関数 | 既定 | 説明 |
| --- | --- | --- |
| `ipConfig(local, gateway, subnet)` | 192.168.7.1 / 192.168.7.1 / 255.255.255.0 | 自分のアドレス。DHCP server では配布の基準にもなる |
| `dhcpServer(enable)` | off | DHCP server を動かす。`dhcpClient` と同時には使えず、後から有効にした方が残る |
| `dhcpClient(enable)` | off | DHCP でアドレスを受け取る（PC が USB の NIC を LAN へ bridge する場合など） |
| `dhcpAdvertiseGateway(enable)` | off | DHCP で gateway（option 3）を通知する。Device が実際に転送するときだけ有効にする |
| `dhcpDns(dns)` | 0.0.0.0（通知しない） | DHCP で通知する DNS（option 6） |
| `defaultRoute(enable)` | off | この interface を ESP32 の既定経路にする |

- 既定では gateway と DNS を通知しません。転送しない Device を PC の既定経路にすると、PC の外向き通信が止まるためです。
- `defaultRoute` が off のとき、USB の netif は経路の優先度を低くします。Wi-Fi STA などがあれば、そちらが既定経路のままです。
- どちらの DHCP も使わなければ、`ipConfig()` の固定アドレスで動きます。

### FS と HS の違い

「HS 対応 controller」は、ESP32-P4 で `EspUsbDeviceConfig::controller` が `FullSpeed` 以外のときです。
このとき bulk MPS を 512 として領域を用意し、Host が実際には FS でつないだ場合も同じ領域を使います。

| 項目 | FS | HS |
| --- | --- | --- |
| bulk MPS（USB 2.0 §5.8.3） | 64 byte | 512 byte |
| `ntbOutBytes` の実際の上限 | 65472 | 65024（超えると begin が `ESP_ERR_INVALID_SIZE`） |
| 受信 buffer 1 個の長さ | `ntbOutBytes` を 64 の倍数へ切り上げ | `ntbOutBytes` を 512 の倍数へ切り上げ |
| NTB の末尾に ZLP を送る条件 | NTB 長が 64 の倍数で、交渉した上限より短い | NTB 長が 512 の倍数で、交渉した上限より短い |
| 通知 endpoint | 16 byte、bInterval 16（16 ms） | 16 byte、bInterval 8（2^(8−1) × 125 µs ＝ 16 ms） |
| 通知する link 速度 | 12 Mbps | 480 Mbps |

## API

### 設定と開始

| 関数 | 戻り値と制約 |
| --- | --- |
| `esp_err_t configure(config)` | 稼働中：`ESP_ERR_INVALID_STATE`。大きさや個数が範囲外：`ESP_ERR_INVALID_SIZE`。外部 buffer を渡したのに buffer 数か byte 数が 0：`ESP_ERR_INVALID_ARG` |
| `configuration()` | 設定した値 |
| `esp_err_t macAddress(mac)` | 停止中だけ（稼働中は `ESP_ERR_INVALID_STATE`）。multicast や全 0 の MAC は `ESP_ERR_INVALID_ARG` |
| `const uint8_t *macAddress()` | 現在の MAC（6 byte） |
| `esp_err_t onFrame(callback)` | 停止中だけ。稼働中は `ESP_ERR_INVALID_STATE` |
| `startError()` | 直近の begin の失敗理由 |

`device.begin()` が失敗したときの `device.lastError()`：

| 値 | 原因 |
| --- | --- |
| `ESP_ERR_INVALID_SIZE` | 受信 buffer 1 個の長さが 65535 を超える。endpoint や descriptor の容量が足りない |
| `ESP_ERR_INVALID_ARG` | 外部 buffer が短い、64 byte 境界でない、DMA 可能な内部 RAM でない、送受信の buffer が重なる |
| `ESP_ERR_NO_MEM` | メモリを確保できない |
| `ESP_ERR_INVALID_STATE` | Device に登録していない、二重の begin、など |

### 送受信

| 関数 | 説明 |
| --- | --- |
| `bool sendFrame(data, length)` | frame を送信用 NTB へコピーできれば `true`。link 未開始、送信待ちが満杯、長さが 14 未満か `maxFrameBytes` 超過なら `false` |
| `onFrame` の callback | 受信した frame 1 つごとに USB worker task 上で呼ぶ。pointer は callback の中だけ有効 |
| `bool linkUp()` | Host がデータ interface を有効にし、通信できる状態なら `true` |
| `ntbInCapacity()` / `ntbOutCapacity()` | 確保した NTB の byte 数。begin 前・end 後・buffer 0 個の方向は 0 |
| `txBufferCount()` / `rxBufferCount()` | 確保した buffer 数。begin 前と end 後は 0 |
| `stats()` | `txFrames`、`rxFrames`、`invalidNtbs`。begin から end までの累計 |

- `sendFrame()` の `true` は「コピーして送信待ちに入れた」という意味です。Host へ届いたことは保証しません。`txFrames` は USB 転送が終わってから増えます。
- 送信待ちの NTB に空きがあれば、続く frame を同じ NTB にまとめます。
- begin 前に送った frame を、後の接続へ持ち越すことはありません。
- 受信 buffer が 2 つ以上あれば、callback を呼ぶ前に次の受信を始めます。1 つだけなら、callback が戻ってから次を受けます。
- 受け取った NTB は、header・table・各 frame の範囲と重なりを全部確かめてから frame を順に渡します。壊れた NTB は丸ごと捨て、`invalidNtbs` を 1 増やします。途中までの frame を渡すことはありません。

### IP 層

| 関数 | 説明 |
| --- | --- |
| `bool beginNetwork()` | IP 層を開始する |
| `bool endNetwork()` | IP 層だけを止め、raw frame の `onFrame` に戻す |
| `bool networkUp()` | esp_netif 側の interface の状態 |
| `IPAddress localIP()` | 現在の IP アドレス |

- `beginNetwork()` は `device.begin()` の後でないと `false` です。さらに `maxFrameBytes` が 1514 以上で、`txBuffers` と `rxBuffers` がどちらも 1 以上である必要があります（IP の MTU 1500 byte のため）。
- `beginNetwork()`／`endNetwork()` はアプリの task から呼びます。USB worker 上（`onFrame` の中など）からは `false` です。
- `device.end()` でも IP 層は回収されます。
- `beginNetwork()` を呼んだ Function にだけ netif を作ります。複数の NCM があっても、netif や IP 設定は共有しません。

### MAC アドレス

- 既定の MAC は、ESP32 の MAC から作った locally administered の unicast アドレスです。同じ Device に複数の NCM を置いても、Function ごとに違う値になります。
- USB descriptor の iMACAddress 文字列は Function ごとに持ちます。
- ESP32 側の netif の MAC は、USB で通知する MAC の最終 byte の最下位 bit を反転した値です（Host 側と Device 側で別の MAC が要るため）。

### endpoint の HALT

IN、OUT、通知の各 endpoint は、それぞれ独立に HALT と CLEAR_FEATURE を扱います。

| 対象 | HALT／CLEAR で捨てるもの | 保つもの |
| --- | --- | --- |
| IN（送信） | 送信中・送信待ちの NTB。HALT 中の `sendFrame()` は `false` | 受信 |
| OUT（受信） | 受信途中の NTB | 送信中・送信待ち |
| 通知 | なし。CLEAR 後に最新の速度と接続状態を送り直す | bulk の状態 |

- CLEAR はその endpoint だけを DATA0 に戻します。反対方向や別の NCM には触れません。
- MAC、NTB の上限、alternate の要求値、callback は残ります。HALT を理由に IP 層を作り直すことはありません。
- 失った frame の再送は上位層（TCP など）が担います。

### 呼べる文脈

| 操作 | 文脈 |
| --- | --- |
| `configure`、`macAddress(mac)`、`onFrame`、IP 層の設定 | 停止中のアプリの task（IP 層の設定は IP 層の停止中） |
| `sendFrame` | アプリの task、または `onFrame` の中 |
| `beginNetwork`、`endNetwork` | アプリの task だけ |
| `onFrame` の callback | USB worker task。待たずにすぐ戻る |

## バッファとメモリ

| 領域 | 置き場所 | 確保 |
| --- | --- | --- |
| 送信用 NTB（`txBuffers` 個） | DMA 可能な内部 RAM、64 byte 境界 | begin。1 個は `ntbInBytes` を 64 の倍数に切り上げた長さ |
| 受信用 NTB（`rxBuffers` 個） | DMA 可能な内部 RAM、64 byte 境界 | begin。1 個は `ntbOutBytes` を MPS の倍数に切り上げた長さ |
| 通知用 buffer（16 byte） | DMA 可能な内部 RAM | begin |
| 管理情報 | 通常の heap | begin |
| lwIP へ渡す受信 frame の写し | lwIP の heap | IP 層を使うとき、受信のたび |

- 外部 buffer（`txBuffer`／`rxBuffer`）を渡すと、begin で NTB 用の領域を確保しません。
- 外部 buffer の条件：DMA 可能な内部 RAM（**PSRAM は不可**）、先頭が 64 byte 境界、長さが「1 個分 × buffer 数」以上、送信と受信の領域が重ならない。
- 例：`ntbInBytes = ntbOutBytes = 3200`、各 2 個のとき

| | FS（MPS 64） | HS 対応 controller（MPS 512） |
| --- | --- | --- |
| 送信 | 3200 × 2 ＝ 6400 byte | 3200 × 2 ＝ 6400 byte |
| 受信 | 3200 × 2 ＝ 6400 byte | 3584 × 2 ＝ 7168 byte |

- 外部 buffer は begin から end まで有効に保ち、アプリから直接書き換えないでください。
- 送受信中に NCM 自身は heap を使いません。IP 層を使うと、lwIP が持つ受信 frame の写しを別に確保します。
- buffer 数を 0 にした方向は buffer を確保しません。interface、endpoint、通知用の buffer、管理情報は残ります。

## Host OS 側の扱い

| OS | 標準の driver | この library での確認 |
| --- | --- | --- |
| Linux | `cdc_ncm` | 未確認 |
| Windows | `UsbNcm`（標準で入っているかは Windows の版による） | 未確認 |
| macOS | 標準の NCM 対応 | 未確認 |

- 動作は、ESP32 を Host にした試験（S3 同士の FS、P4 一台の loopback、P4 と P4X の HS 直結）で確認しています。PC の OS の NCM driver との相互接続、DHCP option の扱い、経路の選ばれ方はまだ確認していません。
- PC で確かめる順番：USB の列挙 → 新しいネットワーク interface ができたか → アドレス → 経路と DNS → アプリの接続。
- PC の既定経路は変えないでください。直結の `192.168.7.0/24` だけに届けば足ります。
- Linux では `dmesg` で `cdc_ncm` が bind したかを見られます。`ip addr` で interface とアドレスを確かめます。

## 規格との対応と設計の理由

### CDC-NCM 1.0 との対応

基準は [NCM 1.0（Errata 1 を含む仕様一式）](https://www.usb.org/sites/default/files/NCM10_012011.zip) です。

| 項目 | 実装 |
| --- | --- |
| interface | Communication（subclass 0x0d）と Data（protocol 0x01）。IAD でまとめる |
| functional descriptor | Header（CDC 1.10）、Union、Ethernet Networking（iMACAddress、wMaxSegmentSize ＝ `maxFrameBytes`、filter なし）、NCM（bcdNcmVersion 1.00） |
| bmNetworkCapabilities（§5.2.1） | D5 だけ（8 byte の GET／SET_NTB_INPUT_SIZE）。packet filter などの機能は宣言しない |
| NTB 形式（§3） | NTB16 だけ。CRC なし。NTB32 は扱わない |
| class 要求（§6.2） | GET_NTB_PARAMETERS、GET／SET_NTB_INPUT_SIZE、GET_NTB_FORMAT、SET_NTB_FORMAT（NTB16 だけ）、GET_MAX_DATAGRAM_SIZE、GET／SET_CRC_MODE（CRC なしだけ）、SET_ETHERNET_PACKET_FILTER（受け付けるだけ） |
| 通知（§6.3） | NETWORK_CONNECTION と CONNECTION_SPEED_CHANGE |
| Data interface の alternate（§7.1） | alternate 0 は endpoint なし（停止・初期化）、alternate 1 に bulk IN／OUT |

- Host が NTB 入力容量を交渉し直した場合も、その容量と frame 数の上限を守ります。
- SET_INTERFACE と NTB 上限の変更では Function 全体を切り替えます。古い DMA を止めてから領域を再利用し、送信待ちの frame は捨てます。SET_INTERFACE は data toggle も初期化します。

### 設計の理由

- **Function ごとの独自 driver。** v2 の固定の NTB pool、別の送信 queue、MAC と送信 queue の global 変数をやめました。使わない方向の固定 pool がなくなり、同じ binary で構成を変えられます。Function ごとに独立した MAC と netif を持てます。
- **frame を送信用 NTB へ直接コピーする。** 中間の queue を持たず、費用が方向と容量で決まります。代わりに、容量の再交渉では送信待ちの frame を保持しません。
- **endpoint ごとの HALT 回復。** 対象の pipe だけを回復するので、無関係な送受信や通知は止まりません。
- **IP 層は必要なときだけリンクする。** `onFrame`／`sendFrame` だけなら esp_netif との接続部はリンクされません。

### v2 からの主な変更

- NTB の大きさと buffer 数は `configure()` で Function ごとに指定し、begin で確保します（固定の pool と送信 queue は廃止）。
- `macAddress()`、`onFrame()`、`ipConfig()` などの setter は `esp_err_t` を返します。
- begin 前の送信や、稼働中の MAC・設定の変更はできません。

## 関連する examples

| example | 内容 |
| --- | --- |
| [UsbNetwork](../../examples/UsbNetwork/) | DHCP server と小さな Web ページ。PC から `http://192.168.7.1/` を開く |

関連文書：[USB device 開発ガイド](../usb-device-guide.ja.md)、[troubleshooting](../troubleshooting.ja.md)、[v2 からの移行](../migrating-from-v2.ja.md)
