# スマートカードリーダー（CCID）

> English: [ccid.md](ccid.md)

`EspUsbDeviceCcid` は、USB Host から「IC カードリーダー」に見える Function です（USB の CCID class、Chip/Smart Card Interface Devices）。
リーダーの部分はライブラリが受け持ちます。挿さっているカードはスケッチが演じます。

- カードの有無と ATR（カードが電源投入時に返す識別情報）を `insertCard()` で決めます。
- Host から届く APDU（カードへの命令）に `onApdu()` で答えます。

## 概要と使う場面

- PC の PC/SC アプリから、ESP32 を「カードが挿さったリーダー」として使わせたいとき。
- 認証用トークンやカードの試作、Host 側ソフトの試験に。

扱う範囲：

| 項目 | 内容 |
| --- | --- |
| slot | 1 つ |
| protocol | T=1 |
| 交換の単位 | short APDU（命令データ最大 255 byte、応答データ最大 256 byte ＋ SW1 SW2） |
| 扱わない | extended APDU、chaining、PIN pad、clock やデータ速度の交渉、T=0 |

APDU の中身（status word を含む）はライブラリが解釈しません。スケッチがそのまま返します。

## 基本の使い方

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceCcid reader(device);

void setup() {
  EspUsbCcidConfig config;               // messageBytes は既定の 0（自動）でよい
  config.name = "Card reader";
  if (reader.configure(config) != ESP_OK) return;

  const uint8_t atr[] = {0x3b, 0x80, 0x01, 0x81}; // T=1 のカード
  reader.insertCard(atr, sizeof(atr));

  reader.onApdu([](const uint8_t *apdu, size_t length, uint8_t *reply, size_t capacity) -> size_t {
    // USB worker task 上で呼ばれる。応答（SW1 SW2 を含む）を reply に書き、長さを返す
    if (capacity < 2) return 0;
    reply[0] = 0x6d; reply[1] = 0x00;    // 「命令を知らない」
    return 2;
  });

  if (!device.begin()) { /* device.lastError() を確認 */ }
}

void loop() {}
```

設定を変えるときは `device.end()` → `configure()` → `device.begin()` の順です。

## 設定

### `EspUsbCcidConfig`

`configure()` は停止中だけ受け付けます。

| 項目 | 既定値 | 範囲・意味 | FS/HS の違い |
| --- | --- | --- | --- |
| `messageBytes` | 0（自動） | 1 つの CCID message（10 byte の header 込み）の最大 byte 数。0 なら begin で `max(271, Bulk-OUT MPS)` に決まる。明示するなら 64〜65535 | 自動の値は FS 271、HS 512。明示値が Bulk-OUT MPS 未満なら begin が `ESP_ERR_INVALID_SIZE` |
| `removable` | `true` | `true`：抜き差しでき、変化を通知 endpoint で Host へ知らせる。`false`：カード固定で、通知 endpoint を持たない | なし |
| `name` | `nullptr` | interface の名前（文字列 descriptor） | なし |

**messageBytes の決まり方**

- 271 は short APDU がちょうど収まる大きさです（命令 10＋4＋1＋255＋1、応答 10＋256＋2）。
- CCID 1.1 §5.1 の `dwMaxCCIDMessageLength` は、short APDU を扱うリーダーに Bulk-OUT の wMaxPacketSize 以上を求めます。そのため HS では 512 以上が必要です。
- 明示値が MPS 未満でも、黙って切り上げません。begin が失敗します。
- 決まった値は `messageCapacity()` と descriptor の `dwMaxCCIDMessageLength` に出ます。`configuration().messageBytes` は設定したまま（自動なら 0）です。
- `onApdu`／`onEscape` の `capacity` は `messageBytes − 10` です（既定で FS 261、HS 502 byte）。
- 大きな値にしても、extended APDU や chaining が使えるようになるわけではありません。

### FS と HS の違い

「HS 対応 controller」は、ESP32-P4 で `EspUsbDeviceConfig::controller` が `FullSpeed` 以外のときです。
このとき自動値と下限に 512 を使い、Host が実際には FS でつないだ場合も同じ値を使います。

| 項目 | FS | HS |
| --- | --- | --- |
| bulk MPS（USB 2.0 §5.8.3） | 64 byte | 512 byte |
| `messageBytes` の自動値 | 271 | 512 |
| 明示できる `messageBytes` | 64〜65535 | 512〜65535（512 未満は begin が `ESP_ERR_INVALID_SIZE`） |
| callback の `capacity`（既定時） | 261 | 502 |
| 通知 endpoint | 8 byte、bInterval 16（16 ms） | 8 byte、bInterval 8（2^(8−1) × 125 µs ＝ 16 ms） |

使う endpoint は bulk IN、bulk OUT、通知 IN です（`removable = false` なら通知なし）。

## API

### 設定と開始

| 関数 | 戻り値と制約 |
| --- | --- |
| `esp_err_t configure(config)` | 稼働中：`ESP_ERR_INVALID_STATE`。`messageBytes` が 0 でも 64〜65535 でもない：`ESP_ERR_INVALID_SIZE` |
| `configuration()` | 設定した値 |
| `size_t messageCapacity()` | 稼働中の message 容量。begin 前と end 後は 0 |
| `onApdu(callback)` / `onEscape(callback)` / `onPower(callback)` | `esp_err_t`。停止中だけ。稼働中は `ESP_ERR_INVALID_STATE` |
| `startError()` | 直近の begin の失敗理由 |

`device.begin()` が失敗したときの `device.lastError()`：

| 値 | 原因 |
| --- | --- |
| `ESP_ERR_INVALID_STATE` | `removable = false` なのに、開始前に `insertCard()` していない。Device に登録していない。すでに開始している。USB がすでに動いている |
| `ESP_ERR_INVALID_SIZE` | 決まった `messageBytes` が Bulk-OUT MPS 未満。endpoint や descriptor の容量が足りない |
| `ESP_ERR_NO_MEM` | メモリを確保できない |

### カードの状態

| 関数 | 説明 |
| --- | --- |
| `bool insertCard(atr, length)` | カードを挿し、ATR を設定する。`length` は 2〜33（`ESP_USB_DEVICE_CCID_MAX_ATR`、ISO 7816-3 の上限） |
| `esp_err_t removeCard()` | カードを抜く |
| `bool cardPresent()` | カードが挿さっていれば `true` |
| `bool cardPowered()` | Host が IccPowerOn でカードを起動していれば `true` |
| `size_t atr(buffer, capacity)` | 現在の ATR をコピーし、長さを返す |
| `bool mounted()` | Host が CCID の interface を設定済みなら `true` |
| `commandCount()` / `apduCount()` / `lastMessageType()` | 応答した command 数、APDU 交換の数、最後の message 種別（診断用） |

- `removable = true`（既定）：稼働中も `insertCard()`／`removeCard()` できます。変化は通知 endpoint（RDR_to_PC_NotifySlotChange）で Host へ知らせます。前の通知を Host がまだ読んでいなければ、最新の状態を保留し、前の通知が終わってから送ります。Host が CCID を設定したときにも、その時点の状態を一度通知します。
- `removable = false`：開始前に `insertCard()` が必要です。稼働中の `insertCard()` は `false`、`removeCard()` は `ESP_ERR_INVALID_STATE` です。停止後は変えられます。

### callback

| callback | 形 | 動作 |
| --- | --- | --- |
| `onApdu` | `size_t (const uint8_t *apdu, size_t length, uint8_t *response, size_t capacity)` | 応答（SW1 SW2 を含む）を `capacity` 以内で書き、長さを返す。0 を返すと command 失敗（bError ＝ ICC_MUTE） |
| `onEscape` | `onApdu` と同じ | PC_to_RDR_Escape の中身を受け、RDR_to_PC_Escape の中身を返す。未登録か 0 なら CMD_NOT_SUPPORTED |
| `onPower` | `void (bool on)` | Host がカードを起動すると `true`、停止すると `false` |

- `onApdu` を登録しないと、APDU には `6D 00`（命令を知らない）を返します。
- カードがない、または起動していないときの APDU は、callback を呼ばずに失敗（ICC_MUTE）を返します。
- 応答用の buffer は、DMA で送る返信用の領域そのものです。

### 呼べる文脈

| 操作 | 文脈 |
| --- | --- |
| `configure`、callback の登録 | 停止中のアプリの task |
| `insertCard`、`removeCard`、状態の照会 | アプリの task |
| `onApdu`、`onEscape`、`onPower` | USB worker task の上で呼ばれる |

- callback は待たずにすぐ戻ってください。callback の中から、この Function の USB 操作（停止や再設定）をしないでください。
- 応答を後で返す（buffer を保存して非同期に返信する）API はありません。重い処理が要るなら、あらかじめ結果を用意しておく設計にします。

### transport の回復

- **SET_INTERFACE（alternate 0）**：DMA を止め、bulk と通知 endpoint を DATA0 に戻します。組み立て途中の message、未読の返信、Abort 待ちは捨てます。カードの状態と callback は残します。
- **class 要求 ABORT（CCID 1.1 §5.3.1）**：slot と sequence を記録し、対応する PC_to_RDR_Abort が来るまで、他の command を拒否します。
- **bulk の HALT／CLEAR**：そのリーダーの組み立て途中の message、未読の返信、Abort 待ちを捨てます。片方の bulk が HALT 中は新しい command を受け付けません。CLEAR の対象だけ DATA0 に戻します。カードの状態・電源・callback、別のリーダーの通信は保ちます。
- **通知 endpoint の HALT／CLEAR**：bulk の交換は捨てず、回復後に最新の slot 状態を通知し直します。
- 中断した交換は続きから再開しません。Host が command を送り直してください。
- 不正な slot、容量を超える message、途中で切れた message には、要求に合った応答形式でエラーを返します。

### 複数のリーダー

複数の CCID を別々の interface として登録できます。`messageBytes`、カードの状態、callback、buffer は Function ごとに別です。
上限は構成全体の endpoint 数で決まります。S3 では着脱できる CCID を 2 つ置くと、control 以外の IN endpoint 4 本を使い切ります。

## バッファとメモリ

Function ごとに、begin で次を確保します。転送のたびの heap 確保や、CCID 専用の task はありません。

| 領域 | 置き場所 | 大きさ |
| --- | --- | --- |
| 受信 packet 用 DMA buffer | DMA 可能な内部 RAM | Bulk-OUT MPS |
| message の組立て領域 | 通常の heap | `messageBytes` |
| 返信用 DMA buffer（callback の出力先を兼ねる） | DMA 可能な内部 RAM | `messageBytes` |
| 通知用 DMA buffer | DMA 可能な内部 RAM | 8 byte（`removable = true` のとき） |
| 管理情報 | 通常の heap | 小 |

- 外部 buffer を渡す設定はありません。DMA buffer は必ず内部 RAM に置きます。
- message の組立て領域は通常の `malloc` です。PSRAM を malloc に使う設定では、PSRAM に置かれることがあります（その構成は実機では未確認）。
- 返信の IN と末尾の ZLP が終わるまで、次の OUT を受け付けません（1 slot で同時に 1 command）。
- ATR の 33 byte 配列は protocol の上限として各 Function の中に持ちます。
- 使わない CCID の driver や buffer はリンクされません。

## Host OS 側の扱い

| OS | 標準の仕組み | この library での確認 |
| --- | --- | --- |
| Windows | 標準の CCID driver（Smart Card サービス） | 未確認 |
| macOS | 標準の CCID 対応（CryptoTokenKit） | 未確認 |
| Linux | pcsc-lite と CCID driver（libccid） | 未確認 |

- 動作は、ESP32 を Host にした試験（S3 同士の FS、P4 一台の loopback、P4 と P4X の HS 直結）で確認しています。PC/SC の各 OS、実カード・実リーダーとの相互接続はまだ確認していません。
- Linux では `pcsc_scan` でリーダーとカードの状態、`scriptor` や `opensc-tool` で APDU の交換を試せます。
- Escape command はリーダーごとの独自命令です。汎用のカード命令ではありません。

## 規格との対応と設計の理由

### CCID 1.1 との対応

基準は [USB-IF CCID 1.1](https://www.usb.org/sites/default/files/DWG_Smart-Card_CCID_Rev110.pdf) です。

| 項目 | 実装 |
| --- | --- |
| interface | class 0x0b、subclass 0、protocol 0（bulk 転送） |
| class descriptor（§5.1） | bcdCCID 1.10、bMaxSlotIndex 0、dwProtocols T=1、bVoltageSupport 5V/3V/1.8V、dwFeatures 0x000204FE、dwMaxCCIDMessageLength ＝ `messageBytes`、bPINSupport 0、bMaxCCIDBusySlots 1 |
| dwFeatures の意味 | ATR に基づく自動設定、自動の起動・電圧・clock・baud rate・parameter 交渉・PPS、T=1 の IFSD 交換、short APDU 単位の交換 |
| clock と速度 | 既定値 3.58 MHz、9600 bps の一つだけを宣言（交渉なし） |
| endpoint（§5.2） | bulk OUT、bulk IN、interrupt IN（着脱できるときだけ） |
| Bulk-OUT message（§6.1） | IccPowerOn、IccPowerOff、GetSlotStatus、XfrBlock、Escape、GetParameters、ResetParameters、SetParameters、Abort。IccClock と T0APDU などは CMD_NOT_SUPPORTED |
| Bulk-IN message（§6.2） | DataBlock、SlotStatus、Parameters、Escape |
| Interrupt-IN（§6.3） | NotifySlotChange |
| class 要求（§5.3） | ABORT |

着脱できるリーダーと固定カードの違い（通知 endpoint の有無）は、CCID 1.1 §3.1.2 と §5.2 に従っています。

### 設計の理由

- **`messageBytes` の既定を自動にした。** 固定の既定値では、HS で MPS 512 の下限を満たせず begin が必ず失敗します。begin で `max(271, Bulk-OUT MPS)` に決めれば、FS でも HS でも既定のまま動きます。明示値が MPS 未満なら黙って直さず拒否します。
- **Function ごとの容量。** 容量とカード固定の設定を Function ごとに持ち、複数のリーダーを独立に置けます。
- **HALT 回復で交換を捨てる。** 標準の pipe 回復の後に、リーダーが受信待ちのまま止まらないためです。途中の応答を送り直すと既読部分が重なりうるので、続きからの再開はしません。
- **停止の順序。** end では Device の worker と DMA を止めてから状態を解放します。

### v2 からの主な変更

- `ESP_USB_DEVICE_CCID_BUFFER_SIZE` は廃止しました（定義すると compile error）。`EspUsbCcidConfig::messageBytes` を使います。既定は自動で、P4 の HS でもそのまま開始できます。
- `configure`／`onApdu`／`onEscape`／`onPower`／`removeCard` は `esp_err_t` を返します。

## 関連する examples

| example | 内容 |
| --- | --- |
| [SmartCardReader](../../examples/SmartCardReader/) | 1 slot のリーダー。`FF CA 00 00 00`（UID 取得）と echo 命令に答え、serial の `i`／`r` でカードを抜き差しする |

関連文書：[USB device 開発ガイド](../usb-device-guide.ja.md)、[troubleshooting](../troubleshooting.ja.md)、[v2 からの移行](../migrating-from-v2.ja.md)
