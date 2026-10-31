# DFU（Device Firmware Upgrade）

> English: [dfu.md](dfu.md)

`EspUsbDeviceDfu` は、USB 規格の DFU で firmware を書き換えられるようにする Function です。
DFU の転送はすべて control endpoint（EP0）を通ります。**interface を 1 つ使い、endpoint は 1 本も使いません。**
そのため、endpoint を使い切った構成にも足せます。

## 概要と使う場面

二つの mode があります。

| mode | Host 側の操作 | 既定の動作 | 使う場面 |
| --- | --- | --- | --- |
| `Download`（既定） | `dfu-util -D firmware.bin` | image を予備の OTA partition へ書き、検証して起動先を切り替え、再起動する | 実行中の sketch のまま、USB だけで application を更新する |
| `Runtime` | `dfu-util -e` | DETACH を受けたら chip の ROM download loader へ再起動する | `esptool` で flash 全体を書き直すため、ボタンを押さずに boot mode へ入る |

- Download の保存先は、既定では内蔵の OTA 更新（`EspUsbDeviceFirmwareUpdate`）です。`setBackend()` で外部 flash や別の processor など、アプリの保存先に替えられます。
- 他の更新手段（MSC、HTTP、CDC、Vendor）との比較は [USB 経由の firmware 更新](../ota-over-usb.ja.md) を参照してください。

## 基本の使い方

### Download（内蔵 OTA）

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceDfu dfu(device);     // 既定は Download と内蔵 OTA

void setup() {
  EspUsbDfuConfig config;
  config.transferBytes = 4096;   // 1 block の最大 byte 数（既定 1024）
  if (dfu.configure(config) != ESP_OK) return;
  dfu.onProgress([](size_t written) { /* これまでに保存した byte 数 */ });
  dfu.onError([](EspUsbDfuStatus status) { /* 失敗の理由を記録する */ });
  if (!device.begin()) { /* device.lastError() を確認 */ }
  // 書込みが終わると、既定では約 500 ms 後に新しい firmware で再起動する
}

void loop() {}
```

- 内蔵 OTA には、application 用の partition が 2 つある partition 表が必要です（Arduino の「Default」など）。
- 送る image は Arduino の普通の `.bin` です（`idf.py dfu` の `dfu.bin` や `*.merged.bin` ではありません）。

### Runtime（ROM loader へ移る）

```cpp
EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Runtime, "Bootloader");
// 既定では、DETACH の status 段の約 100 ms 後に ROM download loader へ再起動する
```

設定を変えるときは `device.end()` → `configure()` → `device.begin()` の順です。再ビルドは要りません。

## 設定

### コンストラクタ

| 引数 | 既定値 | 意味 |
| --- | --- | --- |
| `device` | — | 登録先の `EspUsbDevice` |
| `mode` | `EspUsbDeviceDfuMode::Download` | `Download` または `Runtime` |
| `name` | `nullptr` | interface の名前（文字列 descriptor） |

### `EspUsbDfuConfig`

`configure()` は停止中だけ受け付けます。

| 項目 | 既定値 | 範囲・意味 | FS/HS の違い |
| --- | --- | --- | --- |
| `transferBytes` | 1024 | 1 block（1 回の DFU_DNLOAD）の最大 byte 数。64〜65535。descriptor の `wTransferSize` | なし |
| `transferBuffer` | `nullptr` | Download の block buffer をアプリが用意するときの pointer。`transferBytes` 以上の長さが必要 | なし |
| `detachTimeoutMs` | 1000 | Runtime で DETACH を受けた後、appDETACH を保つ時間の上限（ms）。descriptor の `wDetachTimeOut` | なし |
| `downloadPollMs` | 1 | block の保存中に GETSTATUS で返す待ち時間（bwPollTimeout、ms）。0〜0xFFFFFF | なし |
| `manifestPollMs` | 250 | manifest（書込み後の検証と反映）中に GETSTATUS で返す待ち時間（ms）。0〜0xFFFFFF | なし |
| `manifestationTolerant` | `false` | `true` なら manifest の後も USB reset を待たず、続けて別の転送を始められる | なし |

### FS と HS の違い

DFU は EP0 だけを使います。EP0 の MPS は FS でも HS でも 64 byte です。
`transferBytes` の範囲、poll 時間、DETACH の扱いに速度による違いはありません。

## API

### 設定と開始

| 関数 | 戻り値と制約 |
| --- | --- |
| `esp_err_t configure(config)` | 稼働中：`ESP_ERR_INVALID_STATE`。`transferBytes` や poll 時間が範囲外：`ESP_ERR_INVALID_SIZE`。`transferBuffer` のアドレス計算があふれる：`ESP_ERR_INVALID_ARG` |
| `configuration()` | 設定した値 |
| `mode()` | `Runtime` または `Download` |
| `transferSize()` | descriptor で宣言する `transferBytes` |
| `transferCapacity()` | 確保した Download 用 buffer の byte 数。begin 前・end 後・Runtime では 0 |
| `written()` | 今回の転送で保存した byte 数の累計 |
| `startError()` | 直近の begin の失敗理由 |

`device.begin()` の失敗：確保できないと `ESP_ERR_NO_MEM`、Device に登録していない・二重の begin などは `ESP_ERR_INVALID_STATE` です。確保済みの領域は回収します。

### 進捗と完了（Download）

| 関数 | 説明 |
| --- | --- |
| `onProgress(callback)` | block の保存に成功するたびに、累計 byte 数で呼ぶ。DFU は image 全体の長さを Device に伝えないので、総量は分からない |
| `onError(callback)` | 失敗のたびに `EspUsbDfuStatus` で呼ぶ |
| `onComplete(callback)` | 内蔵 OTA だけ。manifest で検証が通った後に呼ぶ。`false` を返すと image を拒否し、起動先を実行中の partition へ戻す |
| `restartWhenComplete(enable)` | 内蔵 OTA だけ。既定 `true` で、manifest 成功の約 500 ms 後に再起動する。`false` ならアプリが再起動する |
| `firmware()` | 内蔵 OTA が使う `EspUsbDeviceFirmwareUpdate` |

- これらの setter は `esp_err_t` を返し、稼働中は `ESP_ERR_INVALID_STATE` です。
- `manifestationTolerant = false`（既定）：manifest 成功の後は dfuMANIFEST-WAIT-RESET で USB reset を待ちます。
- `manifestationTolerant = true`：manifest 後の GETSTATUS で dfuIDLE に戻り、別の転送を始められます。
- ESP の application でないファイルは、最初の block で `Write` として失敗します。壊れた image は最後の検証で `Verify` として失敗します。どちらも起動先を変えません。

### 保存先を自分で決める（backend）

`EspUsbDfuBackend` の `download` と `manifest` を両方指定すると、内蔵 OTA の代わりにアプリの保存先を使います。

```cpp
EspUsbDfuBackend backend;
backend.download = [](uint16_t block, const uint8_t *data, size_t bytes) {
  // 戻る前にコピーか処理を終える。data を保存してはいけない
  return storeBlock(block, data, bytes) ? EspUsbDfuStatus::Ok : EspUsbDfuStatus::Write;
};
backend.manifest = []() {
  return verifyAndActivate() ? EspUsbDfuStatus::Ok : EspUsbDfuStatus::Verify;
};
backend.abort = []() { discardPartialUpdate(); };   // 任意
dfu.setBackend(backend);
```

| 項目 | 内容 |
| --- | --- |
| `setBackend()` の戻り値 | 稼働中：`ESP_ERR_INVALID_STATE`。`download` と `manifest` の片方だけ、`download` なしで `abort` だけ、Runtime で `download` を指定：`ESP_ERR_INVALID_ARG` |
| 空の backend | 内蔵 OTA へ戻る |
| `download` の `data` | callback の中だけ有効 |
| 一部しか保存できなかったとき | `Ok` を返さず、失敗の status を返す。範囲外の値は `Unknown` に直す |
| `abort` | 保存を始めた転送が、失敗・ABORT・reset・`end()` で終わったときに 1 回呼ぶ。保存前に取り消した block や、成功した manifest の後には呼ばない |
| manifest の後 | 検証・反映・再起動は backend の責任。`onComplete` と `restartWhenComplete` は効かない |

### DETACH（Runtime）

Host が DFU_DETACH を送ったときに、受け付けるかどうかと、受け付けた後の動作を分けて決められます。

| 関数 | 呼ばれる時 | 説明 |
| --- | --- | --- |
| `onDetach(bool callback())` | DETACH の SETUP | `false` を返すと STALL し、状態も予約も変えない。空なら無条件に受け付ける |
| `setDetachHandler(void handler())` | 受け付けた DETACH の status 段の後に 1 回 | 既定の ROM 移行の代わりに動く。handler が bus から離れる責任を持つ。空なら既定の ROM 移行に戻る |

| 段階 | 動作 |
| --- | --- |
| SETUP | 種別 0x21・DETACH・wLength 0 で、状態が appIDLE のときだけ有効。`onDetach` を 1 回呼ぶ |
| status 段の ACK | appDETACH へ移り、handler があれば 1 回呼ぶ。なければ 100 ms 後の ROM loader への再起動を予約する |
| 受け付けた後 | `end()`、USB reset、SET_CONFIGURATION 0 のどれでも取り消さない |
| 期限 | appDETACH を `min(wValue, detachTimeoutMs)` の間保ち、過ぎると appIDLE へ戻る。既定の ROM 移行は wValue に関係なく行う |

- 登録は Runtime の停止中だけです。稼働中は `ESP_ERR_INVALID_STATE`、Download では `ESP_ERR_INVALID_ARG` です。
- 断りたいときは `onDetach` で `false` を返します。受け付けた後で取り消す方法はありません。
- ROM loader がどの USB 口で応答するかは chip で違います（[USB 経由の firmware 更新](../ota-over-usb.ja.md)）。

### 複数の DFU interface

同時に複数の DFU interface を置けます。状態・buffer・backend は共有しません。
内蔵 OTA は同じ保存先へ同時に書かないよう、転送中に使えるのは 1 つだけです（2 つ目は `Target` で失敗）。

### 呼べる文脈

| 操作 | 文脈 |
| --- | --- |
| `configure`、`setBackend`、callback の登録 | 停止中のアプリの task |
| `onProgress`、`onError`、`onComplete`、backend、`onDetach`、DETACH handler | USB worker task の上で同期的に呼ばれる |

- callback の中で待つ、`end()` を呼ぶ、時間のかかる処理をする、のどれもしないでください。重い処理はアプリの task へ渡します。
- 再起動は callback の中ではなく、flag を立てて `loop()` から行うのが安全です。callback の中で再起動すると、Host がまだ終えていない control 転送を切ります。
- backend の `download` は block ごとに同期で呼ばれます。flash の書込みもこの中で行う必要があります（非同期の保存 API はありません）。

## バッファとメモリ

| 領域 | 置き場所 | 確保 |
| --- | --- | --- |
| block buffer（Download のみ） | 通常の `malloc`。または `transferBuffer` | begin。`transferBytes` byte |
| 状態 | 通常の heap | begin |

- block buffer は CPU がコピーするだけなので、DMA の条件も alignment も要りません。外部 buffer は**原理上は PSRAM も使えます**（実機では未確認）。
- 外部 buffer は begin から end まで有効に保ち、直接書き換えないでください。
- Runtime は同じ `wTransferSize` を宣言しますが、block buffer は確保しません。
- block ごとの heap 確保はありません。

## Host OS 側の扱い

| OS | 使う道具 | この library（v3）での確認 |
| --- | --- | --- |
| Linux | `dfu-util`（`303a:*` の udev rule か sudo が必要） | DETACH を pyusb で確認（P4X と Linux PC の HS 直結）。`dfu-util` は未確認 |
| Windows | `dfu-util`（WinUSB が必要） | 未確認 |
| macOS | `dfu-util` | 未確認 |

- 動作は、ESP32 を Host にした試験（S3 同士の FS、P4 一台の loopback、P4 と P4X の HS 直結）と、P4X と Linux PC の HS 直結での DETACH で確認しています。転送の試験は RAM の backend で行いました。`dfu-util` による実際の OTA と再起動は、v3 ではまだ確認していません（v2 では P4 と PC の `dfu-util` で確認）。
- Windows には DFU の標準 driver がありません。本ライブラリは DFU interface に、Microsoft OS 2.0 descriptor で WinUSB の compatible ID を付けます。これで WinUSB が自動で bind する想定ですが、v3 では未確認です。bind しないときは Zadig などで WinUSB を入れてください。
- `dfu-util` が最後の status 読み出しで error を出すことがあります。検証の後、約 500 ms で Device が再起動して見えなくなるためです。書込み自体は終わっているので、board の状態で確かめてください。

## 規格との対応と設計の理由

### DFU 1.1 との対応

基準は [USB-IF DFU 1.1](https://www.usb.org/sites/default/files/DFU_1.1.pdf) です。

| 項目 | 実装 |
| --- | --- |
| interface | class 0xFE、subclass 1。protocol は Runtime が 1、Download が 2（§4.1、§4.2） |
| functional descriptor（§4.1.3） | bcdDFUVersion 1.10、wTransferSize ＝ `transferBytes`、wDetachTimeOut ＝ `detachTimeoutMs` |
| bmAttributes | bitCanDnload は常に 1。Runtime は bitWillDetach、Download は `manifestationTolerant` のとき bitManifestationTolerant。bitCanUpload は 0 |
| 要求（§3） | DETACH（Runtime）、DNLOAD、GETSTATUS、CLRSTATUS、GETSTATE、ABORT。UPLOAD は STALL |
| 状態遷移（§6、付録 A） | 受け取った block は data 段と status 段が終わるまで保存せず、続く GETSTATUS の後で保存先へ渡す |
| block 番号 | 0 から始まり、16 bit で一周する。番号の飛び、`wTransferSize` を超える長さ、不正な要求は dfuERROR。CLRSTATUS で再開できる |
| bStatus（§6.1.2） | `EspUsbDfuStatus` の値は DFU 1.1 の status code（OK、errTARGET … errSTALLEDPKT）と同じ番号 |
| bwPollTimeout | 保存中は `downloadPollMs`、manifest 中は `manifestPollMs` |

- Runtime では、不正な DFU 要求で download 状態へ移ることはありません。
- DFU 1.1 §4.2 の DFU mode では、Device は DFU interface だけを出す想定です。本ライブラリの Download は、実行中の application の構成に protocol 2 の interface を加えます。sketch を動かしたまま更新できるようにするためです。

### 設計の理由

- **独自 driver と実行時の容量。** 使わない block buffer を省け、再ビルドなしで block の大きさを変えられ、複数の interface を置けます。保存先（backend）と USB の状態管理を分けました。代わりに、begin での確保と callback の約束事が増えます。
- **DETACH を「受付の判定」と「受付後の移行」に分けた。** SETUP の時点で断れれば、Host には STALL として明確に伝わります。受け付けた後は、`end()` や reset でも必ず移行します。Host が「受け付けたのに移行しない」状態を見ないためです。
- **既定の ROM 移行は 100 ms 後。** status 段の ACK の後に再起動を予約するので、Host の control 転送は正常に終わります。
- **appDETACH の期限は `min(wValue, detachTimeoutMs)`。** Host の要求と Device の宣言の短い方を使います。

### v2 からの主な変更

- `CFG_TUD_DFU_XFER_BUFSIZE` は `EspUsbDfuConfig::transferBytes` に変わりました。
- `onError` の引数は `uint8_t` から `EspUsbDfuStatus` になりました（`unsigned(status)` で前と同じ番号）。
- v2 の `onDetach(void callback)`（既定の再起動を置き換えるもの）は `setDetachHandler()` になりました。`onDetach()` は SETUP での `bool` の受付判定です。
- setter は `esp_err_t` を返し、停止中だけ受け付けます。
- `setBackend()` で保存先を替えられます。

### 未実装と未確認

- UPLOAD、複数の alternate setting による保存先の選択、非同期の保存 callback は実装していません。
- Runtime だけを使う場合も、Download と内蔵 OTA の処理がリンクされることがあります。
- ESP32-S2 の実機、各 OS の `dfu-util`、v3 での実際の OTA 反映と再起動、block 番号の一周、EP0 の途中切断は未確認です。

## 関連する examples

| example | 内容 |
| --- | --- |
| [FirmwareDFU](../../examples/FirmwareDFU/) | HID keyboard と DFU（Download）を一つの Device に置き、`dfu-util -D` で application を更新する。4096 byte の block |
| [FirmwareBootMode](../../examples/FirmwareBootMode/) | CDC と DFU（Runtime）。`dfu-util -e`、1200 baud、serial の命令で ROM loader へ入る |

関連文書：[USB 経由の firmware 更新](../ota-over-usb.ja.md)、[USB device 開発ガイド](../usb-device-guide.ja.md)、[troubleshooting](../troubleshooting.ja.md)、[v2 からの移行](../migrating-from-v2.ja.md)
