# EspUsbDeviceNet（NCM）API（v3）

`EspUsbDeviceNet`は、USB Hostから「USB接続のネットワークアダプター」に見えるFunctionです。
USB規格のCDC-NCM（Network Control Model）を使い、Ethernet frameをそのまま送受信します。
必要なら`beginNetwork()`でESP-IDFのネットワーク層（esp_netif / lwIP）につなぎ、IPで通信できます。

## 基本の使い方

受け取ったEthernet frameをそのまま送り返す最小例です。

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceNet net(device);

void setup() {
  EspUsbNetConfig config;          // 既定値のままでよければ configure() は省略できる
  config.name = "USB network";
  if (net.configure(config) != ESP_OK) return;

  net.onFrame([](const uint8_t *frame, size_t bytes) {
    // USBの処理task上で呼ばれる。frame は callback の中だけ有効
    net.sendFrame(frame, bytes);   // 中身をコピーして送信待ちに入れる。満杯なら false
  });

  if (!device.begin()) {
    // device.lastError() で設定不正（ESP_ERR_INVALID_SIZE など）と確保失敗（ESP_ERR_NO_MEM）を区別できる
  }
}

void loop() {}
```

IPで使う場合は、`device.begin()`の後に`net.beginNetwork()`を呼びます（[IP層を使う](#ip層を使う)）。

### 主な設定

`EspUsbNetConfig`の項目です。`configure()`は停止中にだけ呼べます。

| 項目 | 既定値 | 意味 | FS/HSでの違い |
| --- | --- | --- | --- |
| `ntbInBytes` | 3200 | Device→Host方向の1回の転送（NTB）の最大byte数。2048〜65535 | なし |
| `ntbOutBytes` | 3200 | Host→Device方向のNTBの最大byte数。2048〜65535 | 実質の上限がFS 65472、HS対応controller 65024（[FSとHSの違い](#fsとhsの違い)） |
| `txBuffers` | 3 | 送信用NTB bufferの数。0なら送信しない | なし |
| `rxBuffers` | 2 | 受信用NTB bufferの数。0なら受信しない | なし |
| `maxDatagrams` | 8 | 1つのNTBに入れるframe数の上限。1以上 | なし |
| `maxFrameBytes` | 1514 | 1 frameの最大byte数（Ethernet header込み）。14以上、かつ両方向のNTB容量−28以下 | なし |
| `txBuffer` / `rxBuffer` | `nullptr` | アプリが用意するbuffer。`nullptr`なら`begin()`で確保 | 受信側の必要な長さが変わる（[外部bufferとメモリ](#外部bufferとメモリ)） |
| `txBufferBytes` / `rxBufferBytes` | 0 | 外部bufferを渡すときのbyte数。外部bufferでは必ず指定する | なし |
| `name` | `nullptr` | interfaceの名前（文字列descriptor） | なし |

NTB（NCM Transfer Block）は、複数のEthernet frameを1回のUSB転送にまとめた単位です。
INはDevice→Host、OUTはHost→Deviceを指します。

## API

### 設定と開始

| 関数 | 戻り値と制約 |
| --- | --- |
| `configure(config)` | `esp_err_t`。稼働中は`ESP_ERR_INVALID_STATE`。サイズや個数が範囲外なら`ESP_ERR_INVALID_SIZE`。外部bufferを渡したのにbuffer数やbyte数が0なら`ESP_ERR_INVALID_ARG` |
| `configuration()` | 設定した値をそのまま返す |
| `macAddress(mac)` | `esp_err_t`。停止中だけ（稼働中は`ESP_ERR_INVALID_STATE`）。multicastや全0のMACは`ESP_ERR_INVALID_ARG` |
| `macAddress()` | 現在のMAC（6 byte）へのpointer |
| `onFrame(callback)` | `esp_err_t`。停止中だけ。稼働中は`ESP_ERR_INVALID_STATE` |
| `startError()` | 直近の`begin()`の失敗理由 |

設定を変えるときは`device.end()`→`configure()`→`device.begin()`の順に呼びます。ビルドオプションや再ビルドは要りません。

`begin()`（`device.begin()`から呼ばれる）での失敗は次のとおりです。
理由は`net.startError()`と`device.lastError()`で確認できます。

- 受信buffer 1個分の長さが65535を超える：`ESP_ERR_INVALID_SIZE`
- 外部bufferが短い、64 byte境界でない、DMA可能な内部RAMでない、送受信のbufferが重なる：`ESP_ERR_INVALID_ARG`
- メモリを確保できない：`ESP_ERR_NO_MEM`

### 送受信

| 関数 | 説明 |
| --- | --- |
| `sendFrame(data, length)` | frameを送信用NTBへコピーできれば`true`。リンク未開始、送信待ちが満杯、長さが14未満か`maxFrameBytes`超過なら`false` |
| `onFrame`のcallback | 受信したframe 1つごとに、USBの処理task（worker）上で呼ばれる。pointerはcallbackの中だけ有効 |
| `linkUp()` | Hostがデータ用interfaceを有効にしていて、通信できる状態なら`true` |
| `ntbInCapacity()` / `ntbOutCapacity()` | 確保したNTBのbyte数。開始前・終了後・その方向のbufferが0個なら0 |
| `txBufferCount()` / `rxBufferCount()` | 確保したbuffer数。開始前・終了後は0 |
| `stats()` | `txFrames`、`rxFrames`、`invalidNtbs`。`begin()`から`end()`までの累計 |

- `sendFrame()`の`true`は「コピーして送信待ちに入れた」という意味で、Hostへ届いたことは保証しません。`txFrames`はUSB転送の完了後に増えます。
- 開始前に送ったframeを、後の接続へ持ち越すことはありません。
- 送信待ちのNTBに空きがあれば、続くframeを同じNTBにまとめます。DMAを始めるのはworkerだけです。
- NTBの長さがbulk MPS（1 packetの最大長。FS 64、HS 512 byte）の倍数で、Hostと決めた上限より短いときは、最後に長さ0のpacket（ZLP）を送って転送の終わりを示します。
- 受信bufferが2つ以上あれば、callbackを呼ぶ前に次の受信を始めます。1つだけなら、callbackが戻ってから同じbufferで次を受けます。
- 受信したNTBは、header・table・各frameの範囲と重なりを全部確かめてから、frameを順に渡します。壊れたNTBは丸ごと捨て、`invalidNtbs`を1つ増やして次のNTBを待ちます。途中までのframeを渡すことはありません。

### MACアドレス

- 既定のMACは、ESPのMACから作ったlocally administered（ローカル管理）なunicastアドレスです。同じDeviceに複数のNCMを置いても、Functionごとに別の値になります。
- MACはFunctionごとに持ち、そのMACの文字列descriptorも各Functionが持ちます（固定の文字列番号は使いません）。
- `onFrame`/`sendFrame`だけを使う場合、IP層の処理はリンクされません。

## IP層を使う

`beginNetwork()`を呼んだFunctionにだけ、esp_netifとの接続部を作ります。複数のNCMがあっても、netifやIP設定は共有しません。

```cpp
net.ipConfig(IPAddress(192, 168, 7, 1), IPAddress(192, 168, 7, 1), IPAddress(255, 255, 255, 0));
net.dhcpServer(true);          // PC側へアドレスを配る
device.begin();
net.beginNetwork();            // device.begin() の後、アプリのtaskから呼ぶ
```

| 関数 | 既定 | 説明 |
| --- | --- | --- |
| `ipConfig(local, gateway, subnet)` | 192.168.7.1 / 192.168.7.1 / 255.255.255.0 | 自分のアドレス。DHCP serverのときは配布の基準にもなる |
| `dhcpServer(enable)` | off | DHCP serverを動かす。`dhcpClient`とは同時に使えず、後から有効にしたほうが残る |
| `dhcpClient(enable)` | off | DHCPでアドレスを受け取る（PCがUSB NICをLANへブリッジする場合など） |
| `dhcpAdvertiseGateway(enable)` | off | DHCPでgateway（option 3）を通知する。Deviceが実際に転送するときだけ有効にする |
| `dhcpDns(dns)` | 0.0.0.0（通知しない） | DHCPで通知するDNS（option 6） |
| `defaultRoute(enable)` | off | このinterfaceをESPの既定経路にする。offのときは経路の優先度を低くし、Wi-Fi STAなどを既定経路のままにする |
| `beginNetwork()` | — | `bool`。IP層を開始する |
| `endNetwork()` | — | `bool`。IP層だけを止め、raw frameの`onFrame`へ戻す |
| `networkUp()` | — | esp_netif側のinterfaceの状態 |
| `localIP()` | — | 現在のIPアドレス |

- 設定関数は`esp_err_t`を返し、IP層の稼働中は`ESP_ERR_INVALID_STATE`です。
- `beginNetwork()`は`device.begin()`の後でないと`false`です。さらに`maxFrameBytes`が1514以上で、`txBuffers`と`rxBuffers`がともに1以上である必要があります（IPのMTUが1500 byteのため）。
- `beginNetwork()`/`endNetwork()`はアプリのtaskから呼びます。USBのworker上（`onFrame`の中など）からは`false`です。
- `device.end()`でもIP層は回収されます。
- `networkUp()`はSDKのnetifの状態、`linkUp()`はUSB側のデータinterfaceの状態です。
- ESP側のnetifのMACは、USB descriptorで通知するMACの最終byteの最下位bitを反転した値です。
- netifへの受信では、lwIPが所有するframeのコピーを別に確保します。IPを使う場合の動的確保はなくなりません。

## 外部bufferとメモリ

`txBuffer`/`rxBuffer`を渡すと、`begin()`でNTB用の領域を確保しません。

- 条件：DMA可能な内部RAM、先頭が64 byte境界、`txBufferBytes`/`rxBufferBytes`が必要な長さ以上、送信と受信の領域が重ならないこと。PSRAMは使えません（`ESP_ERR_INVALID_ARG`）。
- 必要な長さ＝1個分の長さ×buffer数。
  - 送信1個分：`ntbInBytes`を64の倍数へ切り上げた値。
  - 受信1個分：`ntbOutBytes`をbulk MPSの倍数へ切り上げた値。
- 例：`ntbInBytes = ntbOutBytes = 3200`、各2個の場合

| | FS（MPS 64） | HS対応controller（MPS 512） |
| --- | --- | --- |
| 送信 | 3200×2＝6400 byte | 3200×2＝6400 byte |
| 受信 | 3200×2＝6400 byte | 3584×2＝7168 byte |

- 開始から終了までbufferを有効に保ち、アプリから直接書き換えないでください。
- 送信用と受信用のbufferは、それぞれ`begin()`で一度だけ確保します。送受信中はheapを使いません。
- buffer数を0にした方向はbufferを確保しませんが、interface、endpoint、通知用の小さなbuffer、Functionの管理情報は残ります。

## FSとHSの違い

ここでの「HS対応controller」は、ESP32-P4（P4X含む）で`EspUsbDeviceConfig::controller`が`FullSpeed`以外のときです。
このときライブラリはbulk MPSを512として領域を用意し、Hostが実際にはFSでつないだ場合も同じ領域を使います。

| 項目 | FS | HS |
| --- | --- | --- |
| bulk MPS | 64 byte | 512 byte |
| `ntbOutBytes`の実質上限 | 65472（64の倍数へ切り上げて65535以下） | 65024（512の倍数へ切り上げて65535以下）。超えると`begin()`が`ESP_ERR_INVALID_SIZE` |
| 受信buffer 1個の長さ | `ntbOutBytes`を64の倍数へ切り上げ | `ntbOutBytes`を512の倍数へ切り上げ |
| 末尾ZLPを送る条件 | NTB長が64の倍数 | NTB長が512の倍数 |
| 通知endpointのbInterval | 16（16 ms） | 8（2^(8−1)×125 µs＝16 ms） |
| 1つのNCMが使うendpoint | 通知IN、bulk IN、bulk OUT（非control INは2本） | 同じ |
| 非control IN endpointの上限（構成全体） | ESP32-S3：4本 | ESP32-P4 HS：7本 |

S3では、Vendorと2つのNCMを同時に置くとIN endpointが足りません。

## endpointごとのHALT回復

IN、OUT、通知の各endpointは、それぞれ独立にHALT（STALL状態）と解除（CLEAR_FEATURE）を扱います。

- HALT/CLEARの対象endpointのDMAだけを止め、古い完了通知を処理し終えてから、その方向のbufferと送信待ちを初期化します。
- CLEARではそのendpointだけをDATA0（data toggleの初期値）へ戻します。反対方向や別のNCMの状態は変えません。
- IN（送信）のHALT：送信中・送信待ちのNTBは捨て、HALT中の`sendFrame()`は`false`です。受信は続きます。
- OUT（受信）のHALT：受信途中のNTBを捨てます。送信中・送信待ちは残ります。
- 通知のHALT：bulkには触れません。CLEAR後に最新の速度と接続状態を通知し直します。通知を送り直すためにbulkのdata toggleや送信待ちを初期化することはありません。
- MAC、NTB上限、alternate settingの要求値、callbackは残ります。回復が終わると`linkUp()`は元の状態へ戻ります。HALTを理由にIP層を作り直すことはありません。

SET_INTERFACE（alternate settingの切替え）と、HostからのNTB上限の変更では、Function全体を切り替えます。
古いDMAを止め、古い完了通知を処理し終えてから領域を再利用します。送信待ちのframeは捨てます。
SET_INTERFACEでは対象endpointのdata toggleも初期化し、NTB上限の変更では保ちます。
停止時は、USB workerとDMAが終わってから全bufferを解放します。

## 対応範囲

- 基準は[NCM 1.0 Errata 1の仕様一式](https://www.usb.org/sites/default/files/NCM10_012011.zip)です。
- NTB16（16 bit長のNTB）で、CRCなしだけを扱います。NTB32、CRC、IPやfilterの拡張機能は実装しません。
- Ethernet packet filterの機能は宣言しませんが、普通のHostが送るfilter設定の要求は受け付けます。
- HostがNTB入力容量を交渉し直した場合も、その容量とframe数の上限を守ります。

## 設計判断の理由

2026-10-27に、NCMのclass処理、固定のNTB pool、MACと送信queueのglobal変数を、本ライブラリ独自のdriverへ移しました。
Device＋Net＋`onFrame`/`sendFrame`＋`begin`という使い方は変えていません。

- 以前の「1600 byte×4件の別送信queue」と常駐のFreeRTOS queueは撤去し、frameを送信用NTBへ直接コピーしてworkerへ渡します。
- `macAddress()`や`onFrame()`などのsetterは、`void`から`esp_err_t`を返す形に変えました。普通の呼び方はそのままで、戻り値を確認できます。
- 利点：使わない方向の固定poolがなくなる、方向と容量ごとに費用が決まる、同じbinaryで構成を変えられる、Functionごとに独立したMACとnetifを回収できる。
- 不利益：`begin()`での確保失敗がありうる、コピーとlock・管理情報の費用、再交渉時に送信待ちframeを保持しない。
- 移行が必要なコード：`begin()`前の送信や、稼働中のMACや設定の変更に頼っていたもの。
- HALT回復（2026-10-28）は、既存のjob、fence（古い完了通知を処理し終えたことの確認）と、選択済みNCMの状態の中のbit maskで実装し、queueやbufferを追加で確保していません。対象のpipeを回復するだけで通信を再開でき、無関係な送受信や通知が途切れません。その代わり、対象方向の送信待ちや途中のNTBは再利用できず、必要な再送は上位層が担います。
- 試験でHostのdata toggleをそろえる準備にはSET_INTERFACEとpipeの再取得を使いますが、回復そのものはそれらに頼りません。転送中を狙う試験のための非公開の状態照会は、利用者向けAPIにはしていません。
- EspUsbHost 2.10.1は、NTB容量のcapability判定が仕様のD5と異なります。Device側の宣言は仕様どおりにしています。このHostはDeviceの最大値に合わせられるので、実試験で容量とpayloadの一致を確かめています。

## 未確認の範囲

- PCやOSのNCM driverとの相互接続、DHCP optionの内容とroute選択、ESP32-S2/S31での実通信。
- 外部callbackでの長い処理。
- 任意のEP0/reset/endとHALTの競合、TCP通信中のHALT。

## 実測の記録

### S3–S3（2026-10-26〜27）

`ncm-driver-related.xml`は**3 passed、0 skipped、0 unfinished、143.77秒**。
NTB容量2048/3200/8192、buffer 1/2/3個、外部buffer、14〜1514 byteのframe、ZLP、まとめ送り、
容量の再交渉と不正要求の後の復帰、heapの回復、605か所の確保失敗と回復を含みます。
手動のHostで22種類の不正NTBを捨て、直後の正常frameを返すことを確かめました。
block長0、末尾のNDP、逆順のdatagram、8128 byteのhardware armをまたぐpayload、独立した2つのNCMとMAC、12回のalternate切替えとdata toggleの初期化も確認しています。
serialの原本は`/tmp/pytest-embedded/2026-10-26_20-17-53-324449/`。

`ncm-ip-related.xml`は**2 passed、0 skipped、0 unfinished、154.11秒**。
3つの構成ともDHCPで192.168.7.2を取得し、TCPで送った8193 byteが一致し、IP停止後にraw通信へ戻れました。
2つのNCMを別のIP/keyで同時に作り、片方を止めて作り直すことにも成功しました。
serialの原本は`/tmp/pytest-embedded/2026-10-27_00-57-48-429795/`。

両実行とも、各caseで両boardがSTOPPEDになり、panicはありません。
コピーとJUnit/transcriptは`validation/.pytest-results/ncm-driver-related*`と`ncm-ip-related*`。
Netだけ・Deviceだけの最小build/nmも成功し、旧TinyUSB NCMと、使っていないNCM/IP接続部がリンクされないことを確認しました。

### NCM置換後の全体確認（2026-10-27）

標準の`--combo all`は**19 passed、0 skipped、0 unfinished、1080.50秒**。
全19 caseで両boardがSTOPPED、38本のserialにpanicなし。NCMのraw/control/2 interface、DHCP/TCPとIPの回収、605か所の確保失敗と回復、既存の全USB経路を含みます。
serialの原本は`/tmp/pytest-embedded/2026-10-27_01-02-46-482679/`、コピーとJUnit/transcriptは`validation/.pytest-results/runtime-ncm-all*`。
TinyUSB treeは51 file / 15 compiled sourcesで一致。旧tests/examplesはmainと同一で、mainはv3の祖先です。
この時点では、Videoがlegacy classとして残っていました。

### 方向別HALT回復（2026-10-28）

修正前は、HALT解除の制御要求が成功しても、次のframe交換が戻りませんでした（`ncm-halt-baseline.xml`、43.33秒）。
方向別の回復を入れた後、単独試験は38.43秒、実際のSTALLを含む補強版は44.69秒で成功しました。
最後に反対方向のOUTと通知をDATA1へ置く条件を加え、NCM raw 2件、NCM stream、allocation_failure、control_coreの
**5 passed、0 skipped/failed/error/unfinished、201.81秒**を確認しました。

- 構成：2つのNCM、NTB IN 3200 / OUT 16384 byte、送受信bufferは各2個。
- 36回のSET/CLEAR、8回の実STALL、24回のframe交換、途中NTBの送信10回、未読の送信の準備8回が成功。
- OUTの回復で未読のINを保ち、INの回復で途中のOUTとDATA1を保つ。通知のDATA1もbulkのHALT中に保ち、通知の回復では既存のbulkの返信を保つ。
- 同時にHALTしたうち片方だけを解除し、もう片方がwire上でSTALLを続けることも確認。
- 既存のNCM streamの3構成でDHCP/TCP 8193 byteの交換に成功。回復後に2つのIP interfaceの開始と回収、774か所の確保失敗と回復、heap 346712→346712 byte、10本のserialすべてSTOPPED、panicなし。
- S2/P4はcompileのみ成功。VendorとCCIDを使うprobeのELFに、使っていないNCMの状態や関数が入らないことをnmで確認。

原本は`/tmp/pytest-embedded/2026-10-28_06-40-07-341424/`。
JUnit/transcript/serialのコピーは`validation/.pytest-results/ncm-halt-final*`、集計は`ncm-halt-audit.txt`。
HostのSTALL用コマンドの受付を試験側に追加し忘れた失敗も記録しています。
当時の全体収集は33件で、この製品変更の後に33件すべてを実行したわけではありません。

### P4（FS loopback）とHS直結

- 2026-10-30：P4の両controllerのFS接続で、raw frameの基本通信と再開始を確認しました（P4のNCM 4 case）。詳細は[P4/P4Xと追加試験環境](V3_P4_VARIANTS_AND_BENCH.ja.md)。
- 2026-10-31：S3向けのpeer試験（`ncm_ntb`、`ncm_stream`を含む）にP4/P4Xのprofileを加え、旧P4 Host→P4X Device、P4X Host→旧P4 DeviceのHS直結でも実行しました。2つのcomboで失敗はISO OUTの1件だけで、NCMの試験は成功しています。
