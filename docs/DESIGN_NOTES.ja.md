# EspUsbDevice 設計メモ

この文書は、新規 USB Device ライブラリ `EspUsbDevice` の設計背景、初期実装方針、`EspUsbHost` 既存テストからの移行メモをまとめたものです。

`EspUsbDevice` の最終目的は、ESP32 Arduino 向けによりよい USB Device ライブラリを提供することです。
初期段階では `EspUsbHost` の peer / loopback テストを優先ターゲットにしますが、これは実ハードウェアで低レベル挙動を具体的に検証しながら API と実装を固めるためです。

## 背景

現在の `EspUsbHost` テストは、ホスト側をこのライブラリで実装し、ピアデバイス側を Arduino-ESP32 標準の USB Device ライブラリで実装しています。
既存の peer テストは一通り動いていますが、テスト対象を広げるほど Arduino-ESP32 側の USB Device API と実装に限界が出ています。

特に問題になっている点は以下です。

- ESP32-P4 の Arduino USB Device 実装は High Speed 固定で、Full Speed device として起動する標準 API がない。
- P4 の `USB.begin()` / TinyUSB 初期化は rhport と PHY speed を P4 向けに固定しており、loopback テストで FS host 側に接続できない。
- HID keyboard API は US 配列・ASCII 入力寄りで、JIS/日本語キーボード固有キーや raw HID usage を正確に扱う設計になっていない。
- HID output report / feature report / LED など、テストに必要な低レベル挙動を安定して制御しづらい。
- Arduino-ESP32 本体へ PR するには、USB core、TinyUSB config、descriptor、board menu、後方互換性を横断して修正する必要があり、単発修正では済まない。

そのため、`EspUsbHost` と同じ思想で USB Device 側も明示的に制御できる `EspUsbDevice` を別ライブラリとして作る方針にしたいです。

## 目標

`EspUsbDevice` は、Arduino-ESP32 標準の `USB` / `USBHIDKeyboard` 互換ライブラリではなく、USB device の低レベル挙動を明示的に制御できるライブラリとして設計します。
最初は `EspUsbHost` のテスト・検証・サンプル用途に必要な機能から実装します。

主な目標は以下です。

- USB device の port、speed、descriptor、endpoint MPS を明示的に制御する。
- Peer テストと P4 loopback テストの両方で、ホスト・デバイスを EspUsb シリーズだけで構成できるようにする。
- HID は文字入力 API より raw report / HID usage を第一級 API として扱う。
- JIS/日本語キーボードを US 配列の拡張ではなく、HID usage とレイアウト変換の分離で扱う。
- Arduino-ESP32 標準 USB Device stack とは排他利用にする。
- 既存 `EspUsbHost` のテストを、Arduino-ESP32 device API 依存から段階的に移行できるようにする。

## 非目標

初期段階では以下を目標にしません。

- Arduino-ESP32 標準 `USBHIDKeyboard` / `USBHIDMouse` API との完全互換。
- すべての USB class を最初から網羅すること。
- 初期段階から PC 用の一般的な USB Device ライブラリとして完成させること。
- Arduino-ESP32 core の `USB.begin()` と同時利用すること。
- USB Hub、複数段 topology、OTG role switch まで含む汎用 USB stack を作ること。

## 既存 Arduino-ESP32 USB Device 実装の制約

### ESP32-P4 device speed

Arduino-ESP32 3.3.10 の P4 TinyUSB device 実装は、P4 の場合に PHY と TinyUSB を High Speed 固定で初期化しています。

代表的な実装は以下です。

```cpp
#if CONFIG_IDF_TARGET_ESP32P4
  .otg_speed = USB_PHY_SPEED_HIGH,
#else
  .otg_speed = USB_PHY_SPEED_FULL,
#endif
```

```cpp
#if CONFIG_IDF_TARGET_ESP32P4
  tinit.speed = TUSB_SPEED_HIGH;
  tusb_init(1, &tinit);
#else
  tinit.speed = TUSB_SPEED_FULL;
  tusb_init(0, &tinit);
#endif
```

また `tusb_config.h` 相当でも P4 は HS 最大速度です。

```cpp
#if CONFIG_IDF_TARGET_ESP32P4
#define CFG_TUD_MAX_SPEED OPT_MODE_HIGH_SPEED
#else
#define CFG_TUD_MAX_SPEED OPT_MODE_FULL_SPEED
#endif
```

endpoint size も High Speed 前提で 512 になります。

```cpp
#define CFG_TUD_ENDPOINT_SIZE (TUD_OPT_HIGH_SPEED ? 512 : 64)
```

このため、P4 で Arduino 標準 `USBHIDKeyboard` を使うと、HID endpoint でも `wMaxPacketSize=512` が出る場合があります。
P4 の FS host 側でこれを受けると、以下のように endpoint allocation で失敗します。

```text
HCD DWC: EP MPS (512) exceeds supported limit (128)
USBH: EP Alloc error: ESP_ERR_NOT_SUPPORTED
USB HOST: Claiming interface error: ESP_ERR_NOT_SUPPORTED
```

### P4 host 側との違い

`EspUsbHost` は Arduino-ESP32 の USB class を使わず、ESP-IDF USB Host API を直接呼んでいます。
P4 では `usb_host_config_t.peripheral_map` により HS host peripheral と FS host peripheral を選択できます。

```cpp
case ESP_USB_HOST_PORT_HIGH_SPEED:
  return 1U << 0;
case ESP_USB_HOST_PORT_FULL_SPEED:
  return 1U << 1;
```

ただし、これは host peripheral の選択であって、「HS host を FS-only で起動する」設定ではありません。
HS host は FS device と FS signaling で通信できる可能性がありますが、controller としては HS host 側です。
SDK に HS Host では USB Hub が使えない制約がある場合、HS host に FS device を接続してもその制約は回避できません。

### P4 USB ポート/PHY の実測整理（2026-07 実機確認）

`loopback/usb_audio` の切り分けで P4 の USB ポート/PHY の実態を確認したので、正確な内容をここに固定します。
以前の「P4 は FS device として起動できず FS host 側 loopback に接続できない」という理解は不正確でした。

- P4 は **OTG コントローラが2個、UTMI(HS) PHY は1個だけ**（`SOC_USB_OTG_PERIPH_NUM=2`, `SOC_USB_UTMI_PHY_NUM=1`）。
- **速度とコントローラ/PHY は別概念。** HS 対応コントローラ(UTMI)でも、接続相手が FS なら FS にネゴして動く。
- 旧Arduino Core依存経路ではDeviceがHS(UTMI) PHY固定だった。v2はこの初期化を使わず、
  `EspUsbController::FullSpeed`をrhport 0/internal FS、
  `EspUsbController::HighSpeed`をrhport 1/UTMIへmapしてruntimeに選択する。
- HS controllerでも相手がFSなら「HS PHY上のFS link」としてenumerateできる。
- 1台loopbackでは片方だけがUTMI PHYを使える。Device HS + Host FSと、
  Device FS + Host HSの両割当を同じtestで順番に検証できる。
- 帰結: **1台 P4 loopback で HS リンクは作れない**（HS PHY を host / device で共有できない）。HS device + HS host の loopback は不可で、HS リンクの検証には2台構成が要る。

| 1台loopback割当 | Device | Host | 実link |
|---|---|---|---|
| 通常 | HS/rhport 1/UTMI | FS/rhport 0 | FS |
| 逆 | FS/rhport 0 | HS/rhport 1/UTMI | FS |

#### USB Audio への影響と現在の決定

> この節の前半は旧Arduino Core依存実装を調査した記録です。現在の
> `EspUsbAudioFunction`はdescriptor構築をライブラリ内に持ち、下記の旧制約を解消しています。

旧実装では1台loopbackのリンクがFSでも、audio descriptorはbuild targetの
`TUD_OPT_HIGH_SPEED`でUAC2に固定され、EspUsbHostのUAC1 parserと接続できなかった。

「実リンク速度に追従して UAC1/UAC2 を出し分ける」ことは、このスタックでは現実的に不可能と確認した:

- config descriptor は接続前に `tinyusb_init()` で**一度だけ**組まれる（`tinyusb_load_enabled_interfaces()`）。
  `tud_descriptor_configuration_cb` は固定バッファを返すだけで、生成時点で `tud_speed_get()` は無効。
- しかも descriptor を組む主体が2つに分かれている（HID/CDC 等は EspUsbDevice の `tinyusb_enable_interface`、
  **Audio は esp_tinyusb コンポーネントの `tusb_audio_load_descriptor`**）。組み上がったバッファは core の
  static で外から触れない。request 時に速度別再生成するには両サブシステムを自前で握る＝実質フォークが必要。

**現在の決定：UAC versionとcontroller/link speedを分離する。** UAC1をdefault、
UAC2をconstructorで明示選択し、targetや速度による暗黙切替は行わない。UAC1の
speaker/microphone/duplexはS3 Peerで実転送まで自動確認する。UAC2 streamingは
対応するHost実装が揃った後に確認する。1台P4のAudio loopback testは現在未実装だが、
P4 AudioをUAC2/HSへ固定することは理由にしない。

#### bulk endpoint sizeとHS準拠（v2で解消）

v2はTinyUSB descriptor callbackを所有し、FS/HS configurationを接続速度に応じて返す。
CDC/MIDI/MSC/Vendorのbulk MPSはFS=64 / HS=512とし、device qualifierと
other-speed configurationも生成する。旧実装の「P4でbulk=64固定」という既知制約は
解消済みである。P4 HS controllerをFS hostへ接続した場合も、negotiated FS descriptorを
返すため512を誤って広告しない。

### 複合時の HID 採番衝突（実機確定・2026-07）

HID + 動的採番クラス（MSC / MIDI / Vendor）の複合で列挙・claim が失敗する。`peer/composite_hid_msc` で
実機確認し、原因は core まで追って確定した。**独立した 2 つの衝突**があり、両方の修正が必要だった。

> 【誤診の記録】当初 `device.begin()`==`ESP_FAIL` を「EP 衝突の実機確定」と判断したが誤り。実際は
> **テスト側 device が `MSC.onWrite()` を未設定**で `EspUsbDeviceMsc::begin()`（`readCallback_ &&
> writeCallback_` を要求）が false を返し、EP 割り当ての**前段**（クラス begin ループ）で失敗していた。
> テスト修正（onWrite 追加）後、下記 2 つの真の衝突が顕在化した。

**メカニズム**（`cores/esp32/esp32-hal-tinyusb.c`。interface enum 順は `MSC → DFU → HID → VENDOR → CDC → …`）:

core は各 class を enum 順に load し、endpoint と interface number を動的採番する。HID の descriptor は
`EspUsbDevice` が独自に組む（`configDescriptor_`）ため、この動的採番から外れて 2 点で衝突した。

1. **endpoint 番号**: core は使用中 EP を `tinyusb_endpoints.in/.out` ビットマスクで管理し、
   `tinyusb_get_free_*`（MSC/MIDI/Vendor が使用）がそこから採番する。HID を `reserve_endpoints=false` で
   有効化し、かつ独自採番で EP1(OUT)+EP2(IN) を使っていたため、HID の EP はマスク未登録。→ MSC の
   `tinyusb_get_free_duplex_endpoint()` が **EP1 を返し HID EP1(OUT) と重複**。
2. **interface number**: HID の descriptor は `bInterfaceNumber=0` を焼き込んでおり、`espUsbDeviceLoadHidDescriptor`
   が core の動的カウンタ `*itf` を無視してそのままコピーしていた。MSC は HID より先に load される（enum 順）ため
   MSC=interface 0 を取得、続く HID も 0 のまま → **interface number が両方 0 で衝突**（host は
   `EP with 2 address already allocated` / `Claiming interface error: ESP_ERR_INVALID_STATE` を出し HID claim 失敗）。

HID + CDC が動いていたのは、MSC が不在で HID が最初に load され（EP は固定 EP3/4/5 の CDC と偶然 disjoint、
interface number も HID=0 / CDC=1,2 で衝突しなかった）ため。

**影響範囲（実機 + コード確定）**: ✗ HID + {MSC, MIDI, Vendor}（動的採番クラスとの複合全般）。
○ HID + CDC、CDC/MSC/MIDI/Vendor の非 HID 複合（すべて core の動的採番で一貫するため衝突しない）。

**修正（実装済み・実機確認済み 2026-07）**: HID を core の動的採番に整合させた。

- (1) endpoint: HID descriptor を **EP1 duplex（OUT=0x01 / IN=0x81）** に統一し、HID を
  `reserve_endpoints=true` で有効化 → core が EP1 をビットマスク登録し、動的採番クラスは EP2 以降を取得。
  対象は keyboard / HidVendor / 複合 report-ID 経路の IN endpoint（`0x80|(N+1)`→`0x80|N`）。
  本ライブラリの HID は全構成が単一 interface（IN×1・OUT×1）なので EP1 duplex で足りる。
- (2) interface number: `espUsbDeviceLoadHidDescriptor` が copy 後に **HID interface の `bInterfaceNumber` を
  core 採番値 `*itf` に書き換える** → MSC=0 / HID=1 のように一意化。
- `unit/descriptor` の HID IN 期待値を `0x82`→`0x81`、`composite_vendor_eps` を `0x03/0x83`→`0x02/0x82` に更新
  （HID が EP1 duplex になり後続 interface の EP が 1 つ前進）。
- 検証: `peer/composite_hid_msc` **3/3 pass**（`DEVICE_BEGIN ok`、`HOST_ENUM ... dup=0 hid=1 msc=1 claimok=1`、
  MSC=interface 0 / HID=interface 1、keyboard・capacity とも機能）。

### 複合時の HID + bulk Vendor 二重記述（原因確定・修正済・実機確認・2026-07）

HID + bulk Vendor（`EspUsbDeviceVendor`）は当初「別問題で未対応」としていたが、原因を特定して修正した。

**症状**: HID keyboard + bulk Vendor を複合すると、Vendor interface（と bulk EP）が **USB config に二重に現れ**、
interface 番号も衝突して列挙が破綻する。

**原因**: `buildDescriptors()` は `configDescriptor_ = [config header][HID interface][Vendor interface]` を組む
（CDC/MSC/MIDI は各 loader 経由で `configDescriptor_` に入らないが、**Vendor だけは `configDescriptor_` に追記**
される。公開 API `configurationDescriptor(0)` と `unit/descriptor` の期待値がフル構成を前提とするため）。
ところが複合の HID 経路では `espUsbDeviceLoadHidDescriptor` が **`configDescriptor_` の「HID 以降すべて」
（= HID interface + 末尾の Vendor interface）を HID blob として丸ごとコピー**していた。その後 `begin()` は
`tinyusb_enable_interface(USB_INTERFACE_VENDOR, …)` で **Vendor をもう一度**登録するため、Vendor interface が
二重に出力される。加えて HID loader が `*itf += config[4]`（Vendor 込みの総 interface 数）で採番を進めるため、
後続 Vendor の interface 番号もずれて衝突していた。HID を含まない CDC/MSC/MIDI + Vendor では HID blob を
読まないので影響しなかった（＝これが「HID を含む複合限定」だった理由）。

**修正**（`src/EspUsbDevice.cpp`）:
- `buildDescriptors()` で Vendor を追記する直前に **HID interface 部分の長さ / interface 数**を記録
  （`hidInterfacesLength_` / `hidInterfaceCount_`）。
- `espUsbDeviceLoadHidDescriptor` は **HID interface 部分だけをコピー**し（末尾 Vendor を含めない）、
  `*itf` は **HID の interface 数だけ**進める（`config[4]` ではなく `hidInterfaceCount()`）。
- `begin()` の `tinyusb_enable_interface2(USB_INTERFACE_HID, …)` の予約長も `hidInterfacesLength_` に変更。
- `configDescriptor_` 自体はフル構成のまま（公開 API / 単体テストの契約を維持）。Vendor は従来どおり
  `espUsbDeviceLoadVendorDescriptor` 経由でのみ 1 回登録される。

**検証**: `peer/composite_hid_vendor` **3/3 pass**（`HOST_ENUM … dup=0 hid=1 vendor=1 ifnumdup=0 claimok=1`、
keyboard の tapKey → host `KEY a`、bulk Vendor は `onRx` 駆動で echo 往復＝上記 RX callback 修正の恩恵）。
HID 無し / HID+CDC / HID+MSC / HID+CDC+MSC は Vendor を含まないので `hidInterfacesLength_ == 総長-9` となり挙動不変
（回帰なし。`unit/descriptor`・既存 composite 実機で確認）。

### 複合時の endpoint 予算の上限（S3 実機確定・2026-07）

採番衝突を解消しても、**同時に載せられる endpoint には S3 の物理上限**がある。

- bulk のような双方向 function は、USB 仕様どおり同じ endpoint 番号を IN/OUT で共有する。現在の
  HID+CDC+MSC は HID=EP1、CDC notification=EP2 IN、CDC data=EP3 duplex、MSC=EP4 duplex となる。
- S3 の USB-OTG は `CFG_TUD_NUM_EPS=6` / `CFG_TUD_NUM_IN_EPS=5`。IN 側は EP0 を含むため、
  configuration が同時に開ける非control IN endpointは4本。CDCはnotificationとdataで2本、
  HID/MIDI/MSC/Vendorは各1本を使う。
- ✅ 列挙可: HID+CDC+MSC（非control IN=4）、
  CDC+MSC+Vendor（非control IN=4）。
- ✗ 列挙不可: HID+CDC+MSC+Vendor（非control IN=5、修正後レイアウトで実測）および
  HID+CDC+MSC+MIDI（同じく5）。`device.begin()` は成功するが、host の SET_CONFIGURATION
  時に **device が EP0 を STALL**（host 側ログ `USBH: Dev N EP 0 STALL` / `ENUM CHECK_CONFIG FAILED`）。
  descriptor生成自体はUSB上妥当なので、現在の`begin()`ではcontroller固有の上限まで判定していない。
- 実用上の指針: S3では**非control INを合計4本以内**にする。class数だけでは決まらず、CDCは2本として
  数える。超える構成はP4を使うか、将来controller capabilityをdescriptor validatorへ渡して
  `begin()`時に明示的に拒否する。

### 複合時の vendor RX callback が発火しない（原因確定・修正済・実機確認・2026-07）

`composite_cdc_msc_vendor`（CDC+MSC+bulk Vendor）で bulk echo が通らなかった件。host/device 両側に診断を
入れて切り分けたところ、**transport は正常だが `onRx` コールバックが一度も発火していない**ことが判明した。

診断（`v` コマンドで host が bulk OUT を書き、`q` で device 状態を問い合わせ）:

- `VENDOR_OPEN ok=1` / `VENDOR_WRITE ok=1` … host の claim も **同期 bulk OUT も成功**
  （`vendorWrite` は `USB_TRANSFER_STATUS_COMPLETED` を待つので ok=1 は device の HW ACK を意味する）。
- device 側 `DEVICE_VENDOR_STATE onrx=0 rxtotal=0 avail=4` … **データ 4 byte は device の vendor RX FIFO に
  届いている**（`tud_vendor_n_available(0)==4`）が、**`tud_vendor_rx_cb`（→ `onRx`）が一度も発火していない**。

**根本原因（コールバックのシグネチャ不一致による symbol override 失敗）**:

Arduino-ESP32 の TinyUSB（少なくとも 3.3.6〜3.3.10 / S2・S3・P4 の全同梱版）は `tud_vendor_rx_cb` を
**3 引数** `void tud_vendor_rx_cb(uint8_t idx, const uint8_t *buffer, uint32_t bufsize)` で宣言している
（`class/vendor/vendor_device.h`、`extern "C"` ブロック内）。ところが本ライブラリは旧 API の
**1 引数** `void tud_vendor_rx_cb(uint8_t itf)` で定義していた。

- header の 3 引数宣言と定義の 1 引数が食い違うが、定義側は `extern "C"` の外にあったため
  **コンパイルエラーにならず**、代わりに **C++ マングル名 `_Z16tud_vendor_rx_cbh` の別関数**として出力された
  （`nm` で確認）。
- その結果、TinyUSB の weak な C シンボル `tud_vendor_rx_cb` は**上書きされず**、`vendord_xfer_cb` は
  空の weak default を呼び続けた → `onRx` は永遠に発火しない。データは FIFO に届くので `available()`/`read()`
  では読めてしまい、「transport は動くのに callback だけ死ぬ」症状になっていた。
- 対照的に `tud_cdc_rx_cb(uint8_t itf)` は header の宣言も 1 引数で**一致**するため C linkage で正しく
  weak を上書きし、複合でも発火する（CDC が動いていたのはこのため）。**複合固有ではなく standalone でも
  発火しない**バグだった（当初「standalone では発火」と記録したが誤り）。

**修正**: `src/EspUsbDevice.cpp` の `tud_vendor_rx_cb` を header と同じ 3 引数シグネチャに変更。これで C linkage
となり weak default を上書きする（`nm` で C シンボル `tud_vendor_rx_cb` を出力することを確認）。buffered モード
（`CFG_TUD_VENDOR_TXRX_BUFFERED`、既定）では buffer/bufsize は NULL/0 でペイロードは RX FIFO にあり、
`handleRx()` が `available()`/`read()` で drain する。

**結論**: **CDC+MSC+Vendor は複合で正しく列挙し、bulk Vendor は `onRx` コールバック駆動で送受信できる**
（ポーリング不要）。`composite_cdc_msc_vendor` の device はポーリングを撤去し `onRx` のみで echo を成立させ、
テストは `onrx>=1` を検証する回帰ガードにした。

### CDC-NCM ネットワークデバイス（`EspUsbDeviceNet`・実機確認・2026-07）

USB ネットワークデバイス（PC から見て USB NIC）を `EspUsbDeviceNet` として追加。実 PC（Windows）で
DHCP リース取得と `ping 192.168.7.1` 疎通まで確認済み。

**スコープ判断:**
- **NCM のみ・ECM 非対応**。Arduino-ESP32 core（3.3.10）は `CONFIG_TINYUSB_NCM_ENABLED=y` が既定で
  S2/S3/P4 全 USB-OTG ターゲットに入っており、**NCM は追加ビルド不要**。一方 `CFG_TUD_ECM_RNDIS=0` で
  Kconfig も無く、ECM は core 再ビルドが必要なため対象外。NCM は最近の Win/mac/Linux が標準対応。
- **主ユースケースは PC→device**（USB 設定ポータル / ローカル API）。device→インターネットは PC 側の
  ブリッジ/NAT が必須でホスト依存のため非目標（ESP 自身の WiFi を使う方が適切）。

**USB クラス登録:** core の interface enum に NET 枠が無いため、**`USB_INTERFACE_CUSTOM` スロットに
`TUD_CDC_NCM_DESCRIPTOR` を発行**（`espUsbDeviceLoadNetDescriptor`、notif IN + bulk IN/OUT）。lib 内蔵の
`netd` class driver がこれを claim する。`tud_network_*` コールバックは **`class/net/net_device.h` を include
して正しい C シグネチャで定義**し weak default を上書き（vendor RX で踏んだマングル罠を回避）。
`tud_network_mac_address[6]` は lib 未定義なので当ライブラリで定義。

**IP スタック統合は esp_netif を採用**（raw lwIP + 自前 DHCP ではなく）。理由は DHCP サーバ/クライアント/
静的を **ネイティブに off-able で** 切替でき（`esp_netif_dhcps_start/stop`・`esp_netif_dhcpc_start`・
`esp_netif_set_ip_info`）、要件（DHCP は設定制・既定 OFF・ブリッジ=クライアントの余地）に一致するため。
NCM は標準 Ethernet フレームなので **`ESP_NETIF_NETSTACK_DEFAULT_ETH`（lwIP glue）を流用**、inherent config
は esp_eth のイベントシンボル依存を避けるため手組み。

**スレッド設計（要点）:** `tud_task`（usbd タスク）と lwIP tcpip タスクが別スレッド。
- RX: `tud_network_recv_cb`（usbd）→ buf に copy → `esp_netif_receive`（スレッド安全）→ lwIP。`recv_renew` で次を許可。
- TX: esp_netif の transmit（tcpip タスク）→ **mutex + セマフォで直列化**し、`tud_network_xmit` は
  usbd タスク側でのみ実行（TinyUSB は非スレッドセーフ）。`handleXmit`（= `tud_network_xmit_cb`）が copy 完了で
  セマフォを give し transmit を解放。

**API:** `onFrame`/`sendFrame`（生フレーム。`beginNetwork()` を呼ばなければ IP スタック無しの transport
のまま＝PC 側ブリッジ実験用）、`ipConfig`/`dhcpServer`/`dhcpClient`/`beginNetwork`/`localIP`/`macAddress`。
example は `examples/UsbNetwork`（NCM+DHCP+HTTP ページ）、手動/pytest テストは `tests/manual/usb_ncm`
（host からの `ping` 判定。WSL でも device serial 不要で通る）。

**堅牢性修正（コードレビュー指摘・2026-07）:**
- **TX の use-after-free**：`tud_network_xmit(ref,len)` は ref/len を記録するだけで、実コピーは後で
  usbd タスクの `tud_network_xmit_cb`（`handleXmit`）が行う。当初は lwIP/呼び出し側のバッファを直接 ref に
  渡し、送信は 100ms で諦めていたため、タイムアウトで lwIP が pbuf を解放した後に解放済みポインタを memcpy
  し得た。→ **内部バッファ `g_netTxBuf` にコピーしてから `tud_network_xmit` する** 単一経路
  `espUsbDeviceNetTxFrame()` に統一。`can_xmit` が false の間はバッファを再利用しないので上書き競合も無い。
  `sendFrame()` も同経路（mutex 直列化）に通し、「同期コピー」という誤ったコメントを削除。
- **破棄時の use-after-free**：`~EspUsbDeviceNet()` で **先に `g_activeNet=nullptr`**（以降 `tud_network_*`
  コールバックは no-op）→ `esp_netif_destroy()` で netif を解放。
- **DHCP**：サーバモードで既定は **router(option 3) と DNS(option 6) を広告しない**（`esp_netif_dhcps_option`）。
  自 dev は転送しないローカル終端なので、既定ルートにされると off-link がブラックホールになるため。host は
  on-link `/24` で device に到達でき、既存のインターネット経路は保持される。実際に転送する（別 uplink へブリッジ等）
  /到達可能な DNS を持つ上級ケース向けに、`dhcpAdvertiseGateway(true)`（gateway=ipConfig の gw を router 広告）と
  `dhcpDns(ip)`（DNS 広告、0.0.0.0 で無効）で **opt-in** できる。
- **ESP 自身のルーティング（DHCP option とは別レイヤ）**：上記の DHCP option は *ホスト側* の経路を制御する。
  *ESP 自身* のデフォルト netif は `route_prio` による自動選択で決まる。USB netif は既定 `route_prio=10` と
  低く、Wi-Fi STA（100）併用時は Wi-Fi が ESP のデフォルトのままで USB は outbound を奪わない（`esp_netif_set_default_netif`
  は明示呼び出ししない）。「PC が USB 経由で ESP に配線し ESP は USB を uplink にする」reverse-tether 用途では
  `defaultRoute(true)` で `route_prio` を Wi-Fi より上げてデフォルト netif にできる。
- **netif リーク/再試行不能**：`esp_netif_attach` 失敗時に `esp_netif_destroy` して同 `if_key` の再 `new` を可能に。
- **link 状態**：`linkUp()` は `tud_mounted()` を返す（抜線後も true に張り付く問題を解消。`tud_mount_cb`/
  `tud_umount_cb` は Arduino core が所有するため hook せず直接照会）。
- **MAC アドレス**：既定は固定のローカル管理アドレス（`02:02:84:6a:96:00`）だったが、全個体同一だと 1 台の
  ホストに 2 枚挿すと MAC 重複でホスト側 NIC 識別が破綻する。→ 既定を **チップ固有の `esp_read_mac(mac,
  ESP_MAC_ETH)`** に変更（descriptor loader で `macAddress()` 未設定時のみ導出）。ESP_MAC_ETH は eFuse ベース
  MAC 由来で Wi-Fi STA/AP・BT の MAC と必ず異なる（S3 は universal MAC が 2 個なので ETH/AP はローカルビット
  付きで導出される）ため、NCM と自機 Wi-Fi の同時使用でも衝突しない。iMACAddress string descriptor と
  `esp_netif_set_mac` は**この値の最下位バイト bit0 を反転した MAC** を使う（`devMac[5] ^= 0x01`）。
  iMACAddress は *ホスト側* インターフェースに割り当てる MAC なので、自 netif が同じ MAC を使うと
  point-to-point の同一 L2 セグメント上で両端が同一 MAC になり、ARP がホスト自身を指す。TinyUSB の
  `net_lwip_webserver` と同じ回避策で、USB リンクは隔離セグメントのため派生アドレスが実ネットワークに
  漏れることはない。`macAddress()` を呼べば（広告 MAC を）固定でき、その場合は自動導出を抑止する
  （`g_netMacUserSet`）。なお `dhcpServer(true)` を 2 台使うと IP サブネット `192.168.7.0/24`（既定値）は
  依然重複するので、同一ホストへの複数台接続は上級用途。ただしこのサブネットはハードコードではなく
  `ipConfig()` 未設定時の既定に過ぎず、`beginNetwork()` の前に `ipConfig(local, gateway, subnet)` を呼べば
  任意のサブネットへ変更できる（`cfgIp_==0` のときだけ `192.168.7.1/255.255.255.0` が入る）。DHCP サーバの
  配布レンジは esp_netif が `set_ip_info` の IP/mask から自動導出するので追従する。各デバイスに別サブネットを
  与えれば、MAC が個体別になったことと合わせて 1 台のホストに複数台を共存させられる。
- bulk EPはlibrary所有のper-speed descriptorでFS=64 / HS=512を選ぶ。旧FS固定の
  制約はNCMを含む全bulk classで解消済み。

### HID keyboard API の限界

Arduino 標準 keyboard API は `write(char)` のような文字入力 API が中心です。
これは US 配列の簡易入力には便利ですが、テスト用 device としては以下が問題です。

- HID usage ID と文字変換が混ざる。
- JIS 固有キーを扱いづらい。
- `無変換`、`変換`、`かな`、`半角/全角`、`ろ`、JIS の `¥` などが自然に表現できない。
- raw boot keyboard report を任意に送るテストが書きづらい。
- output report / LED の受信確認が Arduino 標準 API の実装都合に引きずられる。

`EspUsbDevice` では、文字 API は上位レイヤとして扱い、基本は raw HID usage / report を直接扱うべきです。

### LED output report の観測（`ledState()`・2026-07）

`onOutputReport()` は単一 slot の `std::function` なので、統合レイヤ（ESP32KeyBridge の出力 adapter など）が
Lock 状態を取るために slot を占有すると、スケッチ側から LED を読む手段が無くなっていた。`ledState()` で
最新の output report を公開して解決する。

- **listener 化しない。** LED は event ではなく**状態**で、競合しているのは「1回の通知を誰が消費するか」ではなく
  「最新値を誰が読めるか」。getter なら本ライブラリに listener 基盤を新設せずに済み、EspBle の HID Device 側
  `onOutputReport()` が単一 slot である現状（= Device 側同士の一貫性）も崩さない。`onOutputReport` /
  `onProtocol` の listener 化自体は将来の選択肢として残す（観測系なので EspUsbHost の切り分けでは対象内）。
- **callback の有無に関係なく更新する。** `onHidSetReport()` で raw byte を保存してから callback を呼ぶ。
  callback 未設定時に早期 return する旧構造のままだと状態が更新されない。
- **値返しにする（参照ではない）。** `onHidSetReport()` が走るのは TinyUSB device task
  （`internal/EspUsbTinyUsbRuntime.cpp` が `espusb-device` task で `tud_task()` を回している）で、
  `ledState()` を呼ぶのはスケッチの task。参照を返すと**他 task が書き換える実体**を読ませることになる。
  保持するのは raw LED byte 1 個を `std::atomic<uint8_t>` で、`ledState()` はその 1 回の load から
  report を組み立てる。これでフィールドが途中状態で混ざる（leds は新しいが capsLock は古い）ことが
  原理的に起きない。EspBle も同じ理由で値返し（そちらは stack task から書かれ mutex で保護）。
- **bus attach / detach でクリアする。** `begin()` だけでは足りない。抜線・再挿入でオブジェクトは生き残る。
- **ビットとフラグの対応は `EspUsbDeviceHidKeyboardOutputReport::setLeds()` の 1 箇所だけ。** ライブラリは
  フィールドを直接代入せず、callback へ渡す report も `ledState()` が返す report もこれを通す。
  EspBle も同じ形（`EspBleHidKeyboardOutputReport::setLeds()`）で、Lock フラグが両ライブラリとも
  bool メンバに揃った（EspBle 側は 1.0.0 前に メソッド → メンバの破壊的変更で追随）。
- **Host が最初の output report を送るまでは全 false。** 「Host が全 LED off と言った」状態と区別できないが、
  区別のための flag は API を増やす割に用途が薄いので持たない（外付け LED 用途では差が無い）。

### bus attach / detach での host 向け状態のクリア（2026-07）

`tud_mount_cb` / `tud_umount_cb` を実装し、`EspUsbDeviceClass::onBusAttached()` / `onBusDetached()`
（既定 no-op）へ配送する。keyboard はこれを受けて LED 状態と NKRO 保持状態を捨てる。

- **なぜ必要か（stuck key）。** 状態ベースの呼び出し側には「重複送信の抑制は自分の責務、比較対象は
  `heldState()`」と案内している。抜線・再挿入を挟んで chord が残っていると、その比較が「前回と同じだから
  送らない」と判断する一方 Host は何も押していないので、**解消しない stuck key** になる。EspBle も同じ理由で
  切断時に `heldState()` をクリアしており（`EspBle/docs/REPLY_ESPBLE_LED_STATE.ja.md`）、そちらの指摘で
  こちらの穴も見つかった。
- **両方の hook が必要。** `tud_umount_cb` は `SET_CONFIGURATION 0` / deinit / （VBUS sensing のある
  ボードでのみ）抜線で呼ばれる。**素の bus reset では呼ばれない**ので、VBUS sensing の無いボードで
  再挿入すると umount を経ずに再 enumeration へ進む。常に発火する `tud_mount_cb`
  （`SET_CONFIGURATION n`）側でもクリアして穴を閉じる。mount 時点では Host は「何も押されていない・
  LED 未設定」と認識しているので、どちらで消しても正しい。
- **NKRO 保持状態は USB task からは消さない。** `onBusAttached()` / `onBusDetached()` は USB task で走る。
  そこで `nkroState_` を触ると書き手が 2 つになり、`heldState()` が返す参照が競合する。USB task は
  atomic flag を立てるだけにして、実際のクリアはスケッチ側 task の入口
  （`sendReport` / `pressUsage` / `releaseUsage` / `releaseAll` / `heldState`）で `applyPendingBusChange()`
  が行う。これで `nkroState_` の書き手はスケッチ task 1 つのままなので、`heldState()` は参照を返せる
  （EspBle 側も `heldState()` は参照のまま）。LED は atomic byte 1 個なので USB task から直接クリアできる。

### N-key rollover（NKRO・opt-in・2026-07）

`EspUsbDeviceHidKeyboard::enableNkro()`（`begin()` 前）で NKRO に切り替えられる。設計上のポイント:

- **レポート形式**: report protocol では modifier 1バイト + usages `0x00`-`0xDF` の 224bit
  bitmap（`NKRO_KEYBOARD_REPORT_DESCRIPTOR`）。各キーが専用ビットを持つのでロールオーバー制限が無い。
  bitmap 上限を `0xDF` にしたのは (1) modifier 領域 `0xE0`-`0xE7` と重ならない、(2) 224bit=28B ちょうどで
  バイト境界に揃う、ため。International1-9(`0x87`-`0x8F`)・LANG1-9(`0x90`-`0x98`) を含むので JIS も通る
  （`0x00`-`0x77` に絞ると日本語キーが漏れるため full 範囲を採用）。
- **boot fallback**: 単独時は interface が `SUBCLASS_BOOT` を宣言しているので、Host が SET_PROTOCOL(boot) を
  投げたら（BIOS/UEFI）`protocol_==0` を見て bitmap を先頭6キーに畳んだ 6KRO boot report を送る。複合時は
  interface protocol が 0 なので boot は来ない。
- **エンドポイント**: bitmap レポートは 29B(複合は +Report ID で 30B)。1転送に収めるため、`hidInEndpointSize()`
  仮想関数で NKRO 時 32B を要求し、単独パス（`configurationDescriptor`）と複合パス（共有 HID EP のサイズ計算）の
  両方で wMaxPacketSize を引き上げる。`CFG_TUD_HID_EP_BUFSIZE=64`(=`CONFIG_TINYUSB_HID_BUFSIZE`) 以内なので安全。
- **状態モデル**: NKRO 有効時は公開型そのものを持つ `nkroState_`（`EspUsbDeviceNkroKeyboardReport`）が正の状態。
  bitmap レイアウトと modifier 振り分け（`0xE0`-`0xE7`）の定義を1箇所に閉じるため、private の `setKeyBit()` は
  廃止して `pressUsage/releaseUsage/releaseAll` も `nkroState_.press()/.release()/.clear()` を通す。
  `sendReport(boot)` は渡された 6KRO を bitmap 状態に取り込む（`write()/tapKey()` など文字 helper もそのまま動く）。
  全 NKRO 送信は private `sendNkroReport()` の1本に集約。既定は無効で、6KRO の既存挙動・descriptor は一切変えない。
- **状態送信 API**（2026-07 追加）: `sendReport(const EspUsbDeviceNkroKeyboardReport &)` で保持キー全体を1レポートで
  送る。状態ベースの統合レイヤ（ESP32KeyBridge の `OutputAdapter::write(KeySet)`）は毎周期にキー集合全体を渡す契約で、
  増分 API に合わせると差分計算とライブラリ内部状態の同期が呼び出し側に要り、1キーの変化ごとに1レポートで
  同時押し・同時離しが分割される。`heldState()` で最後に Host へ伝えた状態を公開し、shadow copy 無しで差分・再同期が
  できるようにした（`releaseAll()` 以外の復旧手段が無い状態を解消）。
- **重複送信は抑制しない**: 毎周期呼ばれる契約なので同一状態が連投されるが、ライブラリ側で「前回と同じなら送らない」を
  隠し持つと `releaseAll()` や boot protocol 切替を挟んだあとの再同期が読めなくなる。抑制は呼び出し側の責務とし、
  比較対象として `heldState()` を提供する。
- **`enableNkro()` 未実行時は失敗、boot protocol 時は畳む**: 同じ「NKRO の形では送れない状況」で挙動を分ける基準は
  責任の所在。boot protocol の選択は Host 主導の実行時条件でスケッチに責任が無いため送れる形へ畳む。`enableNkro()`
  忘れは構成の誤りで、畳んで成功させると7キー目以降が恒久的に無言で消えるため失敗させる（`nkroEnabled()` で事前判定可）。
- **boot fold-down の内容**: 押した順ではなく **usage 番号の小さい順**に先頭6個を採り、7キー以上でも
  `ErrorRollOver`(`0x01`) は返さない。BIOS/UEFI で7キー同時押しを識別させる要求が実用上無く、Host 自身が boot を
  選んでいるため「妥当な何かを送る」方が失敗より良い、という妥協を意図的に固定したもの。
- **命名規則**: bitmap を持つメンバは `bitmap`、usage の配列を持つメンバは `keys`。`keys[0] = 0x04` が型によって
  「usage 0x04 が押されている」と「usage 3 と 5 が押されている」の別の意味になり、取り違えてもコンパイルが通る。
  姉妹3ライブラリ共通の決定（`EspBle/docs/DECISIONS.ja.md` 19）で、`EspUsbHost` も `EspUsbHostKeyboardState` を
  `bitmap`/`changedBitmap` へ改名済み（2.7.0）。EspUsbDevice では bitmap を持つ公開メンバが本 struct が最初なので
  既存 API の改名は不要（`EspUsbDeviceBootKeyboardReport::keys[6]` は usage 配列なのでそのままで正しい）。
  bitmap サイズは Host 側32 byte（`0x00`-`0xFF`）／Device 側28 byte（`0x00`-`0xDF`）で非対称。Device 側は
  report descriptor が宣言する範囲に縛られるため。
- **増分 API のエラー化**: 旧 `setKeyBit()` は範囲外 usage を黙って無視していた。`press()`/`release()` の戻り値を
  そのまま `pressUsage()`/`releaseUsage()` の失敗にする（EspBle も同じ形。`DECISIONS.ja.md` 20）。あわせて
  `releaseUsage()` が modifier usage を落とさなかった穴も解消した（`release()` が `modifiers` 側もクリアする）。
- **検証**: struct の bitmap レイアウト・modifier 振り分け・境界（`0xE8` 以上を拒否）は host g++ の
  `tests/unit/nkro_report`（実ヘッダから struct を抽出してコンパイル）でカバー。
- **peer 検証**: `tests/peer/hid_keyboard_nkro` が実機で検証済み。`EspUsbHost` は
  `keyboardUsesBitmapReport()` で bitmap レポートを識別できるので、host 役として使える。この suite は
  8キー chord の**キーコード集合の一致**（数だけでなく識別）と、International/LANG（JIS）の高 usage
  `0x87`-`0x91` が全て届くこと（bitmap が `0x00`-`0xDF` 全域であることの証明）を見る。
  状態送信 API（1レポートで7キー以上）は同 suite の `s` コマンドで追加検証する。

## 設計方針

### 1. Arduino USB stack とは排他

`EspUsbDevice` を使うスケッチでは、Arduino-ESP32 標準の以下を使わない前提にします。

- `USB.begin()`
- `USBHIDKeyboard`
- `USBHIDMouse`
- `USBCDC`
- `USBMSC`
- `USBMIDI`
- `USBAudioCard`

同じ TinyUSB / USB PHY / endpoint を二重初期化すると破綻するため、排他利用を明文化します。

### 2. Device 初期化を明示設定にする

```cpp
struct EspUsbDeviceConfig {
  const char *manufacturer = "EspUsbDevice";
  const char *product = "EspUsbDevice";
  const char *serialNumber = nullptr;
  uint16_t vid = 0x303a;
  uint16_t pid = 0x4000;
  bool selfPowered = false;
  uint16_t maxPowerMilliamps = 100;
};
```

> 【2026-07 改訂 / Step 1】当初は `EspUsbDevicePort` / `EspUsbDeviceSpeed` で port / speed を
> "選択" できる設計にしていたが、これは誤った抽象だったため撤去した。理由:
>
> - **port は honor できない。** P4 では Arduino core がデバイスを HS(UTMI) コントローラに
>   決め打ちしており（`init_usb_hal` / `tusb_init(1)`）、`config` から選べない（実際 `config_.port` /
>   `config_.speed` はどこにも使われていなかった）。
> - **speed は"選択"するものではない。** 実際のリンク速度はホストとのネゴで決まる。デバイスが宣言した
>   値で EP サイズを決めると、ネゴ結果と食い違ったとき壊れる（HS 決め打ちの Core は FS で EP512 が
>   通らず、FS 決め打ちだと HS で bulk=512 必須に違反する）。
>
> よって capability（FS-only か HS 対応か）は**ハードで決まる前提**とし、config からは port / speed の
> 選択欄を無くした。理想は EP サイズや UAC1/UAC2 を**ネゴ速度に追従して出し分ける**ことだが、このスタック
> では build-once 等の制約で実質フォークが必要と判明したため見送り、現段階は全 class とも **FS サイズ固定**
> （HS 非準拠は既知制約）とする。詳細は「P4 USB ポート/PHY の実測整理」および「bulk エンドポイントサイズと
> HS 準拠」を参照。

### 3. Descriptor はライブラリ側で所有する

Arduino-ESP32 の共通 `CFG_TUD_ENDPOINT_SIZE` に依存せず、device speed と class ごとに適切な MPS を生成します。

目安:

- FS interrupt endpoint: 8/16/32/64 bytes。HID keyboard/mouse なら 8 bytes 程度でよい。
- FS bulk endpoint: 64 bytes。
- HS bulk endpoint: 512 bytes。
- HS interrupt endpoint: class/report に応じて明示。HID keyboard で 512 にする必要はない。
- EP0: 基本 64 bytes。

テスト用途では「ホストが正しく扱うべき descriptor」を作ることが重要です。
速度に応じた endpoint MPS の正しさをユニットテスト対象にします。

> 【現状 / 2026-07】上記は目標。実装は**全 class とも FS サイズ固定**（interrupt=8、bulk=64）で、
> 速度別の出し分けはまだ入れていない。P4 を実 HS ホストに繋ぐと bulk が非準拠になる既知制約がある。
> 詳細と方針は「bulk エンドポイントサイズと HS 準拠」を参照。

### 4. Class は小さい部品として合成する

Device 全体を `EspUsbDevice` が管理し、各 class は追加登録する形にします。

```cpp
EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceHidMouse mouse(device);
EspUsbDeviceCdcAcm cdc(device);

void setup() {
  EspUsbDeviceConfig config;
  device.begin(config);
}
```

Composite device を自然に作れることが重要です。
Peer テストでは keyboard + mouse、HID vendor in/out、CDC、MIDI、MSC などを組み合わせます。

### 5. raw report を第一級 API にする

HID keyboard の最小 API は文字入力ではなく report 送信です。

```cpp
struct EspUsbDeviceBootKeyboardReport {
  uint8_t modifiers;
  uint8_t reserved;
  uint8_t keys[6];
};

keyboard.sendReport(report);
keyboard.pressUsage(ESP_USB_HID_KEY_A);
keyboard.releaseUsage(ESP_USB_HID_KEY_A);
keyboard.releaseAll();
```

文字入力は別レイヤです。

```cpp
keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);
keyboard.write("abc");
keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP);
keyboard.write("@[]:\"");
```

layout ID は Host 側の `EspUsbHostKeyboardLayout` と同じ値にします。
Device 側は `ascii -> usage/modifier`、Host 側は `usage/modifier -> ascii` の
逆方向変換として対応させます。初期実装では `EN_US` と `JA_JP` の ASCII wrapper を
提供し、他の layout は Host 側 keymap と同じ粒度で順次追加します。

JIS キーは ASCII ではなく usage として明示します。

```cpp
keyboard.pressUsage(ESP_USB_HID_KEY_LANG1);       // kana 等
keyboard.pressUsage(ESP_USB_HID_KEY_INTERNATIONAL1);
keyboard.pressUsage(ESP_USB_HID_KEY_INTERNATIONAL3);
keyboard.pressUsage(ESP_USB_HID_KEY_HENKAN);
keyboard.pressUsage(ESP_USB_HID_KEY_MUHENKAN);
```

名称は HID Usage Tables に合わせ、ローカル別名を足す場合でも usage 値との対応を明示します。

## 必要機能

### MVP

最初に作るべき最小範囲です。

- P4/S3 のビルド対応。
- Arduino 標準 USB stack と排他で TinyUSB device を初期化。
- device port / speed / VID / PID / string descriptor の設定。
- descriptor 生成と endpoint MPS の speed 別切替。
- HID keyboard boot protocol device。
- raw keyboard report 送信。
- keyboard output report 受信 callback。NumLock/CapsLock/ScrollLock を検証可能にする。
- HID mouse boot protocol device。
- raw mouse report 送信。
- pytest-embedded の peer device として使える serial command protocol。

MVP の目的は、既存 `tests/peer/hid_keyboard`、`tests/peer/hid_mouse`、`tests/peer/hid_keyboard_mouse`、P4 `loopback/hid_keyboard` を Arduino 標準 USB device なしで再実装することです。

### HID 拡張

次に必要な HID 機能です。

- HID consumer control。
- HID system control。
- HID gamepad。
- HID vendor IN/OUT/Feature。
- custom HID report descriptor 登録。
- report descriptor 取得テスト用の安定した descriptor。
- report ID あり/なしの両方。
- output report / feature report の送受信。
- boot protocol / report protocol 切替への対応。

既存 Host 側テストとの対応:

- `peer/hid_consumer_control`
- `peer/hid_system_control`
- `peer/hid_gamepad`
- `peer/hid_vendor`
- `peer/custom_hid`
- `peer/hid_logic`

### CDC ACM

必要機能:

- CDC ACM composite interface。
- device to host 送信。
- host to device 受信。
- line coding / control line state callback。
- baudrate、parity、stop bits、data bits の設定イベントをテスト可能にする。

既存 Host 側テストとの対応:

- `peer/usb_serial`

### USB MIDI

必要機能:

- MIDI streaming interface。
- USB MIDI event packet の raw send/receive。
- note on/off、control change、program change、pitch bend、channel pressure、poly pressure。
- SysEx 送受信。

既存 Host 側テストとの対応:

- `peer/usb_midi`

### USB Mass Storage

必要機能:

- BOT MSC device。
- 1 LUN から開始し、後で複数 LUN。
- block size 512。
- inquiry / capacity / test unit ready / request sense。
- read10/write10。
- out-of-range rejection。
- write failure injection。
- synchronize cache。
- removable / writable flag。

既存 Host 側テストとの対応:

- `peer/usb_msc`

MSC は実装量が大きいので、HID/CDC/MIDI の後に着手してよいです。

### USB Audio

旧`onPcm()` / `onData()` callback型Audio Card APIは廃止した。現在は
`EspUsbAudioFunction`へPlayback/Capture streamを追加し、bounded FIFOを
`read()` / `write()`でpollする。control/stream変更も固定長queueから`pollEvent()`で読む。
TinyUSB taskはbounded copyとstate更新だけを行い、user code、I2S、codec、DSPを実行しない。

このライブラリの責務はUSB Audio classとPCM FIFO境界までに限定する。受け取ったPCMは
application、PCMFlow、PCMFlowDeviceなどへ渡し、I2S、codec、DAC、microphone等の
hardware接続はこのライブラリでは扱わない。volume/mute DSPも暗黙適用しない。

既存 Host 側テストとの対応:

- `peer/usb_audio_speaker`
- `peer/usb_audio_microphone`

AudioはUAC1をdefault、UAC2を明示選択とし、speaker/microphone/duplexの
Peer streaming（S3, UAC1/FS）まで追加済みです。残作業は、対応Hostを用いた
UAC2 streaming、M5 speaker実音確認、複合Audio deviceのPeer確認です。

## テスト計画

### テスト体系

新リポジトリでは、最終的に以下の構成を目指します。

```text
tests/
  unit/       descriptor、HID usage、report builder などホスト不要の単体テスト
  peer/       2台構成。Host は EspUsbHost、Device は EspUsbDevice
  loopback/   P4 1台構成。Host も Device も EspUsb 系
  probe/      P4 port / speed / PHY / PC enumeration の切り分け
  manual/     物理デバイスや目視確認が必要なもの
```

### Peer テスト

目的は、既存 Arduino-ESP32 device peer を `EspUsbDevice` に置き換えることです。

初期移行順:

1. `hid_keyboard`
2. `hid_mouse`
3. `hid_keyboard_mouse`
4. `custom_hid`
5. `hid_vendor`
6. `hid_consumer_control`
7. `hid_system_control`
8. `hid_gamepad`
9. `usb_serial`
10. `usb_midi`
11. `usb_msc`
12. `usb_audio_speaker`
13. `usb_audio_microphone`

各 peer device sketch は serial command で挙動を制御します。
例:

```text
SEND_KEY_USAGE 04
SEND_KEY_TEXT hello
SEND_MOUSE_MOVE 40 0 0
SEND_VENDOR_IN hello
EXPECT_VENDOR_OUT
FAIL_NEXT_MSC_WRITE
```

Python 側は既存と同じ `peers["device"]` を使い、host 側のシリアル出力を検証します。

### Loopback テスト

P4 1台で host と device を同時に起動するテストです。
既存の Arduino USB Device では P4 device が HS 固定になり、FS host 側 loopback が失敗しました。
`EspUsbDevice` では P4 の port/speed を明示し、以下を検証します。

- FS リンクの loopback（device は HS/UTMI PHY 上で FS 動作、host は FS 側）。これが 1台で実現できる唯一の形。
- endpoint MPS が speed と class に対して正しいこと。
- HS device + HS host の loopback は **1台では不可**（UTMI PHY が1個で共有できない）。HS リンクは 2台 peer が必要。詳細は「P4 USB ポート/PHY の実測整理」。

Loopback は最初からすべて通す必要はありません。
まずは device speed と descriptor が意図通りになっていることをログ化できる probe を作り、その後 HID keyboard から自動テスト化します。

> 【メモ / 将来の P4 peer との関係】P4 の HS パス（HS enumerate、bulk=512、UAC2/HS audio 等）は
> loopback では物理的に届かないため、将来は **P4 2台の HS peer**（両側とも無指定で HS になる）を追加して補完するのが自然。
> ただし **P4 peer は2台＋配線が要り維持コストが高く、常時 CI 的には回せない想定**。位置づけは「HS 検証用の時々／手動レイヤー」。
> 一方 **loopback は1台で回せて、P4 FS＋両スタック共存を検証できる主力**として維持する。両者は置き換えでなく補完。

### Probe

P4 は USB port と speed の切り分けが重要です。
以下の probe を用意します。

- `p4_device_fs_probe`: FS device として PC/外部 host に列挙されるか。
- `p4_device_hs_probe`: HS device として PC/外部 host に列挙されるか。
- `p4_host_fs_probe`: FS host peripheral で外部 FS device を列挙できるか。
- `p4_host_hs_probe`: HS host peripheral で外部 HS/FS device を列挙できるか。
- `p4_loopback_matrix_probe`: P4 内で host/device の組み合わせを試し、port、speed、MPS、claim 結果を出力。

## API 案

### Core

```cpp
#include "EspUsbDevice.h"

EspUsbDevice device;

void setup() {
  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4001;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice HID Keyboard";

  if (!device.begin(config)) {
    Serial.printf("DEVICE_BEGIN_FAILED %s\n", device.lastErrorName());
  }
}

void loop() {
  device.task();
}
```

`device.task()` が必要か、内部 task で動かすかは実装方式に合わせて決めます。
`EspUsbHost` と同様に `begin()` 後は内部 task で回す設計でもよいです。

### HID keyboard

```cpp
EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

void setup() {
  keyboard.onOutputReport([](const EspUsbDeviceHidKeyboardOutputReport &report) {
    Serial.printf("LED num=%u caps=%u scroll=%u\n", report.numLock, report.capsLock, report.scrollLock);
  });

  keyboard.begin();
  device.begin(config);
}

void sendA() {
  keyboard.pressUsage(ESP_USB_HID_KEY_A);
  delay(10);
  keyboard.releaseUsage(ESP_USB_HID_KEY_A);
}
```

### HID custom/vendor

```cpp
EspUsbDeviceHidVendor vendor(device, {
  .reportSize = 64,
  .inputReportId = 1,
  .outputReportId = 2,
  .featureReportId = 3,
});

vendor.onOutputReport([](const uint8_t *data, size_t len) {
  Serial.printf("VENDOR_OUT len=%u\n", (unsigned)len);
});

vendor.sendInput(data, len);
vendor.setFeature(data, len);
```

### CDC ACM

```cpp
EspUsbDeviceCdcAcm cdc(device);

cdc.onLineCoding([](const EspUsbDeviceCdcLineCoding &coding) {
  Serial.printf("CDC_LINE baud=%lu data=%u parity=%u stop=%u\n",
                coding.baud, coding.dataBits, coding.parity, coding.stopBits);
});

cdc.write("hello", 5);
```

### MSC

```cpp
EspUsbDeviceMsc msc(device);

msc.onRead([](uint32_t lba, uint32_t offset, void *buffer, uint32_t size) -> int32_t {
  return storageRead(lba, offset, buffer, size);
});

msc.onWrite([](uint32_t lba, uint32_t offset, const uint8_t *buffer, uint32_t size) -> int32_t {
  return storageWrite(lba, offset, buffer, size);
});

msc.begin({
  .blockCount = 16,
  .blockSize = 512,
  .vendor = "ESPUSB",
  .product = "MSC_PEER",
  .revision = "1.0",
});
```

MSC は block device と filesystem を分けて設計します。`EspUsbDeviceMsc` は SCSI /
READ(10) / WRITE(10) の transport と block I/O callback だけを担当し、RAM、SD、FAT image
などの使いやすさは helper class に分離します。

初期 helper は次の分担にします。

```cpp
static uint8_t storage[256 * 1024];

EspUsbDeviceMsc msc(device);
EspUsbDeviceMscRamDisk blocks(storage, sizeof(storage) / 512);

blocks.attach(msc);
```

`EspUsbDeviceMscRamDisk` は raw block I/O、peer / loopback テスト、低レベル MSC example
向けです。FAT は生成しないため、PC が通常の USB drive として mount できるとは限りません。

ファイル受け渡し用途には `EspUsbDeviceMscFatRamDisk` を追加する方針です。

```cpp
static uint8_t storage[256 * 1024];

EspUsbDeviceMsc msc(device);
EspUsbDeviceMscFatRamDisk disk(storage, sizeof(storage));

disk.volumeLabel("ESPUSB");
disk.addTextFile("README.TXT", "Drop firmware.bin and eject.\r\n");
disk.attach(msc);

disk.onEject([]() {
  if (disk.exists("FIRMWARE.BIN")) {
    // firmware update or Wi-Fi upload
  }
});
```

`EspUsbDeviceMscFatRamDisk` の最初の仕様は、実用範囲を意図的に小さくします。

- 512 byte sector 固定。
- FAT12 または小容量 FAT16 の最小 image を生成する。
- long file name は扱わず、8.3 filename を標準にする。
- root directory 直下の通常 file を対象にする。
- directory、timestamp、attribute の高度な操作は後回しにする。
- Host 書き込み中は ESP32 側で FAT を読まない。
- `SYNCHRONIZE CACHE(10)`、eject、`START STOP UNIT` 後に file scan / read を行う。
- firmware update や Wi-Fi 転送では、RAM 上に全保持する方式から始める。
- 大きな file は後で streaming / PSRAM / SD へ拡張する。

永続ストレージ用途には `EspUsbDeviceMscSdCard` を追加する方針です。SD は元から block
device で、USB MSC と FAT の相性がよいため、ユーザー向けの実用 example に向いています。
ただし Host が MSC として SD を所有している間は、ESP32 側が同じ filesystem を同時に
mount / 書き込みしない排他設計にします。eject / stop 後に ESP32 側へ所有権を戻します。

初期実装では Arduino-ESP32 の SPI `SD` を対象にし、`SDFS::readRAW()` /
`SDFS::writeRAW()` を使います。Arduino の通常 file API ではなく、sector 単位の raw I/O
として扱うことが重要です。

```cpp
#include <SD.h>
#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceMsc msc(device);
EspUsbDeviceMscSdCard sdMsc(SD);

void setup() {
  sdMsc.begin(SD_CS, SPI, 4000000);
  sdMsc.onEject([]() {
    // Host ownership ended. Device-side file access may resume here.
  });
  sdMsc.attach(msc);
  msc.mediaPresent(true);
  msc.isWritable(true);
  device.begin(config);
}
```

`EspUsbDeviceMscSdCard` の最初の仕様は以下にします。

- Arduino `SD` / SPI 接続から始める。
- sector size は 512 bytes のみ対応する。
- MSC の offset 付き read/write は内部で sector read-modify-write する。
- `readOnly(true)` で Host write を拒否できる。
- Host 所有中は ESP32 側 file API を使わないことを docs / example で強調する。
- `SD_MMC` 対応は、同じ raw sector API で扱えることを確認してから追加する。

内蔵 flash、SPIFFS、LittleFS を USB MSC として直接公開する標準 API / example は作りません。
USB MSC は sector-level block device であり、SPIFFS / LittleFS は ESP32 側 filesystem API
なので抽象度が合いません。内蔵 flash は firmware partition、erase block、書き換え耐性の
制約も強いため、一般ユーザー向け導線から外します。

## 実装上の注意

### TinyUSB 統合

固定commitから選択したTinyUSB source、library所有`tusb_config.h`、ESP-IDF PHY APIを
使用し、Arduino coreの`USB.begin()` / `esp32-hal-tinyusb.c`経路は使わない。
PHY speed、TinyUSB rhport、controller capability、descriptor MPSを1つのruntimeが
一貫して所有する。pinと更新手順は`third_party/tinyusb/PROVENANCE.ja.md`に記録する。

### descriptor と runtime speed

High Speed capable device は、単に HS descriptor を出すだけでは不十分です。
実際に FS で接続された場合に FS configuration descriptor を返す必要があります。
TinyUSB の callback と speed 判定、または separate descriptor table の扱いを実装前に確認してください。

`CFG_TUD_ENDPOINT_SIZE` のようなグローバル固定値に class descriptor を依存させると、今回と同じ問題が再発します。
各 endpoint の MPS は device config と接続 speed から決めるべきです。

### P4 の HS/FS port

P4 は host 側では HS peripheral と FS peripheral を `peripheral_map` で選べます。
Device側は`EspUsbController::{Auto, FullSpeed, HighSpeed}`で選択する。P4ではFullSpeedを
rhport 0/internal PHY、HighSpeedをrhport 1/UTMIへmapする。S2/S3はFullSpeedのみで、
未対応controller指定を暗黙fallbackせず失敗させる。

### エラーログ

テストしやすさのため、初期化と enumeration 周辺では以下を出力できるようにします。

- selected port
- requested speed
- actual TinyUSB rhport
- actual connected speed
- descriptor endpoint MPS
- VID/PID
- interface count
- endpoint list
- last error name

P4 loopback では、このログが原因切り分けに直結します。

## 既存テストからの移行メモ

### `peer/hid_keyboard`

現状は peer device が `USBHIDKeyboard` を使っています。
LED テストは Arduino-ESP32 側の `USBHID.cpp` 実装都合で自動テストから除外されています。
`EspUsbDevice` では keyboard output report callback を実装し、NumLock/CapsLock/ScrollLock の自動テストを復活させます。

### `peer/hid_keyboard_mouse`

Composite HID の基本テストです。
`EspUsbDevice` では keyboard と mouse を別 interface にするか、report ID 付き単一 HID interface にするかを選べるとよいです。
Arduino TinyUSB runtime では複数 HID interface の列挙制約が出るため、MVP は report ID 付き単一 HID interface で進めます。

### `peer/custom_hid`

Host 側は report descriptor 取得と raw input dump を検証しています。
Device 側は任意 descriptor と raw report 送信を安定して行う必要があります。
これは `EspUsbDevice` の設計妥当性を見る良い初期テストです。

### `peer/hid_vendor`

Interrupt IN/OUT と Feature report の往復テストに使います。
output report と feature report の callback を分けて実装してください。

### `peer/usb_serial`

CDC ACM の line coding 設定テストがあります。
Arduino 標準 `USBCDC` 依存を置き換えるには、line coding event をテスト用にログ出力できる必要があります。

### `peer/usb_midi`

MIDI event packet の raw 送受信ができれば、上位 helper は後から足せます。
まずは packet 単位の API を作るべきです。

### `peer/usb_msc`

現在のテストはかなり具体的です。
容量、Inquiry、Sense、Read/Write、範囲外拒否、失敗注入まで含みます。
MSC は `EspUsbDevice` の中盤以降のマイルストーンにします。

### `loopback/hid_keyboard`

初期検討では P4 上で `EspUsbHost` と Arduino `USBHIDKeyboard` を同時に使う構成を試しました。
Arduino device 側が HS 固定で MPS 512 を出し、FS host 側で claim できないことが分かったため、
現在は `EspUsbHost` と `EspUsbDeviceHidKeyboard` の組み合わせへ移行しています。

`EspUsbDevice` 側では以下の matrix を明示的に扱います。

| Device | Host | 期待 |
|--------|------|------|
| HS(UTMI) device / FS 動作 | FS host | 1台 loopback の実現構成。デバイスは HS PHY 固定だが FS でネゴ |
| HS device | HS host | **1台 P4 では不可**（UTMI PHY が1個で共有できず PHY 衝突）。HS リンクは2台構成で検証 |
| — | — | デバイス側は HS(UTMI) 固定なので「FS device port を選ぶ」構成は core 改造なしには作れない |

（上表は 2026-07 の実機確認に基づく。詳細は「P4 USB ポート/PHY の実測整理」を参照。）

## リポジトリ構成案

```text
EspUsbDevice/
  library.properties
  README.md
  README.ja.md
  src/
    EspUsbDevice.h
    EspUsbDevice.cpp
    EspUsbDeviceTypes.h
    EspUsbDeviceDescriptors.h
    EspUsbDeviceHid.h
    EspUsbDeviceHidKeyboard.h
    EspUsbDeviceHidMouse.h
    EspUsbDeviceHidVendor.h
    EspUsbDeviceCdcAcm.h
    EspUsbDeviceMidi.h
    EspUsbDeviceMsc.h
  examples/
    HID/EspUsbDeviceKeyboard/
    HID/EspUsbDeviceKeyboardJis/
    HID/EspUsbDeviceMouse/
    HID/EspUsbDeviceCompositeHID/
    HID/EspUsbDeviceHIDVendor/
    CDC/EspUsbDeviceCdcAcm/
    MIDI/EspUsbDeviceMidi/
    MSC/EspUsbDeviceMscRamDisk/
  tests/
    unit/
    peer/
    loopback/
    probe/
```

## マイルストーン

### Milestone 1: HID keyboard/mouse MVP

- Core device 初期化。
- P4/S3 build。
- speed/port config。
- descriptor 生成。
- HID keyboard raw report。
- HID keyboard LED output report callback。
- HID mouse raw report。
- peer `hid_keyboard`、`hid_mouse`、`hid_keyboard_mouse` 移行。

### Milestone 2: P4 loopback

- P4 device FS/HS probe。
- P4 loopback matrix probe。
- loopback `hid_keyboard` を EspUsbHost + EspUsbDevice で再実装。
- endpoint MPS ログとアサーション。

### Milestone 3: HID generalization

- custom HID descriptor。
- vendor HID IN/OUT/Feature。
- consumer/system/gamepad。
- report ID あり/なし。
- peer HID 系を全移行。

### Milestone 4: CDC and MIDI

- CDC ACM。
- line coding/control line state。
- MIDI raw packet。
- MIDI helper。
- peer `usb_serial`、`usb_midi` 移行。

### Milestone 5: MSC and Audio

- MSC RAM disk。
- failure injection。
- multi-block read/write。
- MSC FAT RAM disk helper。
- MSC SD card helper。
- Audio sink。
- `AudioSpeaker` / `AudioSpeakerM5` の manual 確認。
- USB Audio loopback / microphone path / composite Audio の検討。

## 完了条件

初期リリースの完了条件は以下です。

- Arduino 標準 `USB.begin()` を使わずに、EspUsbDevice 単体で HID keyboard/mouse device として列挙できる。
- ESP32-S3 peer device として既存 HID keyboard/mouse テストを置き換えられる。
- ESP32-P4 で device speed と endpoint MPS を明示的にログ化できる。
- P4 loopback で FS device + FS host、または少なくとも HS device + HS host の HID keyboard テストが通る。
- HID output report callback で keyboard LED の自動テストが可能。
- raw HID usage API により JIS 固有キーのテストを設計できる。

## 新リポジトリ作業時の最初の指示案

新しいリポジトリで作業を開始するときは、以下のように指示するとよいです。

```text
EspUsbDevice という Arduino ライブラリを新規作成してください。
この文書 docs/EspUsbDevice_HANDOFF.ja.md を設計仕様として読み、まず Milestone 1 の HID keyboard/mouse MVP を実装してください。
Arduino-ESP32 標準 USB.begin()/USBHIDKeyboard は使わず、TinyUSB/ESP-IDF の device API を直接使う方針で進めてください。
最初のテスト対象は EspUsbHost の peer/hid_keyboard、peer/hid_mouse、peer/hid_keyboard_mouse の置き換えです。
P4 では device port/speed と endpoint MPS をログ出力できるようにしてください。
```
