# Vendor（独自の bulk 転送）

[English](vendor.md)

## 概要と使う場面

`EspUsbDeviceVendor` は、class 0xFF（Vendor Specific）の interface を1つ作り、bulk endpoint の IN/OUT 1組で独自形式のデータをやり取りする Function です。
標準の class の中で最も速く大量のデータを送れます。Vendor 要求（control 転送）も受けられます。

使う場面の目安です。

- 専用の Host アプリ（libusb、pyusb、WinUSB、WebUSB など）で、独自の protocol を使う。
- 大量のデータを速く送る。
- serial terminal から使いたいなら [CDC](cdc.ja.md)、ドライバなしで小さなデータを送るなら [HID の Vendor](hid.ja.md) を使います。

Vendor の interface は serial terminal からは開けません。Host 側はアプリで interface を選んで開きます。

送受信の仕組み（ring、DMA、借用転送、取消し、標準要求による初期化）は [CDC](cdc.ja.md) と共通で、この文書で説明します。

動作は、ESP32-S3 同士の FS 接続、ESP32-P4 一台の FS loopback（PSRAM の ring を含む）、旧 P4 と P4X の HS 直結（両方向）で確認しています。PC の OS との相互接続は未確認です。

用語：

- **MPS**（Max Packet Size）：1 packet の最大長。bulk では FS が 64 byte 以下、HS が 512 byte。
- **ring**：送受信データを一時的にためる環状 buffer。DMA は直接使いません。
- **借用転送**：アプリの buffer をコピーせず、そのまま DMA に渡す送受信（`writeDirect` / `readDirect`）。
- **ZLP**（Zero Length Packet）：長さ 0 の packet。MPS の倍数ちょうどで終わる転送の終わりを Host へ知らせます（USB 2.0 §5.8.3）。

## 基本の使い方

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceVendor vendor(device);

void setup() {
  EspUsbVendorConfig config;      // EspUsbBulkStreamConfig の別名
  config.rxCapacity = 2048;
  config.txCapacity = 4096;
  if (vendor.configure(config) != ESP_OK) return;  // 停止中だけ成功する
  if (!device.begin()) {
    // 失敗理由は device.lastError() / device.lastErrorName()
    return;
  }
}

void loop() {
  uint8_t buffer[64];
  const size_t n = vendor.read(buffer, sizeof(buffer));  // 届いている分だけ返す
  if (n) vendor.write(buffer, n);  // 受け付けた byte 数を返す（全部とは限らない）
}
```

## 設定

`EspUsbVendorConfig`（`EspUsbBulkStreamConfig` と同じ型）の全項目です。

| 項目 | 既定値 | 範囲・意味 | FS/HS の違い |
| --- | --- | --- | --- |
| `rxCapacity` | 512 | 受信 ring の容量（byte）。`rxMode = Stream` では 1 以上、それ以外では 0 | なし |
| `txCapacity` | 512 | 送信 ring の容量（byte）。0 なら `write()` は使えず、借用送信だけになる | なし |
| `rxBuffer` / `txBuffer` | `nullptr` | ring の領域を自分で渡すときに指定。`nullptr` なら begin で確保。PSRAM でもよい | なし |
| `transferBytes` | 512 | コピー送信で1回に DMA へ渡す最大 byte 数（1〜65535）。`txCapacity` が 1 以上なら 0 は不可 | MPS 未満でも受け付ける |
| `autoFlush` | `true` | `true` なら `write()` のたびに送信を始める。`false` なら `flush()` まで溜める | なし |
| `streamTermination` | `FunctionDefault` | コピー送信の終わりに ZLP を付けるか。Vendor の `FunctionDefault` は `Continue`（付けない） | ZLP が付く長さが MPS で変わる |
| `rxMode` | `Stream` | 受信の方式。`Stream` / `Callback` / `Borrowed` / `Disabled`（[受信の方式](#受信の方式)） | なし |

constructor の第2引数 `endpointSize`（既定 64）は、FS 接続のときの bulk MPS です。

| `endpointSize` | 結果 |
| --- | --- |
| 8、16、32、64 | その値を FS の MPS にする |
| 64 より大きい値 | 64 として扱う |
| それ以外（例：10） | begin が `ESP_ERR_INVALID_SIZE` |
| （HS 接続） | 常に 512 |

### 設定と開始・停止の規則

- `configure()` は停止中だけ成功します。動作中は `ESP_ERR_INVALID_STATE`、値の不正は `ESP_ERR_INVALID_ARG` を返し、どちらも元の設定を保ちます。
- ring と DMA 用 buffer は begin で確保します。失敗すると確保したものを戻し、USB は開始しません。
- 設定を変えるときは `device.end()` → `configure()` → `device.begin()` の順にします。再ビルドは要りません。Host からは切断と再接続に見えます。
- callback（`onRx`、`onRxData`、`onTxComplete`、`onDirectTxComplete`、`onDirectRxComplete`、`onControlRequest`）の登録も停止中だけです。動作中は `ESP_ERR_INVALID_STATE` を返します。
- 指定した大きさを黙って縮めることはしません。

`configure()` が `ESP_ERR_INVALID_ARG` を返す条件：

- `txCapacity` が 1 以上なのに `transferBytes` が 0
- `rxMode = Stream` で `rxCapacity` が 0、または `Stream` 以外で `rxCapacity` が 0 でない
- `rxBuffer` / `txBuffer` を渡したのに対応する容量が 0、または RX と TX の領域が重なっている
- `streamTermination` や `rxMode` が範囲外

begin の失敗理由（`device.lastError()` で読む）：

| 値 | 原因 |
| --- | --- |
| `ESP_ERR_INVALID_ARG` | `rxMode = Callback` で `onRxData` が未登録、または `rxMode = Borrowed` で `onDirectRxComplete` が未登録 |
| `ESP_ERR_INVALID_SIZE` | `endpointSize` が 8/16/32/64 でない（64 を超える値は 64 として扱う）。または `device.lastError()` で、構成全体の IN endpoint が controller の送信 FIFO に収まらない（[FS と HS の違い](#fs-と-hs-の違い)） |
| `ESP_ERR_NO_MEM` | ring や DMA 用 buffer を確保できなかった |
| `ESP_ERR_INVALID_STATE` | 既に開始している |

## API

### 状態と容量

| 関数 | 説明 |
| --- | --- |
| `mounted()` | Host が構成を選び、endpoint が開いている |
| `configuration()` | 要求した設定（`FunctionDefault` などもそのまま） |
| `readCapacity()` / `writeCapacity()` | 実際に確保した容量。開始前と停止後は 0 |
| `endpointSize()` | constructor で指定した FS の MPS |

### コピー方式の送信

| 関数 | 戻り値・動作 |
| --- | --- |
| `write(data, n)` / `write(byte)` | ring へコピーし、受け付けた byte 数を返す。足りなければ一部だけ。待たない |
| `flush()` | 溜まったデータの送信を始める。完了は待たない |
| `availableForWrite()` | 今 `write()` が受け付ける byte 数（`int`。大きい値は `INT_MAX`） |
| `writeAvailable()` | 同じ値を `size_t` で返す |
| `waitWritable(n, timeoutMs = 100)` | 空きが n byte 以上になるまで待つ。n が `writeCapacity()` より大きいとすぐ false。1 tick ごとの polling |
| `onTxComplete(cb)` | コピー送信の DMA 完了の通知（送った byte 数） |

- 戻り値は「ring に入った量」です。Host へ届いたことは意味しません。戻った時点で元の buffer は再利用できます。
- 借用送信中、停止中、TX ring がないとき、IN endpoint が HALT 中は、空きは 0 です。照会しても領域は予約されないので、`write()` の戻り値も確認してください。
- `EspUsbDeviceVendor` は Arduino の `Print` を実装しているので、`print()` も使えます。

### コピー方式の受信

| 関数 | 戻り値・動作 |
| --- | --- |
| `available()` | 読める byte 数 |
| `read(buffer, size)` | 届いている分だけ返す。待たない |
| `read()` | 1 byte（なければ -1） |
| `peek()` | 先頭の 1 byte を消費せずに返す（空なら -1） |
| `onRx(cb)` | 受信のたびに `available()` の値を通知 |

ring が 1 packet より小さくても受信データは失われません。入りきらない残りは DMA 用 buffer に置いたまま、読み終わるまで次の受信を止めます（Host には NAK を返して待たせます）。そのため `available()` が `readCapacity()` を超えることがあります。

### メッセージの区切りと ZLP

コピー方式は byte stream です。複数の `write()` がまとめて送られることがあり、1回の `write()` が Host の1回の read になるとは限りません。Host 側は、長さを protocol に含めるなどして区切ってください。

| `streamTermination` | 動作 |
| --- | --- |
| `FunctionDefault` | Vendor では `Continue`、CDC では `EndWithShortPacket` |
| `Continue` | ring が空になっても ZLP を送らない |
| `EndWithShortPacket` | 送信が MPS の倍数で終わり、ring が空なら ZLP も送る |

「この転送をここで終える」と明示したい場合は、借用送信の `EndWithShortPacket` を使います。

### 受信の方式

| `rxMode` | 受け取り方 | `rxCapacity` |
| --- | --- | --- |
| `Stream` | `available()` / `read()` で読む | 1 以上 |
| `Callback` | `onRxData(data, length)` で受け取る。`data` は callback の間だけ有効 | 0 |
| `Borrowed` | `readDirect()` でアプリの buffer を渡して受け取る | 0 |
| `Disabled` | 受信しない。OUT endpoint は descriptor に残るが、受信の準備をしない | 0 |

`Disabled` では `available()` / `read()` は 0、`readDirect()` は `NotSupported` です。

### 借用転送

大きなデータや速度が必要なときは、アプリの buffer をそのまま DMA に渡せます。

buffer の条件（送信・受信とも）：

- 先頭アドレスが 64 byte 境界
- 全体が DMA で使える内部 RAM。PSRAM は使えません（`NotDmaCapable`）
- 完了 callback が呼ばれるまで、変更・解放しない

```cpp
// begin 前に登録する
vendor.onDirectTxComplete([](const EspUsbTransferResult &result) {
  // result.buffer がアプリへ戻った。result.status と result.actualBytes を確認する
});

// begin 後
auto submission = vendor.writeDirect(dmaBuffer, length,
                                     EspUsbTransferTermination::EndWithShortPacket);
if (!submission) {
  // 受理されなかった。理由は submission.error。buffer はアプリのまま
}
```

**借用送信（`writeDirect`）**

- 1つの instance で同時に進められる借用送信は1つです。コピー送信のデータが残っている間も `Busy` です。
- `Continue` は ZLP を付けません。`EndWithShortPacket` は長さが MPS の倍数なら ZLP まで送ってから完了します。
- 途中に短い packet や ZLP は入りません。転送全体に対して完了は1回です。
- 長さの上限は `maxDirectTransfer()` で分かります（32 bit の ESP32 では 2147483647、停止中は 0）。controller の1回の DMA 設定に収まらない長さは、ライブラリが内部で分けます。

**借用受信（`readDirect`）**

```cpp
EspUsbVendorConfig config;
config.rxMode = EspUsbVendorRxMode::Borrowed;
config.rxCapacity = 0;
config.txCapacity = 0;     // コピー送信も使わないなら 0
config.transferBytes = 0;
vendor.configure(config);
vendor.onDirectRxComplete([](const EspUsbTransferResult &done) {
  // done.actualBytes 分だけが受信データ
});
device.begin();

auto submission = vendor.readDirect(dmaBuffer, capacity);
```

- `capacity` は 64 の倍数で、かつ接続中の MPS の倍数です（FS は 64 の倍数、HS は 512 の倍数）。合わないと `BadArgument` です。
- buffer がいっぱいになるか、短い packet か ZLP を受けると完了します。
- 次の受信は、改めて `readDirect()` で投稿します。投稿していない間は OUT を受け付けず、Host を待たせます。callback の中から次の buffer を投稿できます。
- 上限は `maxDirectReceive()` で分かります。

**投稿の結果（`EspUsbTransferSubmission`）**

`if (submission)` で受理を確認します。拒否されたときは完了 callback は来ず、buffer はアプリのままです。

| `error` | 意味 |
| --- | --- |
| `None` | 受理された。`id` は 0 でない |
| `NotSupported` | 今の `rxMode` ではその操作を使えない |
| `NotMounted` | 未接続、または endpoint が開いていない |
| `BadArgument` | buffer や長さが不正、完了 callback が未登録、受信容量が MPS/64 の倍数でない |
| `NotAligned` | 先頭アドレスが 64 byte 境界でない |
| `NotDmaCapable` | DMA で使えないメモリ（PSRAM など） |
| `Busy` | 同じ方向で転送が進行中 |
| `TransferFailed` | controller が受け付けなかった |

**完了の結果（`EspUsbTransferResult`）**

完了 callback は USB worker で1回だけ呼ばれます。`buffer`、`requestedBytes`、`actualBytes`、`status`、`actualBytesKnown`、`id` が入ります。

| `status` | 意味 |
| --- | --- |
| `Success` | 完了。`actualBytes` が有効。Host のアプリが処理したことまでは意味しない |
| `BusReset` | bus reset などで破棄された。どこまで送れたかは不明（`actualBytesKnown = false`） |
| `Stopped` | `end()` で DMA を止めてから返した。どこまで送れたかは不明 |
| `TransferError` | 下位の転送エラー（HALT を含む） |
| `Cancelled` | 取消しによって返した |

callback の中でしてよいこと：準備済みの次の buffer の投稿、他の task への通知。
してはいけないこと：待つ処理、`end()`、設定変更、Function の破棄。

**取消し**

- `cancelDirectTx(id)` / `cancelDirectRx(id)` で取消しを頼めます。`id` を省略（0）すると現在の転送が対象です。
- `ESP_OK` は「取消しを受け付けた」という意味で、buffer が戻ったわけではありません。buffer は完了 callback（`Cancelled` など）で戻ります。
- 対象がない、または `id` が違うと `ESP_ERR_INVALID_STATE` です。古い `id` で新しい転送を取り消すことはありません。controller が対応していなければ `ESP_ERR_NOT_SUPPORTED` です。
- `id` は begin/end をまたいでも増え続けます。
- コピー方式の `write()` / `read()` は取消しの対象外です。

### Vendor 要求（control 転送）

`onControlRequest(callback)` で、この interface 宛ての Vendor/Class 要求を受け取ります。callback は SETUP、DATA、ACK の各段階で呼ばれます。
data 段階の送受信は `sendControlResponse(request, data, length)` で始めます（IN なら送るデータ、OUT なら受信先）。

```cpp
vendor.onControlRequest([&](const EspUsbDeviceVendorControlRequest &r) {
  if (r.bmRequestType != 0x41 || r.bRequest != 0x11 ||
      !r.wLength || r.wLength > sizeof(commandBuffer)) return false;
  if (r.stage == ESP_USB_DEVICE_CONTROL_STAGE_SETUP)
    return vendor.sendControlResponse(r, commandBuffer, r.wLength);  // OUT の受信先
  if (r.stage == ESP_USB_DEVICE_CONTROL_STAGE_DATA)
    return r.actualBytes == r.wLength && validate(commandBuffer, r.actualBytes);
  if (r.stage == ESP_USB_DEVICE_CONTROL_STAGE_ACK)
    apply(commandBuffer, r.actualBytes);
  return true;
});
```

- SETUP/DATA で `false` を返すと要求を拒否（STALL）します。受け付けるかの検査は DATA で、反映は ACK で行います。ACK で `false` を返しても完了した要求は取り消せません。
- `actualBytes` は data 段階で実際に転送した byte 数です。SETUP では 0 です。
- 受信先や送信元の buffer は、control 転送が終わるまで有効にしておきます。
- `rawRequest` は応答用の内部参照です。保存して後から応答する用途には使えません。
- **bRequest 0x01 と 0x02 は避けてください。** ライブラリは次の 2 つの要求だけを、この callback より先に自分で答えます。
  - WebUSB の GET_URL：bRequest 0x01、bmRequestType 0xC0、wIndex 2（`webusbEnabled` のときだけ）。
  - Microsoft OS 2.0 descriptor set：bRequest 0x02、bmRequestType 0xC0、wValue 0、wIndex 7（descriptor set を作ったときだけ）。

  bRequest が 0x01／0x02 でも、これ以外の要求は `onControlRequest()` に届きます。それでも別の値を使うほうが、独自 protocol と紛れません。

## 標準要求による転送の初期化

Host から次の標準要求が来ると、Vendor と CDC の bulk 転送は対象の転送を止めて初期化します。借用中の buffer は、DMA を止めて古い完了を処理し終えてから1回だけ返します。

| Host の要求 | 対象 | 借用中の転送の結果 | data toggle |
| --- | --- | --- | --- |
| SET_INTERFACE（alt 0） | 指定した interface の送受信 | `Cancelled` | 両方向 DATA0 に戻す |
| SET_FEATURE(ENDPOINT_HALT) | 指定した endpoint だけ | `TransferError` | HALT のまま |
| CLEAR_FEATURE(ENDPOINT_HALT) | 指定した endpoint だけ | `Cancelled` | DATA0 に戻す |

- 他の Function や、指定されなかった方向の転送には影響しません。
- コピー方式でも、対象方向の ring は捨てられます。`write()` で受け付け済みでも未送信のデータは失われることがあります。確実に届ける必要があるなら、アプリ側で確認と再送をしてください。
- IN が HALT 中、または初期化の完了待ちの間は、`write()` と `writeAvailable()` は 0 です。
- 監視したいときは `device.onAnyControlRequest()` を使います。

## FS と HS の違い

| 項目 | FS（12 Mbps） | HS（480 Mbps） |
| --- | --- | --- |
| bulk MPS | 64（`endpointSize` で 8/16/32 にもできる） | 512 |
| `transferBytes` | 1〜65535。MPS 未満でもよい | 同左 |
| `readDirect()` の容量 | 64 の倍数 | 512 の倍数 |
| ZLP が付く長さ | MPS の倍数 | 512 の倍数 |

- 非 control の IN endpoint は、ESP32-S3 で 4本、P4 の HS controller で 7本までです。Vendor は IN を1本使います。
- IN endpoint はそれぞれ、controller の送信 FIFO に少なくとも 1 packet 分が要ります。FIFO は全 endpoint で共有します（ESP32-S2/S3 と P4 の FS controller は 256 word、P4 の HS controller は 1024 word＝4 KiB。受信 FIFO を含む）。`device.begin()` は構成全体を検査し、収まらなければ `ESP_ERR_INVALID_SIZE` で失敗します。列挙の後で endpoint を開けない Device にしないためです。P4 の HS controller では、既定設定の Vendor は 5 個まで収まり、6 個（512 byte の bulk IN が 6 本）は収まりません。
- 連続して Host へ送る場合、各 bulk IN endpoint の送信 FIFO を2 packet 分にすると速くなることがあります。`EspUsbDeviceConfig::bulkInBuffering`（既定 `Auto`：全部入るときだけ2倍にする）で選べます。`device.bulkInDoubleBuffered()` で結果を確認できます。

## バッファとメモリ

| 領域 | 置ける場所 |
| --- | --- |
| `rxBuffer` / `txBuffer`（ring） | 内部 RAM でも PSRAM でもよい。境界の条件もない |
| 自動確保の ring | 通常の `malloc` で確保（置き場所は sdkconfig の malloc 設定による） |
| DMA 用 buffer とコピー送信の staging | ライブラリが DMA 可能な内部 RAM（64 byte 境界）に確保 |
| 借用転送の buffer | DMA 可能な内部 RAM が必要。PSRAM は `NotDmaCapable` で拒否 |

- `rxBuffer` / `txBuffer` はライブラリが解放しません。begin から end まで有効に保ってください。
- HS に対応した controller（P4 の HS controller）では、受信用の DMA buffer を 512 byte で確保します。FS で接続した場合も同じ大きさです。
- `device.end()` が `ESP_OK` を返したら、確保した資源は解放済みで、借用中の buffer もすべて返っています。USB の callback の中から `device.end()` を呼ぶと `ESP_ERR_INVALID_STATE` で拒否されます。
- Vendor を使わないプログラムには、Vendor の実装はリンクされません。

## Host OS 側の扱い

| OS | 必要なもの | 分かっていること |
| --- | --- | --- |
| Windows | WinUSB | ライブラリは Vendor の interface ごとに Microsoft OS 2.0 descriptor（compatible ID `WINUSB` と `DeviceInterfaceGUIDs`）を自動で出すので、INF なしで WinUSB が結び付く設計です |
| Linux | なし（libusb、pyusb） | 一般ユーザーで開くには udev 規則が要る |
| macOS | なし（libusb、pyusb） | 標準の class driver が結び付かないので、libusb から開ける |

- Windows で Host アプリが見つける GUID は `EspUsbDeviceConfig::deviceInterfaceGuid` で指定します。既定の GUID はこのライブラリの全製品で共通なので、製品では自分の GUID を指定してください。
- 単一 interface の装置では compatible ID を set header の直下に置き、複数 interface の装置では function subset に入れます（`msOs20Layout = AUTO`）。Windows は function subset を usbccgp（複合装置用の親ドライバ）経由でしか使わないためです。
- Windows は descriptor から読んだ registry の値（`DeviceInterfaceGUIDs`）を、VID/PID/serial ごとに保存します。値の変化は `wVendorRevision` が変わったときだけ読み直します。ライブラリは既定で、この値を descriptor の内容から求めるので、内容が変われば値も変わります。
- WebUSB を使う場合は `EspUsbDeviceConfig::webusbEnabled = true` にし、必要なら `webusbUrl` を指定します。
- 上の Windows の振る舞いは v2 で同じ形式の descriptor を使って確認したものです。v3 では Windows 実機で未確認です。macOS も未確認です。
- 詳しい確認方法は [USB デバイス開発ガイド（応用）](../usb-device-advanced.ja.md) を参照してください。

Host 側で詰まりやすい点：

- echo の量が Device の RX と TX の保存量の合計を超える場合、Host が OUT を全部送り終えてから IN を読むと、お互いに待ち続けます。Host は送信と受信を並行して行ってください。
- `Continue` では MPS の倍数で終わる送信に ZLP が付きません。Host が「短い packet が来るまで」読む作りなら、長さを protocol に含めるか、`EndWithShortPacket` を使います。

## 規格との対応と設計の理由

- **FS の MPS を 8/16/32/64 に限る理由**：USB 2.0 §5.8.3 が FS の bulk endpoint に認める値がこの4つだからです。HS の bulk は 512 だけです。
- **ZLP**：USB 2.0 §5.8.3 では、転送が MPS の倍数ちょうどで終わったとき、Host は次の packet を待ちます。終わりを伝える必要があるときだけ ZLP を送ります。
- **instance 単位の設定**：容量を build 設定ではなく `configure()` で持つので、1つの binary で構成を変えられ、使わない資源を確保しません。代わりに begin での確保失敗と、停止してからの再設定が必要です。
- **投稿の結果を戻り値で返す**：呼び出しごとに理由が分かり、続く呼び出しで上書きされません。
- **借用受信を投稿式にした理由**：内部の packet buffer が要らず、アプリが buffer の数を決められます。容量を MPS と 64 の倍数にして領域全体を DMA に渡すことで、P4 などで cache の操作が周りの変数を壊すことを防ぎます。
- **取消しを要求と返却に分けた理由**：DMA を止め、USB worker の queue で古い完了を処理し終えてから返すので、buffer がまだ DMA に使われている間にアプリへ戻ることがありません。
- **`waitWritable()` が polling の理由**：待ち用の同期資源を常に確保しないためです。

## v2 からの変更点

- `build_opt.h` での容量や方式の指定、`CFG_TUD_VENDOR` は使いません。容量は `configure()` で指定します。
- callback の登録は開始前に行います。動作中の登録はエラーになります。
- 借用送信は `EspUsbTransferSubmission` を返します。`if (!vendor.writeDirect(...))` の形はそのまま使えます。
- `flush()` は送信を始めるだけで、完了を待ちません。暗黙の ZLP に頼っていた Host 側の read は、終端の条件を見直してください。

詳しくは [v2 からの移行](../migrating-from-v2.ja.md) を参照してください。

## 関連する examples

| example | 内容 |
| --- | --- |
| [USBVendor](../../examples/USBVendor/) | bulk の echo と Vendor 要求、WebUSB |
| [FirmwareVendor](../../examples/FirmwareVendor/) | Vendor bulk でのファームウェア更新 |

規格：[USB 2.0](https://www.usb.org/document-library/usb-20-specification)、[Microsoft OS 2.0 Descriptors](https://learn.microsoft.com/windows-hardware/drivers/usbcon/microsoft-os-2-0-descriptors-specification)、[WebUSB](https://wicg.github.io/webusb/)
