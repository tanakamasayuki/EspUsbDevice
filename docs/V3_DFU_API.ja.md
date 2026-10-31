# EspUsbDeviceDfu（DFU）API（v3）

`EspUsbDeviceDfu`は、USB規格のDFU（Device Firmware Upgrade）でfirmwareを書き込めるようにするFunctionです。
2つのmodeがあります。Downloadは`dfu-util -D`などで送られたimageを受け取り、既定では予備のOTA partitionへ書いて再起動します。
Runtimeは「DFUへ切り替えて」という要求（DETACH）だけを受け、既定ではchipのROM download loaderへ再起動します。
DFUの転送はすべてcontrol endpoint（EP0）を通るので、endpointを1本も使いません。

## 基本の使い方

Download mode（既定）で、内蔵のOTA更新を使う例です。

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceDfu dfu(device);   // 既定は EspUsbDeviceDfuMode::Download と内蔵OTA

void setup() {
  EspUsbDfuConfig config;
  config.transferBytes = 4096; // 1 blockの最大byte数（既定 1024）
  if (dfu.configure(config) != ESP_OK) return;
  dfu.onProgress([](size_t written) { /* これまでに受け付けたbyte数 */ });
  dfu.onError([](EspUsbDfuStatus status) { /* 失敗の理由を保存する */ });
  device.begin();               // 書込みが終わると既定で自動的に再起動する
}

void loop() {}
```

Runtime modeは`EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Runtime);`で作ります（[DETACHの受付と移行](#detachの受付と移行2026-10-30-実装)）。

### 主な設定

`EspUsbDfuConfig`の項目です。`configure()`は停止中にだけ呼べます。

| 項目 | 既定値 | 意味 | FS/HSでの違い |
| --- | --- | --- | --- |
| `transferBytes` | 1024 | 1 block（1回のDFU_DNLOAD）の最大byte数。64〜65535。descriptorの`wTransferSize`になる | なし |
| `transferBuffer` | `nullptr` | Download用のblock bufferをアプリが用意する場合のpointer。`transferBytes`以上の長さが必要 | なし |
| `detachTimeoutMs` | 1000 | Runtimeで、DETACHを受けた後にappDETACH状態を保つ時間の上限（ms）。descriptorの`wDetachTimeOut` | なし |
| `downloadPollMs` | 1 | block書込み中にGETSTATUSで返す待ち時間（ms）。0〜0xFFFFFF | なし |
| `manifestPollMs` | 250 | manifest（書込み完了後の検証・反映）中にGETSTATUSで返す待ち時間（ms）。0〜0xFFFFFF | なし |
| `manifestationTolerant` | `false` | `true`ならmanifestの後もUSB resetを待たず、続けて別の転送を始められる | なし |

DFUはEP0だけを使い、EP0のMPSはFS/HSとも64 byteです。DFUの設定値や制約にFS/HSの違いはありません。

## API

### 設定と開始

| 関数 | 戻り値と制約 |
| --- | --- |
| `EspUsbDeviceDfu(device, mode, name)` | `mode`の既定はDownload、`name`はinterfaceの名前 |
| `configure(config)` | `esp_err_t`。稼働中は`ESP_ERR_INVALID_STATE`。`transferBytes`やpoll時間が範囲外なら`ESP_ERR_INVALID_SIZE`。`transferBuffer`のアドレス計算があふれるなら`ESP_ERR_INVALID_ARG` |
| `configuration()` | 設定した値をそのまま返す |
| `mode()` | Runtime/Download |
| `transferSize()` | descriptorで宣言する`transferBytes` |
| `transferCapacity()` | 確保したDownload用bufferのbyte数。開始前・終了後・Runtimeでは0 |
| `written()` | 今回の転送で受け付けたbyte数の累計 |
| `startError()` | 直近の`begin()`の失敗理由 |

- Downloadのblock bufferは`begin()`で一度だけ確保し、blockごとのheap操作はしません。Runtimeは同じ`wTransferSize`を宣言しますが、block bufferは確保しません。
- `transferBuffer`はCPUがコピーするための領域で、DMAの境界合わせは要りません。開始から終了まで有効に保ち、直接書き換えないでください。
- `begin()`で確保できないと、`startError()`と`device.lastError()`が`ESP_ERR_NO_MEM`になり、確保済みの領域は回収されます。
- 設定を変えるときは`device.end()`→`configure()`→`device.begin()`の順に呼びます。ビルドオプションや再ビルドは要りません。

### 進捗と完了（Download）

| 関数 | 説明 |
| --- | --- |
| `onProgress(callback)` | blockの保存に成功するたびに、累計byte数で呼ばれる |
| `onError(callback)` | 失敗のたびに`EspUsbDfuStatus`で呼ばれる |
| `onComplete(callback)` | 内蔵OTAだけ。manifestのときに呼ばれ、`false`を返すとimageを拒否し、起動先を実行中のpartitionへ戻す |
| `restartWhenComplete(enable)` | 内蔵OTAだけ。既定`true`で、manifest成功の約500 ms後に再起動する。`false`ならアプリが再起動する |
| `firmware()` | 内蔵OTAで使う`EspUsbDeviceFirmwareUpdate` |

これらのsetterは`esp_err_t`を返し、稼働中は`ESP_ERR_INVALID_STATE`です。

- `manifestationTolerant = false`（既定）：manifest成功の後はUSB resetを待ちます。
- `manifestationTolerant = true`：manifest完了後のGETSTATUSでdfuIDLEへ戻り、別の転送を始められます。

### 保存先を自分で決める（backend）

`EspUsbDfuBackend`の`download`と`manifest`を両方指定すると、内蔵OTAの代わりにアプリの保存先を使います。
外部flash、別processorへの転送、アプリが検証・反映を管理するimageなどを想定しています。

```cpp
EspUsbDfuBackend backend;
backend.download = [](uint16_t block, const uint8_t *data, size_t bytes) {
  // 戻る前にコピーまたは処理する。data を保存してはいけない
  return storeBlock(block, data, bytes) ? EspUsbDfuStatus::Ok : EspUsbDfuStatus::Write;
};
backend.manifest = []() {
  return verifyAndActivate() ? EspUsbDfuStatus::Ok : EspUsbDfuStatus::Verify;
};
backend.abort = []() { discardPartialUpdate(); };   // 任意
dfu.setBackend(backend);
```

- `setBackend()`は`esp_err_t`。稼働中は`ESP_ERR_INVALID_STATE`。`download`と`manifest`の片方だけ、`download`なしで`abort`だけ、Runtimeでの`download`指定は`ESP_ERR_INVALID_ARG`です。
- 空のbackendを渡すと内蔵OTAへ戻ります。
- callbackはUSBの処理task（worker）上で同期的に呼ばれます。`download`の`data`はcallbackの中だけ有効です。
- 一部しか保存できなかった場合は`Ok`を返さず、失敗のstatusを返してください。範囲外のstatus値は`Unknown`に直します。
- `abort`は、保存を始めた転送が失敗・ABORT・reset・`end()`で終わったときに1回呼ばれます。保存前に取り消したblockや、成功したmanifestの後には呼ばれません。
- manifestの後の検証・反映・再起動はbackendの責任です。`onComplete`と`restartWhenComplete`は内蔵OTA専用で、backendには効きません。

### 複数のDFU interface

同時に複数のDFU interfaceを公開できます。状態・buffer・backendは共有しません。
内蔵OTA同士は同じ保存先へ同時に書かないよう、転送中に内蔵OTAを使えるのは1つだけです。

### 状態遷移と制御要求

[USB-IF DFU 1.1](https://www.usb.org/sites/default/files/DFU_1.1.pdf)の6章と付録Aに沿って状態遷移を実装しています。

- 受け取ったblockはdata段とstatus段が終わるまで保存せず、続くGETSTATUSのACKの後でbackend（または内蔵OTA）へ渡します。
- block番号は0から始まり、16 bitで1周します。番号の飛び、`transferBytes`を超える長さ、不正な要求はdfuERRORになります。HostがCLRSTATUSを送ると再開できます。
- Runtimeでは、不正なDFU要求でdownload状態へ移ることはありません。

## DETACHの受付と移行（2026-10-30 実装）

Runtime modeでHostがDFU_DETACHを送ったときの動きです。受け付けるかどうかと、受け付けた後の動作を分けて決められます。

```cpp
using DetachCallback = std::function<bool()>;  // SETUPのとき。false を返すと STALL
esp_err_t onDetach(DetachCallback callback);
using DetachHandler = std::function<void()>;   // status段のACKの後に1回。既定のROM移行の代わりに動く
esp_err_t setDetachHandler(DetachHandler handler);
```

| 段階 | 動作 |
| --- | --- |
| SETUP | 種別0x21・DETACH・wLength 0で、状態がappIDLEのときだけ有効。`onDetach`を1回呼び、`false`ならSTALLし、状態も予約も変えない |
| 受付 | status段を返す。この時点ではまだ移らない |
| status段のACK | appDETACHへ移り、handlerがあれば1回呼ぶ。なければ100 ms後のROM loaderへの再起動を予約する |
| 受付の後 | `end()`、USB reset、SET_CONFIGURATION 0のどれでも取り消さない |
| 期限 | appDETACHを`min(wValue, detachTimeoutMs)`の間保ち、過ぎるとappIDLEへ戻る。既定のROM移行はwValueに関係なく行う |

- 登録できるのはRuntimeの停止中だけです。稼働中は`ESP_ERR_INVALID_STATE`、Downloadでは`ESP_ERR_INVALID_ARG`です。
- 空のcallbackは無条件に受け付け、空のhandlerは既定のROM移行へ戻します。
- どちらもUSB worker上で呼ばれます。待機、`end()`、保存領域の書換えはせず、重い処理はアプリのtaskへ渡してください。
- 断りたい場合は`onDetach`で`false`を返します。受け付けた後で取り消す方法はありません。
- 実測の根拠は[判断の根拠となった実測](#判断の根拠となった実測)にあります。

## FSとHSの違い

DFUはEP0だけを使います。EP0のMPSはFS/HSとも64 byteで、`transferBytes`の範囲（64〜65535）、poll時間、DETACHの扱いは同じです。
HSでは、P4XとLinux PCのHS直結（DETACH）と、P4/P4XのHS直結（転送）で確認しています（[実測の記録](#実測の記録)）。

## 設計判断の理由

- 2026-10-27に、DFU Runtime/DownloadのTinyUSB classと固定のglobal bufferを、本ライブラリ独自のdriverへ置き換えました。
- 利点：使わないblock bufferを省ける、再ビルドなしでサイズを変えられる、複数のinterfaceを置ける、保存先とUSBの状態管理を分けられる。
- 不利益：開始時の動的確保、callbackの約束事の追加、状態管理の費用。
- 移行：旧`CFG_TUD_DFU_XFER_BUFSIZE`は`configure()`へ、classの`transferSize`はinstanceの呼出しへ移しました。`onError`の引数は`uint8_t`から`EspUsbDfuStatus`へ変えました。他のcallbackの呼び方は保ち、setterは`esp_err_t`を返します。
- DETACHの仕様は、当初の案（ACKの後のvoid callback）を、SETUPでの受付判定と受付後の移行に分けて確定しました。P4XとLinux PCの実測と、ArduinoCore-CH32RVとの相談に基づきます。共通の契約は「SETUPで`onDetach`が判定し、受け付けたら必ず移行する」です。`setDetachHandler`とappDETACHの状態はESP独自です。CH32RV側は期限を持たず必ず移行し、`end()`で取り消されていたのは意図しない動作なので同じ「取り消さない」契約に直す、という回答でした。

## 未実装と未確認の範囲

- Upload、複数のalternate settingによる保存領域の選択、非同期のflash callbackは実装していません。
- Runtimeだけを使う場合も、共通driverのDownload処理と内蔵OTA処理がリンクされることがあり、flash費用まで完全には分離できていません。
- ESP32-S2/S31、dfu-utilと各OS、実OTAの反映と再起動、block番号の1周、EP0の途中切断は未確認です。

## 実測の記録

### S3–S3（2026-10-26）

`dfu-runtime-related.xml`は**2 passed、0 skipped、0 unfinished、74.27秒**。
64/129/4097/65535 byteのblock、内部/外部buffer、独立した2つのDownloadとRuntime、GETSTATUSとblock順序、
不正要求のSTALLとCLRSTATUS、書込み/manifestの失敗、ABORTと`end()`での1回だけの回収、
manifestation tolerantとreset待ち、DETACHの期限切れからの復帰を確認しました。設定の繰り返しでheapも戻りました。
502か所の確保失敗と回復は338520→338520 byte、両caseで両boardがSTOPPED、panicなし。
serialの原本は`/tmp/pytest-embedded/2026-10-26_19-25-20-261549/`、コピーとJUnit/transcriptは`validation/.pytest-results/dfu-runtime-related*`。
DFUだけ・Deviceだけの最小build/nmで、旧TinyUSB DFUと、使っていない独自DFUがリンクされないことを確認しました。
試験は公開IDF Host APIとRAMのbackendで行い、conftestや独自のpytest hookは使っていません。
Coreのenum filterが有効なので、Hostは対象VID/PIDのfilterを明示します。

### DFU置換後の全体確認（2026-10-27）

標準の`--combo all`は**17 passed、0 skipped、0 unfinished、841.47秒**。
全17 caseで両boardがSTOPPED、34本のserialにpanicなし。502か所の確保失敗と回復、
DFUの最大block・複数interface・manifestationと、従来のAudio/CDC/Vendor/HID/MIDI/MSC、descriptor/registry、取消しと転送境界を含みます。
serialの原本は`/tmp/pytest-embedded/2026-10-26_19-27-27-183733/`、コピーとJUnit/transcriptは`validation/.pytest-results/runtime-dfu-all*`。
DFUだけ・Deviceだけの最小build/nmも成功。TinyUSB treeは51 file / 15 compiled sourcesで一致、旧tests/examplesはmainと同一で、mainはv3の祖先です。

### 判断の根拠となった実測

DETACHの仕様を決めた試験です。`validation/p4x_pc/dfu_detach`（P4X v3.2のHSをbenchの3-4でLinux PCへ直結し、PCからpyusbで操作）。**1 passed、58.34秒**。
記録は`validation/.pytest-results/dfu-detach*`、数値は`dfu-detach-measure.json`。

| 項目 | 結果 | 判断 |
| --- | --- | --- |
| ACKの後の再起動待ちを0/1/5/20/100 ms、各10回 | 50/50でstatus段が完了。制御転送は最大0.52 ms、Hostが機器の消失を検出したのはACKの136〜253 ms後で、待ち時間による差はなし | 既定の100 msを維持。wValueと連動させない |
| appDETACHの期限 | wValue 300では301 ms後、wValue 3000（detachTimeoutMs 1000）では1001 ms後にappIDLE。期限内の再DETACHはSTALL | `min(wValue, detachTimeoutMs)`を採用 |
| ACKの0/20/50/90 ms後に`end()` | 12/12で`end()`は1 ms以内に`ESP_OK`、再起動も実行、panicなし | 取り消さない。CH32RVも同じ方針に直す |
| ACK直後にHostがport reset | 5/5で再起動を実行 | 取り消さない |
| `onDetach`が2回`false` | 2回ともSTALL、GETSTATEはappIDLEのまま、handlerは呼ばれない。3回目で受付、appDETACH | SETUPでの拒否を採用 |

PC側では、新しく列挙された機器を別のprocessが調べている間、interfaceのclaimがEBUSYになることがありました（1回の実行で3〜10回）。
要求はbusへ出ていないので送り直しており、Deviceの動作とは区別しています。

### P4とHS直結

- 2026-10-30：P4の両controllerのFS接続で、RAM backendによる基本通信と再開始を確認しました（P4のDFU 2 case）。詳細は[P4/P4Xと追加試験環境](V3_P4_VARIANTS_AND_BENCH.ja.md)。
- 2026-10-31：S3向けのpeer試験（`dfu_transport`を含む）をP4/P4XのHS直結の両方向でも実行し、成功しました。HS向けには`validation/peer/dfu`も追加しています（91828c9）。
