# EspUsbDeviceVideo API（v3）

USB Video Class（UVC 1.5）のカメラを作るクラスです。Device→Host の一方向だけで、アプリが用意した frame（MJPEG または YUY2）を Host へ送ります。
frame は送信が終わるまでライブラリが借用し、終了理由ごとに callback で返します。TinyUSB の Video class は使わず、本体内部の独自 driver で動きます。

## 基本の使い方

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceVideo camera(device, "Camera"); // 名前は Host に表示される

void setup() {
  EspUsbVideoConfig config;                     // 既定は Isochronous、payload 512 byte
  // config.transport = EspUsbVideoTransport::Bulk; // 可変長の MJPEG なら Bulk も選べる
  if (camera.configure(config) != ESP_OK) return;
  camera.setFormat(EspUsbDeviceVideoFormat::Mjpeg);
  camera.setFrameSize(320, 240);
  camera.setFrameRate(15);
  camera.setMaxFrameSize(32000);                // MJPEG 1 枚の上限（byte）
  camera.onFrameResult([](const EspUsbTransferResult &result) {
    // どの終了理由でも、ここから result.buffer を再利用してよい
  });
  if (!device.begin()) { /* device.lastError() を確認 */ }
}

void loop() {
  // Host が Commit し（ISO なら streaming の alternate も選んで）から送る
  if (camera.streaming() && !camera.frameInFlight()) {
    // const auto submission = camera.submitFrame(jpeg, jpegBytes);
    // if (!submission) { /* submission.error を確認。frame は借用されていない */ }
  }
}
```

設定（`configure()`、format・サイズ・fps・最大 frame、callback の登録）はすべて停止中に行い、`device.begin()` で反映します。稼働中は拒否するので、変えるときは `device.end()` してから設定し直します。

### 主な設定

`EspUsbVideoConfig` の項目です。

| 項目 | 既定値 | 意味 | FS/HSでの違い |
| --- | --- | --- | --- |
| `transport` | `Isochronous` | 転送方式（`Isochronous` または `Bulk`） | なし |
| `payloadBytes` | 512 | 1 回の payload の大きさ（UVC header 2 byte を含む）。3〜65535、Isochronous では 1024 まで | Bulk では FS 64 byte／HS 512 byte の packet に分けて送る |
| `isochronousPacketBytes` | 0 | ISO endpoint の 1 packet の大きさ。0 なら `payloadBytes` から決める。明示時は 3〜1024 | 上限は FS 1023、HS 1024 |
| `payloadBuffer` | `nullptr` | payload の staging に使う外部領域（DMA 可能な内部 RAM、64 byte 境界）。`nullptr` なら begin 時に自動確保 | なし |
| `payloadBufferBytes` | 0 | 外部領域の長さ | なし |

format などは関数で設定します。

| 関数 | 既定値 | 範囲・意味 |
| --- | --- | --- |
| `setFormat(EspUsbDeviceVideoFormat)` | `Mjpeg` | `Mjpeg` または `Yuy2` |
| `setFrameSize(width, height)` | 320×240 | 1〜4096。YUY2 は幅が偶数 |
| `setFrameRate(fps)` | 15 | 1〜255 |
| `setMaxFrameSize(bytes)` | 0（自動） | 1 frame の上限。0 なら YUY2 は `幅×高さ×2`、MJPEG はその 1/8 |

## 設定と転送方式

### configure の検証

`esp_err_t configure(const EspUsbVideoConfig &config)` の戻り値：

| 戻り値 | 条件 |
| --- | --- |
| `ESP_OK` | 受け付けた |
| `ESP_ERR_INVALID_STATE` | 稼働中 |
| `ESP_ERR_INVALID_ARG` | 未知の `transport` |
| `ESP_ERR_INVALID_SIZE` | `payloadBytes` が 3〜65535 の外、Isochronous で 1024 超、`isochronousPacketBytes` が 0 以外で 3〜1024 の外 |

外部 staging の条件は `begin()` で確かめます。足りなければ `begin()` が失敗し、`startError()` が `ESP_ERR_INVALID_ARG` になります（[PSRAMとバッファ](#psramとバッファ)）。

### Isochronous と Bulk の選び方

- **Isochronous（既定）**：帯域を予約して周期的に送ります。足りなくても再送はしません。
- **Bulk**：USB の再送が使えます。帯域と遅延は他の通信の影響を受けます。可変サイズの MJPEG や、帯域の予約より確実な転送を優先する用途に向きます。

どちらも SoC の FIFO、他の Function との組合せ、frame の大きさと fps から選び、`configure()` で明示してください。controller の FIFO や endpoint が足りない構成は開始に失敗します。API の数値の上限は、すべての SoC での成功を保証するものではありません。

既定の ISO packet 512 byte は FS でも使えます。FS の bulk（64 byte packet）の制限を ISO に当てはめる必要はありません。理論上の上限は [FSとHSの違い](#fsとhsの違い) を参照してください。

### 形式と交渉

- 一つの形式・一つの解像度・一つの固定 frame 間隔だけを Host に示します。
- YUY2 は幅が偶数で、frame は必ず `幅×高さ×2` byte です。`setMaxFrameSize()` に別の値は指定できません。
- MJPEG の最大 frame の既定（非圧縮サイズの 1/8）は encoder の出力サイズの保証ではありません。実際の frame がこれを超えるなら、`setMaxFrameSize()` で上げてください。小さすぎると Host 側で画像が欠けます。
- `setFormat()`／`setFrameSize()`／`setFrameRate()`／`setMaxFrameSize()` は、稼働中や不正な値のとき false を返し、元の設定を保ちます。不正な値とは、未知の format、0 や 4096 超の寸法、fps 0、bit rate（最大 frame × 8 × fps）が 32 bit を超える設定などです。
- 検証は他の項目の現在値との組合せで行います。たとえば `setMaxFrameSize()` で YUY2 の実サイズと違う値を入れたままだと、`setFormat(Yuy2)` は false になります。YUY2 に切り替える前に `setMaxFrameSize(0)` で既定に戻してください。
- UVC 1.5 の 48 byte の Probe／Commit と、26／34 byte の短い SET を受け付けます。Probe は対応する形式・間隔と payload 上限に合わせて返し、Commit は対応しない値を拒否します。
- Isochronous は alternate 0 で停止、1 で送信します。Bulk は alternate 0 のまま送信します。
- Bulk で payload の途中終端が packet 長の倍数になるときは、ZLP（長さ 0 の packet）を送ります。
- `commit()` で Host が Commit した値を読めます（Commit 前は 0）。`onCommit()` で Commit の通知を受けられます。

## frameの所有権と通知

### 送る

| 関数 | 戻り値 |
| --- | --- |
| `bool sendFrame(const void *frame, size_t length)` | 受け付けたら true |
| `EspUsbTransferSubmission submitFrame(const void *frame, size_t length)` | 受け付けたら bool 変換が true、`id` が 0 以外 |
| `esp_err_t cancelFrame(uint64_t id)` | 取消しの依頼結果 |
| `bool streaming() const` | 送れる状態か（Commit 済み、ISO なら alternate 1 選択済み） |
| `bool frameInFlight() const` | frame を借用中か（取消し処理中も含む） |

`submitFrame()` を拒否したときは `id` が 0 で、frame は借用されず、callback も呼ばれません。`error` の意味：

| error | 意味 |
| --- | --- |
| `BadArgument` | NULL、長さ 0、最大 frame 超え、YUY2 の長さ不一致、アドレス範囲の overflow |
| `NotMounted` | 未開始、未 Commit、ISO の alternate 未選択、停止中・再構成中・HALT 中など送れない状態 |
| `Busy` | 前の frame をまだ借用中 |
| `TransferFailed` | 内部の投稿を受け付けられない、または id を発行できない |

- 一つの Function で借用できる frame は一つだけです。同時に呼ばれても順に処理し、2 つ目は `Busy` です。
- frame 自体に DMA の alignment や置き場所の条件はありません（payload ごとに staging へコピーして送ります）。
- 返却の通知が来るまで、frame の領域を解放したり書き換えたりしないでください。

### 返却を受け取る

`onFrameResult(callback)` に `EspUsbTransferResult` を受け取る関数を登録します。frame は次のどの終わり方でも一度だけ返ります：`Success`、`Cancelled`、`BusReset`、`TransferError`、`Stopped`。

- `actualBytes` は UVC header を除いた、送ったと確認できた frame の byte 数です。中断などで確定できないときは `actualBytesKnown` が false です。
- 成功時だけ呼ばれる `onFrameComplete()` もあります。両方登録すると、result → complete の順に呼びます。
- callback は USB worker の上で動きます。軽い処理と次の frame の投稿だけにしてください。`device.end()`、Function の破棄、callback の差替えを callback の中で行わないでください。
- callback の登録（`onFrameResult`／`onFrameComplete`／`onCommit`）は稼働中だと `ESP_ERR_INVALID_STATE` です。

### 取り消す

- `cancelFrame(id)` は worker に取消しを依頼するだけです。`ESP_OK` でも、その時点ではまだ返却されていません。返却は `onFrameResult` で届きます。
- 古い id や 0 は `ESP_ERR_NOT_FOUND`、停止処理中などで依頼できないときは `ESP_ERR_INVALID_STATE` です。
- USB 転送を止め、古い完了 event を排出してから返します。alternate の変更や再 Commit でも、古い frame の返却を待ちます。`onCommit` は所有権の返却の通知ではありません。

### 停止と破棄

- 稼働中の Function を外から破棄すると、Device を止め、worker と DMA が止まってから状態を解放します。
- Device に登録していない Function の `begin()`、二重の `begin()`、他の runtime が稼働中の `begin()` は `ESP_ERR_INVALID_STATE`（`startError()`）です。

## bulk endpointのHALTとCLEAR

Bulk で Host が SET_FEATURE ENDPOINT_HALT を送ったとき：

- DMA と古い完了 event を片付け、借用中の frame を `TransferError` で一度だけ返します。送った量が確定できなければ `actualBytesKnown` は false で、確認できた分だけを `actualBytes` に入れます。
- HALT 中は `streaming()` が false、`submitFrame()` は `NotMounted`（id 0）、`sendFrame()` は false です。

CLEAR_FEATURE のとき：

- 対象 endpoint だけ DATA0 に戻し、今の Commit のまま再開します。HALT なしの CLEAR で frame を借用中なら `Cancelled` で返します。
- HALT による返却がまだ続いている間に CLEAR が届いても、`TransferError` を `Cancelled` で上書きしません。
- SET／CLEAR を繰り返しても返却は重複せず、別の Video の frame や PID には影響しません。
- 再開後は新しい frame を投稿してください。中断した frame の続きからは送りません。

ISO の alternate 制御はこれまでどおりで、この回復経路の対象外です。追加の queue や buffer は確保しません。

## FSとHSの違い

ESP32-S3 は FS（Full Speed、12 Mbps）、ESP32-P4／P4X の HS 側 controller は HS（High Speed、480 Mbps）でつながります。

| 項目 | FS | HS |
| --- | --- | --- |
| ISO の送信間隔 | 1 ms ごと（bInterval 1） | 125 µs ごと（bInterval 1） |
| ISO の 1 packet の上限 | 1023 byte | 1024 byte |
| ISO の packet（既定 512 byte のとき） | 512 byte | 512 byte |
| ISO の理論上限（既定 512 byte、header 2 byte を除く） | 510,000 byte/s | 4,080,000 byte/s |
| ISO の理論上限（1023／1024 byte のとき） | 約 1 MB/s | 約 8 MB/s |
| Bulk の packet の大きさ | 64 byte | 512 byte |

- `isochronousPacketSize(highSpeed)` で、それぞれの速度で descriptor に出す packet の大きさを確認できます。
- HS の ISO は 1 microframe に 1 packet だけです（1 microframe に複数 packet を送る high-bandwidth 方式は使いません）。
- ISO の 1 payload の大きさは、`payloadBytes` と packet の小さい方です。
- 理論上限は計算値です。実測の帯域や、その fps を保てることの保証ではありません。

## PSRAMとバッファ

- payload の staging は DMA 可能な内部 RAM です。自動確保では `payloadBytes` を 64 byte 単位に切り上げた大きさを確保します。
- 外部 staging（`payloadBuffer`）は次をすべて満たす必要があります。満たさないと `begin()` が失敗します（`ESP_ERR_INVALID_ARG`）。
  - 64 byte 境界のアドレス
  - `payloadBytes` を 64 byte 単位に切り上げた長さ以上（`payloadBufferBytes`）
  - 領域の先頭から末尾まで、DMA 可能な内部 RAM（PSRAM は不可）
- 外部 staging は `begin()` から `end()` まで有効に保ち、driver が使っている間は触らないでください。
- `payloadBufferSize()` は確保中の容量（`payloadBytes`）を返し、begin 前と end 後は 0 です。設定値は `configuration()` で読めます。
- frame 本体は CPU で staging にコピーするので、置き場所に DMA の条件はありません。ただし frame を PSRAM に置いた構成は、実機ではまだ確認していません。
- begin で確保するのは Function の状態と staging 一つだけで、通常の frame 送信中には確保しません。使わない Video のための大きな static buffer はありません。

## 設計判断の理由

- **実行時に方式を選ぶ**：2.x の `CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE` と `CFG_TUD_VIDEO_STREAMING_BULK` を廃止し、同じ binary で Function ごとに選べるようにしました。TinyUSB Video を有効にする旧 build flag は、二重 driver を防ぐため compile error にしています。
- **frame は借用、payload はコピー**：frame に DMA の条件を課さずに済みます。代わりに staging 一つ分の内部 RAM と、コピーの費用がかかります。
- **全終了理由での返却**：中断（取消し、bus reset、HALT、停止）でも frame を一度だけ返すので、アプリは返却後に安全に再利用できます。中断を扱うアプリは `onFrameResult` を使ってください。
- **単一 global owner の廃止**：複数の Video を Function ごとの状態で扱います。
- **submitFrame の戻り値**：Vendor／CDC の `writeDirect` と同じ `EspUsbTransferSubmission` にそろえました（2026-10-30、ArduinoCore-CH32RV との調整）。旧 3 引数の `submitFrame(frame, length, id)` は残していません。
- 2.x からの影響：容量や方式の照会は static から instance へ、callback の登録は `esp_err_t` を返すように変わりました。begin 時の資源不足と、外部 DMA 領域の条件を扱う必要があります。

## 実測の記録

ここから下は実装の各段階での試験の記録です。commit hash、所要時間、件数は当時の値です。serial の原本は `/tmp/pytest-embedded/…`、JUnit／transcript のコピーは `validation/.pytest-results/` にあります。特記がない限り S3–S3（FS）での結果です。

### HS 直結（2026-10-31）

既存の S3 peer 試験の sketch に P4／P4X の profile を加え、HS 直結でも収集しました。aade7e0（ISO OUT の偶奇の再 arm。Video は送信のみなので直接の対象ではない）の後に、HS 両方向の audio／video 各 17 件が成功しています。詳細は [P4/P4X と追加試験環境](V3_P4_VARIANTS_AND_BENCH.ja.md) を参照してください。PC の OS での認定は含みません。

### CH32RV調整後の投稿API（2026-10-30）

`submitFrame` を `EspUsbTransferSubmission` の戻り値に統一しました（02c7509）。S3 の制御／HALT 回復 2 件、ISO／Bulk の借用寿命 6 件、P4 両 controller × 両方式 4 件の計 12 件が、標準 combo all の 1 回の実行で成功しました（179.81 秒）。不正 frame／NULL／空の拒否と id 0、未開始、Busy、cancel／end／再 Commit での返却、guard と heap を確認。P4 は FS の実 link で、HS の帯域・P4X・PC OS の認定は含みません。記録 `video-submission-initial*`。

### Video bulk HALTの実測

2026-10-28。修正前は SET_FEATURE が成功しても借用 frame が返りませんでした（`video-halt-baseline.xml`、41.34 秒）。修正後は関連 10 件が 10 passed、154.16 秒。2 つの Video で 16 回の SET／CLEAR、途中 payload の読出し 2 回、復帰後と別 Function の正常 frame 10 回を確認。各 Video の返却は計 8 件（Success 5、Cancelled 1、TransferError 2）、重複・誤 id・残留なし。送信中 HALT の 4 件は `actualBytesKnown=false`。774 取得点の確保失敗回復、heap 346564→346564。試験 sketch のカウンタ初期化不足で一括実行が一度失敗した記録も残しています。記録 `video-halt-final*`、`video-halt-audit.txt`。未確認：全 EP0 順序、SET／CLEAR と bus reset／end のすべての競合、OS や他 SoC／HS の実通信（当時）。

### legacy class ソース除去後の全体試験

2026-10-27。無効化済みの TinyUSB class 実装 10 本と関連 header を削除しました。標準 `--combo all` で 26 passed、1703.91 秒（670 取得点の確保失敗回復、ISO／Bulk 各 3 構成、複数 Video の制御を含む）。nm で legacy class driver の symbol がないことを確認。Core 3.3.12 / Host 2.10.1 / plugin 1.10.0、S3–S3。記録 `runtime-video-all*`、`video-control-cleanup-nm.txt`。

### 確保失敗・複数 Function・制御要求

2026-10-27。`video-driver-extended.xml` は 9 passed、175.58 秒。ISO／Bulk の内部・外部 staging、2 Function、65535 byte の staging を含む 670 取得点を一つずつ失敗させ、すべて NoMem、資源の回収、再開始、heap の回復を確認しました。`video_control` は 2 Function を同時に列挙し、MJPEG の 65 byte payload と YUY2 の 128 byte payload を実通信。26／34／48 byte の Probe／Commit、GET_LEN／INFO／MIN／MAX／RES／DEF、22 件の不正要求の STALL と復帰、片方の alt／再 Commit／取消し中に他方の frame を保持・完了できることを確認。Probe の GET_MIN の payload を 3、GET_RES を 1 に直し、受け付ける範囲と一致させました。62 byte frame の Bulk ZLP 終端、外部 DMA 領域前後の canary も含みます。記録 `video-driver-extended*`。

### 独自 driver の初回と取消し

独自 driver 初回の `video-driver-initial.xml` は関連 2 件成功（161.26 秒）。257／511／512／1021 byte の ISO 転送、未登録 Function の拒否、64 KiB frame 送信中の破棄を確認。最終関連試験 `video-driver-cancel.xml` は 7 passed、212.48 秒。ISO／Bulk 各 64／65／512 byte payload の 6 構成を同じ binary の実行時 API で選び、各構成で 4 frame の全 byte 一致、64 KiB frame の取消しと古い id の拒否、取消し直後の再投稿、送信中破棄で Stopped が一度だけ返ることを確認しました。65 byte 構成は 64 byte 境界・128 byte 以上の外部 DMA 領域を使います。VideoOnly／DeviceOnly の最小 build と nm で、TinyUSB Video の symbol がないこと、DeviceOnly に独自 Video がリンクされないことも確認しました（`*-video-nm.txt`）。記録 `video-driver-cancel*`。

未確認：PC／OS、JPEG の decode、取消し・reset・再 Commit の競合の網羅。
