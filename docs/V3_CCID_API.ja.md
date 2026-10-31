# EspUsbDeviceCcid（CCID）API（v3）

`EspUsbDeviceCcid`は、USB Hostから「ICカードリーダー」に見えるFunctionです（USB規格のCCID、Chip/Smart Card Interface Devices）。
リーダーに挿さっているカードはスケッチが演じます。カードの有無とATR（カードが電源投入時に返す識別情報）を`insertCard()`で決め、
Hostから届くAPDU（カードへの命令と応答）を`onApdu()`で処理します。
扱うのは1 slot、T=1プロトコル、short APDU（命令データ最大255 byte・応答最大256 byte）の範囲です。

## 基本の使い方

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceCcid reader(device);

void setup() {
  EspUsbCcidConfig config;               // messageBytes は既定の 0（自動）のままでよい
  config.name = "Card reader";
  if (reader.configure(config) != ESP_OK) return;

  const uint8_t atr[] = {0x3b, 0x80, 0x01, 0x81}; // T=1 のカード
  reader.insertCard(atr, sizeof(atr));

  reader.onApdu([](const uint8_t *apdu, size_t length, uint8_t *reply, size_t capacity) -> size_t {
    // USBの処理task上で呼ばれる。応答（SW1 SW2 を含む）を reply に書き、その長さを返す
    if (capacity < 2) return 0;
    reply[0] = 0x6d; reply[1] = 0x00;   // 例：「命令を知らない」
    return 2;
  });

  if (!device.begin()) {
    // device.lastError() で設定不正（ESP_ERR_INVALID_SIZE など）と確保失敗（ESP_ERR_NO_MEM）を区別できる
  }
}

void loop() {}
```

### 主な設定

`EspUsbCcidConfig`の項目です。`configure()`は停止中にだけ呼べます。

| 項目 | 既定値 | 意味 | FS/HSでの違い |
| --- | --- | --- | --- |
| `messageBytes` | 0（自動） | 1つのCCID message（10 byteのheader込み）の最大byte数。0なら`begin()`のときに自動で決まる。明示するなら64〜65535 | 自動の値はFS 271、HS 512。明示値がbulk MPS（FS 64、HS 512）未満だと`begin()`が`ESP_ERR_INVALID_SIZE` |
| `removable` | `true` | `true`：カードを抜き差しでき、変化を通知endpointでHostへ知らせる。`false`：カード固定で、通知endpointを持たない | なし |
| `name` | `nullptr` | interfaceの名前（文字列descriptor） | なし |

`onApdu`/`onEscape`のcallbackに渡る応答用の容量（`capacity`）は`messageBytes`−10です。既定ではFS 261、HS 502 byteです。

## messageBytesの決まり方

- 既定の0は「自動」です。`begin()`のときに`max(271, Bulk-OUT MPS)`に決まります。FSでは271、HSでは512です。
- 271は、short APDUがちょうど収まる大きさです（命令：10＋4＋1＋255＋1、応答：10＋256＋2）。
- 明示する場合は64〜65535 byteです。範囲外は`configure()`が`ESP_ERR_INVALID_SIZE`を返します。
- 明示値がBulk-OUT MPSより小さいと、`begin()`が`ESP_ERR_INVALID_SIZE`で失敗します。黙って切り上げることはしません。
- 決まった値は`messageCapacity()`とdescriptorの`dwMaxCCIDMessageLength`に出ます。`configuration().messageBytes`は設定したまま（自動なら0）を返します。
- 大きな値を設定しても、extended APDU、chaining、PIN入力の機能が増えるわけではありません。

最小値がMPSに縛られる理由は[設計判断の理由](#設計判断の理由)を見てください。

## API

### 設定と開始

| 関数 | 戻り値と制約 |
| --- | --- |
| `configure(config)` | `esp_err_t`。稼働中は`ESP_ERR_INVALID_STATE`。`messageBytes`が0でも64〜65535でもなければ`ESP_ERR_INVALID_SIZE` |
| `configuration()` | 設定した値をそのまま返す |
| `messageCapacity()` | 稼働中に確保しているmessage容量。開始前・終了後は0 |
| `onApdu(callback)` / `onEscape(callback)` / `onPower(callback)` | `esp_err_t`。停止中だけ。稼働中は`ESP_ERR_INVALID_STATE` |
| `startError()` | 直近の`begin()`の失敗理由 |

`begin()`（`device.begin()`から呼ばれる）での失敗は次のとおりです。

- `removable = false`なのに、開始前に`insertCard()`していない：`ESP_ERR_INVALID_STATE`
- 決まった`messageBytes`がBulk-OUT MPS未満：`ESP_ERR_INVALID_SIZE`
- メモリを確保できない：`ESP_ERR_NO_MEM`

設定を変えるときは`device.end()`→`configure()`→`device.begin()`の順に呼びます。
`ESP_USB_DEVICE_CCID_BUFFER_SIZE`を定義するとコンパイルエラーになります。`messageBytes`を使ってください。

### カードの状態

| 関数 | 説明 |
| --- | --- |
| `insertCard(atr, length)` | `bool`。カードを挿し、ATRを設定する。`length`は2〜33（`ESP_USB_DEVICE_CCID_MAX_ATR`）。スケッチのtaskから呼ぶ |
| `removeCard()` | `esp_err_t`。カードを抜く |
| `cardPresent()` | カードが挿さっていれば`true` |
| `cardPowered()` | HostがIccPowerOnでカードを起動していれば`true` |
| `atr(buffer, capacity)` | 現在のATRをコピーし、その長さを返す |
| `mounted()` | HostがCCIDのinterfaceを設定済みなら`true` |
| `commandCount()` / `apduCount()` / `lastMessageType()` | 応答したcommand数、APDU交換の数、最後のmessage種別。試験や例のための診断用 |

- `removable = true`（既定）：稼働中も`insertCard()`/`removeCard()`でき、変化を通知endpointでHostへ知らせます。前の通知をHostがまだ読んでいなければ、最新の状態を保留し、前の通知が終わってから送ります。HostがCCIDを設定したときにも、その時点の状態を一度通知します。
- `removable = false`：開始前に`insertCard()`が必要です。稼働中の`insertCard()`は`false`、`removeCard()`は`ESP_ERR_INVALID_STATE`です。停止後は変更できます。
- ATRの差し替えとIccPowerOnの応答はlockで保護しています。
- 着脱できるリーダーと固定カードの違いは、[USB-IF CCID 1.1](https://www.usb.org/sites/default/files/DWG_Smart-Card_CCID_Rev110.pdf)の§3.1.2と§5.2に従っています。

### callback

- `onApdu(callback)`：`size_t callback(const uint8_t *apdu, size_t length, uint8_t *response, size_t capacity)`。応答（SW1 SW2を含む）を`response`へ`capacity`以内で書き、長さを返します。0を返すと、Hostにはcommand失敗（bError＝ICC_MUTE）として伝わります。Device側でstatus wordを足したり解釈したりはしません。
- callbackを登録しない場合、APDUには`6D 00`（命令を知らない）を返します。
- カードがない、または起動していないときのAPDUは、callbackを呼ばずに失敗（ICC_MUTE）を返します。
- `onEscape(callback)`：PC_to_RDR_Escapeの中身を受け、RDR_to_PC_Escapeの中身を返します。形は`onApdu`と同じです。未登録か0を返した場合は「未対応」（CMD_NOT_SUPPORTED）を返します。
- `onPower(callback)`：Hostがカードを起動すると`true`、停止すると`false`で呼ばれます。
- callbackはUSBの処理task（worker）上で動きます。待たずにすぐ戻り、callbackの中からこのFunctionのUSB操作（停止や再設定）をしないでください。bufferを保存して後で返信する形のAPIはありません。
- 応答用のbufferはDMAで送る返信用の領域をそのまま使います。

### 対応しない機能

1 slot、T=1、short APDUのemulated slotという範囲に限ります。
chaining、PIN pad（secure PIN entry）、clockやデータ速度の交渉は実装せず、descriptorでも宣言しません。従って仕様どおりのHostはこれらを要求しません。

## FSとHSの違い

ここでの「HS対応controller」は、ESP32-P4（P4X含む）で`EspUsbDeviceConfig::controller`が`FullSpeed`以外のときです。
このとき`messageBytes`の自動値と下限には512を使い、Hostが実際にはFSでつないだ場合も同じ値を使います。

| 項目 | FS | HS |
| --- | --- | --- |
| bulk MPS | 64 byte | 512 byte |
| `messageBytes`の自動値（既定） | 271 | 512 |
| 明示できる`messageBytes` | 64〜65535 | 512〜65535（512未満は`begin()`が`ESP_ERR_INVALID_SIZE`） |
| callbackの`capacity`（既定時） | 261 | 502 |
| 通知endpoint | 8 byte、bInterval 16（16 ms） | 8 byte、bInterval 8（2^(8−1)×125 µs＝16 ms） |
| 使うendpoint | bulk IN、bulk OUT、通知IN（`removable = false`なら通知なし） | 同じ |
| 非control IN endpointの上限（構成全体） | ESP32-S3：4本 | ESP32-P4 HS：7本 |

## 複数のリーダー

複数のCCIDを別々のinterfaceとして登録できます。上限は構成全体のendpoint数で決まります。
S3では着脱可能なCCIDを2つ置くと非control INを4本使い切るため、さらにVendorを足せるとは限りません。
`messageBytes`、カードの状態、callback、送受信bufferはFunctionごとに別です。

## メモリ

選んだFunctionごとに、`begin()`で次を確保します。転送ごとのheap確保やCCID専用のtaskはありません。

- 管理情報
- 受信packet用のDMA buffer（MPS分。DMA可能な内部RAM）
- message組み立て用の領域（`messageBytes`分）
- 返信用のDMA buffer（`messageBytes`分。DMA可能な内部RAM）。callbackの出力先も兼ねる
- 着脱可能なら通知用のDMA buffer（8 byte）

返信のINと末尾のZLPが終わるまで、次のOUTを受け付けません（1 slotで同時に1 command）。
使わないCCIDのdriverやbufferはリンクされません。ATRの33 byte配列はprotocolの上限として各Functionの中に残ります。

## transportの回復

- SET_INTERFACEでalternate 0が選ばれると、DMAを止め、古い完了通知を処理し終えてから、両方のbulkと通知endpointをDATA0へ戻します。組み立て途中のmessage、未読の返信、古いAbort待ちは捨て、カードの状態とcallbackは残します。
- class要求のABORTはslotとsequenceを記録し、対応するPC_to_RDR_Abortが来るまで、他のcommandやsequenceの合わないcommandを拒否します。
- 不正なslot、容量を超えるmessage、途中で切れたmessageには、要求に合った応答形式でエラーを返します。
- 32 bitの長さfieldにheader長を足す計算は64 bitで行い、桁あふれを避けます。

## endpoint HALTからの復帰

bulkのSET_FEATURE/CLEAR_FEATURE（HALT）では、そのリーダーの組み立て途中のmessage、未読の返信、Abort待ちを、1つの交換としてまとめて捨てます。
DMAを止め、古い完了通知を処理し終えてから受信を再開します。

- 片方のbulkがHALT中は、新しいcommandを受け付けません。
- CLEARの対象だけDATA0へ戻し、もう片方のdata toggleとHALTは保ちます。
- これは本ライブラリの回復方針で、CCIDのclass ABORTとは別のものです。
- カードの有無・電源状態・callback、別のリーダーの通信は保ちます。
- 通知endpointのHALT/CLEARではbulkの交換を捨てず、回復後に最新のslot状態を通知し直します。bulkの回復で通知endpointのHALTを勝手に解除することもありません。

公開APIは変えていません。標準のpipe回復の後に、リーダーが受信待ちのまま止まらないのが利点です。
不利益は、進行中のbulk交換を続けられないため、Hostがcommandを送り直す必要があることです。
途中の応答をそのまま送り直すと既読部分が重複しうるので、続きからの再開は保証しません。

## 設計判断の理由

- **既定の`messageBytes`を自動（0）にした理由**（2026-10-31、57ae028）：以前の既定は320でしたが、HSでは`begin()`が必ず拒否していました。ユーザーの決定とArduinoCore-CH32RVとの相談により、既定0を自動とし、`begin()`のときに`max(271, Bulk-OUT MPS)`へ決める方式にしました。ArduinoCore-CH32RVの7248ca0dと同じ決め方です。明示値がMPS未満なら従来どおり拒否します。
- **最小値がMPSに縛られる理由**：[CCID 1.1 §5.1のdwMaxCCIDMessageLength](https://www.usb.org/sites/default/files/DWG_Smart-Card_CCID_Rev110.pdf#page=20)は、short APDUを扱うリーダーにBulk-OUTのwMaxPacketSize以上を求めています。packetの組み立て領域を分けるだけでは、この条件は外せません。
- **Functionごとの容量**：独自coreへ独自driverを登録し、容量とカード固定の設定をFunctionごとに持たせました。Device/Function/config/beginを明示する使い方は変えていません。
- **戻り値**：`configure`/`onApdu`/`onEscape`/`onPower`/`removeCard`は`esp_err_t`を返します。戻り値を無視していた呼び方はそのまま動きます。
- **停止の順序**：`end()`ではDeviceのworkerとDMAを止めてから状態を解放します。稼働中にFunctionを破棄した場合もDeviceの停止を経由します。
- **HALT回復の試験方法**：試験HostのESP-IDF `usb_host_endpoint_clear()`は古いdata toggleを引き継ぎます（[実装](https://github.com/espressif/esp-idf/blob/v5.5.4/components/usb/hcd_dwc.c)を確認）。そこでDevice側のCLEARを試す対象はDATA0にそろえ、途中のOUTや未読のINでは反対側をDATA1に置いて、反対側まで誤って戻す実装が通らないようにしています。SET_INTERFACEとHostのinterface再取得は各条件の準備にだけ使い、HALT後の回復には使いません。

## 未確認の範囲

- HALT/CLEAR_FEATUREとbus reset/endの全ての競合順序。
- PC/SCや各OS、実カード・実リーダーとの相互接続。
- ESP32-S2/S31での実通信。

## 実測の記録

### S3–S3

関連4件が527.51秒で成功し、CCIDを加えた774か所の確保失敗と回復、heapの一致を確認しました。
最後のAbort状態の補強の後、上限までのEscape、T=1のparameterとPowerOffを含むCCIDの境界試験1件が80.51秒で成功しました。
実機でwire転送したmessage容量は最大1088 byteです。65535 byteは設定値・確保・終了の確認で、その長さのwire転送の合格ではありません。
詳細とserialの原本の場所は[独立試験の記録](../validation/README.ja.md#ccidのruntime容量と独立instance)にあります。

### endpoint回復（2026-10-28）

Hostの対象data toggleをDATA0にそろえても、修正前はHALT解除の後の次の応答が戻らず失敗しました（`ccid-halt-data0-baseline.xml`、33.71秒）。
修正後の単独の境界試験は61.19秒で成功しました。
最後にCLEARの繰り返しと別リーダーの通信継続を加え、CCID/control_core/allocation_failureの
**3 passed、0 skipped/failed/error/unfinished、146.41秒**を確認しました。

- 4つのmessage容量×2つのリーダー（0→1→0）で、待機中のbulk、途中のOUT、未読のIN、通知、両方向同時のHALT、両方の解除順序、HALTしていないendpointへのCLEARの繰り返しを確認。
- 計260回のHALT/CLEAR要求、34回の状態照会、通知の復帰をまたぐ未読返信6回が成功。
- 別リーダーの応答が成功しても、対象リーダーのHALTが残ることも確認。
- 774か所の確保失敗と回復、heap 346564→346564 byte、6本のserialすべてSTOPPED、panicなし。
- S2/P4はcompileのみ成功。製品側の変更はCCIDに限り、直前の全31件は再実行していません。

原本は`/tmp/pytest-embedded/2026-10-27_19-56-13-268381/`。
JUnit/transcript/serialのコピーと集計は`validation/.pytest-results/ccid-halt-final*`、`ccid-halt-audit.txt`。
最初の試験用buffer不足・Hostのdata toggle不一致と、toggleをそろえた後の製品の不具合を区別してtranscriptを保存しています。
旧tests/examplesはmainとの差分なし。この実測は、全てのEP0の順序、bus reset/endとの全競合、OS/PC/SC、他SoCの実通信を証明するものではありません。

### P4とHS直結

- 2026-10-30：P4の両controllerのFS接続で、基本通信と再開始を確認しました（P4のCCID 4 case）。詳細は[P4/P4Xと追加試験環境](V3_P4_VARIANTS_AND_BENCH.ja.md)。
- 2026-10-31：S3向けのpeer試験（`ccid_stream`を含む）をP4/P4XのHS直結の両方向でも実行し、成功しました。HS向けには`validation/peer/ccid`も追加しています（91828c9）。
- 2026-10-31：既定の自動値を、`ccid_stream`のmetadataで確認しました。容量とdescriptorの`dwMaxCCIDMessageLength`は、S3で271、HSで512でした。
