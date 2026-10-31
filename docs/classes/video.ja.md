# USB Video（UVC）

> English: [video.md](video.md)

`EspUsbDeviceVideo` は、USB Video Class（UVC 1.5）のカメラを作る Function です。
Device→Host の一方向だけです。アプリが用意した frame（MJPEG または YUY2）を Host へ送ります。
frame は送り終わるまでライブラリが借用し、どの終わり方でも callback で一度だけ返します。
TinyUSB の Video class は使わず、本ライブラリ独自の driver で動きます。

## 概要と使う場面

- PC の Web 会議アプリやブラウザ（`getUserMedia()`）から、ESP32 の映像をカメラとして見せたいとき。
- カメラの代わりに試験パターンや画面の内容を流したいとき。

できること・できないこと：

| 項目 | 内容 |
| --- | --- |
| 形式 | MJPEG または YUY2（非圧縮）。一つの Function に一つの形式 |
| 解像度と fps | 一つの解像度、一つの固定 fps だけを Host に示す |
| 転送方式 | Isochronous（既定）または Bulk。Function ごとに実行時に選ぶ |
| JPEG の encode | しない。encoder や sensor が作った JPEG を渡す |
| still image、カメラの制御（露出など） | 宣言しない |
| 同時に借用できる frame | Function ごとに 1 つ |

## 基本の使い方

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceVideo camera(device, "Camera"); // 名前は Host に表示される

void setup() {
  EspUsbVideoConfig config;                 // 既定は Isochronous、payload 512 byte
  if (camera.configure(config) != ESP_OK) return;
  camera.setFormat(EspUsbDeviceVideoFormat::Mjpeg);
  camera.setFrameSize(320, 240);
  camera.setFrameRate(15);
  camera.setMaxFrameSize(32000);            // MJPEG 1 枚の上限（byte）
  camera.onFrameResult([](const EspUsbTransferResult &result) {
    // どの終わり方でも、ここで result.buffer が返る。以後は再利用してよい
  });
  if (!device.begin()) { /* device.lastError() を確認 */ }
}

void loop() {
  // Host が Commit し、Isochronous なら alternate 1 も選んでから送れる
  if (camera.streaming() && !camera.frameInFlight()) {
    // const auto submission = camera.submitFrame(jpeg, jpegBytes);
    // if (!submission) { /* submission.error を確認。frame は借用されていない */ }
  }
}
```

- 設定（`configure()`、形式・寸法・fps・最大 frame、callback の登録）はすべて停止中に行い、`device.begin()` で反映します。
- 変えるときは `device.end()` → 設定 → `device.begin()` の順です。再ビルドは要りません。
- frame は fps に合わせて `loop()` から投稿してください。callback の中から次を投稿すると、endpoint が空く速さで送り続けるので、宣言した fps を超えます。

## 設定

### コンストラクタ

| 引数 | 既定値 | 意味 |
| --- | --- | --- |
| `device` | — | 登録先の `EspUsbDevice` |
| `name` | `nullptr` | Function の名前（IAD の iFunction と両 interface の iInterface）。Windows はこれをカメラ名に使う |

### `EspUsbVideoConfig`

| 項目 | 既定値 | 範囲・意味 | FS/HS の違い |
| --- | --- | --- | --- |
| `transport` | `Isochronous` | `Isochronous` または `Bulk` | なし |
| `payloadBytes` | 512 | 1 回の payload の大きさ（UVC header 2 byte を含む）。3〜65535。Isochronous では 1024 まで | Bulk は FS 64 byte／HS 512 byte の packet に分けて送る |
| `isochronousPacketBytes` | 0 | ISO endpoint の packet の大きさ。0 なら `payloadBytes` から決める。明示時は 3〜1024 | 上限は FS 1023、HS 1024 |
| `payloadBuffer` | `nullptr` | payload の staging に使う外部領域。`nullptr` なら begin で自動確保 | なし |
| `payloadBufferBytes` | 0 | 外部領域の長さ | なし |

### 形式の設定（関数）

| 関数 | 既定値 | 範囲 |
| --- | --- | --- |
| `bool setFormat(format)` | `Mjpeg` | `Mjpeg` または `Yuy2` |
| `bool setFrameSize(width, height)` | 320×240 | 1〜4096。YUY2 は幅が偶数 |
| `bool setFrameRate(fps)` | 15 | 1〜255 |
| `bool setMaxFrameSize(bytes)` | 0（自動） | 1 frame の上限。0 なら YUY2 は `幅×高さ×2`、MJPEG はその 1/8 |

- 稼働中や不正な値のときは `false` を返し、元の設定を保ちます。
- 不正な値の例：未知の形式、0 や 4096 を超える寸法、fps 0、bit rate（最大 frame × 8 × fps）が 32 bit を超える設定。
- 検証は他の項目の現在値との組合せで行います。YUY2 の frame は必ず `幅×高さ×2` byte なので、`setMaxFrameSize()` に別の値を入れたままでは `setFormat(Yuy2)` が `false` になります。YUY2 に切り替える前に `setMaxFrameSize(0)` で戻してください。
- MJPEG の既定の最大値（非圧縮の 1/8）は、encoder の出力サイズの保証ではありません。実際の frame が超えるなら上げてください。Host はこの値で受信 buffer を決めるので、小さすぎると画像が欠けます。

### FS と HS の違い

| 項目 | FS | HS |
| --- | --- | --- |
| ISO の間隔 | 1 ms（bInterval 1） | 125 µs（bInterval 1） |
| ISO packet の上限（USB 2.0 §5.6.3） | 1023 byte | 1024 byte（1 microframe に 1 packet） |
| ISO の理論上限（既定 512 byte、header 2 byte を除く） | 510,000 byte/s | 4,080,000 byte/s |
| ISO の理論上限（1023／1024 byte） | 約 1 MB/s | 約 8 MB/s |
| Bulk の packet | 64 byte | 512 byte |

- ISO の 1 payload は、`payloadBytes` と packet の大きさの小さい方です。
- 理論上限は計算値です。その帯域や fps を保てる保証ではありません。
- 160×120 の YUY2（38,400 byte）を FS の既定 packet で送ると、1 frame に約 76 ms かかり、約 13 fps になります。大きな画像は MJPEG か HS を使ってください。
- HS の high-bandwidth（1 microframe に複数 packet）は使いません。
- `isochronousPacketSize(highSpeed)` で、各速度の descriptor に出す packet の大きさを確認できます。

## API

### 設定と状態

| 関数 | 戻り値・エラー |
| --- | --- |
| `esp_err_t configure(config)` | 稼働中：`ESP_ERR_INVALID_STATE`。未知の `transport`：`ESP_ERR_INVALID_ARG`。`payloadBytes` が 3〜65535 の外、Isochronous で 1024 超、`isochronousPacketBytes` が 0 以外で 3〜1024 の外：`ESP_ERR_INVALID_SIZE` |
| `configuration()` | 設定した値 |
| `format()` / `width()` / `height()` / `frameRate()` / `maxFrameSize()` | 現在の設定 |
| `bulkStreaming()` | Bulk を選んでいれば `true` |
| `descriptorLength(highSpeed)` | その速度の descriptor の byte 数 |
| `isochronousPacketSize(highSpeed)` | その速度の ISO packet の大きさ |
| `payloadBufferSize()` | 確保中の staging 容量。begin 前と end 後は 0 |
| `commit()` | Host が Commit した値（`EspUsbDeviceVideoCommit`）。Commit 前は全部 0 |
| `startError()` | 直近の begin の失敗理由 |

`device.begin()` が失敗したときの `device.lastError()`：

| 値 | 原因 |
| --- | --- |
| `ESP_ERR_INVALID_ARG` | 形式・寸法・fps・最大 frame の組合せが不正。外部 staging が条件を満たさない |
| `ESP_ERR_INVALID_SIZE` | endpoint や descriptor の容量が足りない。または構成全体の IN endpoint が controller の送信 FIFO に収まらない |
| `ESP_ERR_NO_MEM` | メモリを確保できない |
| `ESP_ERR_INVALID_STATE` | Device に登録していない、二重の begin、など |

S2／S3 の controller は送信 FIFO が小さく、全 endpoint で共有します。ISO のカメラと Audio の Capture を同時に置くと収まらないことがあります。`device.begin()` は構成全体の IN endpoint を検査し、収まらなければ失敗します。列挙はできるのに何も届かない Device にしないためです。
そのときは `payloadBytes` を下げる、Bulk を選ぶ、別の Function を外す、のどれかで調整します。

### frame を送る

| 関数 | 戻り値 |
| --- | --- |
| `EspUsbTransferSubmission submitFrame(frame, length)` | 受け付けたら bool 変換が `true`、`id` が 0 以外 |
| `bool sendFrame(frame, length)` | 受け付けたら `true`（`submitFrame` の簡易形） |
| `esp_err_t cancelFrame(id)` | 取消しの依頼の結果 |
| `bool streaming()` | 送れる状態か（Commit 済みで、Isochronous なら alternate 1 選択済み） |
| `bool frameInFlight()` | frame を借用中か（取消し処理中も含む） |

拒否したときは `id` が 0 で、frame は借用されず、callback も呼ばれません。`error` の意味：

| `error` | 意味 |
| --- | --- |
| `BadArgument` | NULL、長さ 0、最大 frame 超え、YUY2 の長さ不一致、アドレス範囲のあふれ |
| `NotMounted` | 未開始、未 Commit、ISO の alternate 未選択、停止中・再構成中・HALT 中など |
| `Busy` | 前の frame をまだ借用中 |
| `TransferFailed` | 内部の投稿を受け付けられない、または id を発行できない |

- frame の領域は、返却の通知が来るまで解放も書換えもしないでください。
- frame そのものに置き場所や alignment の条件はありません（payload ごとに staging へコピーして送ります）。
- `cancelFrame(id)` は USB worker に取消しを頼むだけです。`ESP_OK` でも、その時点ではまだ返っていません。返却は `onFrameResult` で届きます。古い id と 0 は `ESP_ERR_NOT_FOUND`、停止処理中などは `ESP_ERR_INVALID_STATE` です。

### 返却と通知

| 関数 | 説明 |
| --- | --- |
| `esp_err_t onFrameResult(callback)` | frame の返却。`Success`、`Cancelled`、`BusReset`、`TransferError`、`Stopped` のどれでも一度だけ呼ぶ |
| `esp_err_t onFrameComplete(callback)` | 成功したときだけ呼ぶ。両方あれば result → complete の順 |
| `esp_err_t onCommit(callback)` | Host が Commit したとき。所有権の返却の通知ではない |

- `EspUsbTransferResult` の `actualBytes` は、UVC header を除いた、送ったと確認できた byte 数です。中断で確定できないときは `actualBytesKnown` が `false` です。
- callback の登録は停止中だけです。稼働中は `ESP_ERR_INVALID_STATE` です。
- 中断を扱うアプリは `onFrameResult` を使ってください。`onFrameComplete` は成功しか知らせません。

### Bulk の HALT と CLEAR

Bulk で Host が SET_FEATURE(ENDPOINT_HALT) を送ったとき：

- DMA を止め、借用中の frame を `TransferError` で一度だけ返します。
- HALT 中は `streaming()` が `false`、`submitFrame()` は `NotMounted` です。

CLEAR_FEATURE(ENDPOINT_HALT) のとき：

- その endpoint だけ DATA0 に戻し、今の Commit のまま再開します。HALT なしの CLEAR で frame を借用中なら `Cancelled` で返します。
- 再開後は新しい frame を投稿してください。中断した frame の続きからは送りません。

Isochronous には HALT がありません。alternate 0／1 の切替で停止と再開をします。

### 呼べる文脈

| 操作 | 文脈 |
| --- | --- |
| `configure`、`set*`、callback の登録 | 停止中のアプリの task |
| `submitFrame`、`sendFrame`、`cancelFrame`、状態の照会 | アプリの task。または `onFrameResult` の中 |
| `onFrameResult`、`onFrameComplete`、`onCommit` | USB worker task の上で呼ばれる。軽い処理だけにする |

callback の中で `device.end()`、Function の破棄、callback の差替えをしないでください。
同時に `submitFrame` が呼ばれても順に処理し、2 つ目は `Busy` です。

## バッファとメモリ

| 領域 | 置き場所 | 確保 |
| --- | --- | --- |
| payload の staging（1 つ） | DMA 可能な内部 RAM、64 byte 境界 | begin。`payloadBytes` を 64 byte 単位に切り上げた大きさ |
| Function の状態 | 通常の heap | begin |
| frame | アプリの領域（借用） | アプリ |

- 外部 staging（`payloadBuffer`）は次をすべて満たす必要があります。満たさないと begin が `ESP_ERR_INVALID_ARG` で失敗します。
  - 64 byte 境界のアドレス
  - 長さ（`payloadBufferBytes`）が `payloadBytes` を 64 byte 単位に切り上げた値以上
  - 先頭から末尾まで DMA 可能な内部 RAM（**PSRAM は不可**）
- 外部 staging は begin から end まで有効に保ち、その間は触らないでください。
- frame 本体は CPU が staging へコピーするので、置き場所に DMA の条件はありません。**PSRAM に置いた frame も原理上は使えます**。ただし実機では未確認です。
- 通常の frame 送信中に heap は使いません。使わない Video のための static buffer もありません。

## Host OS 側の扱い

| OS | 標準の driver | この library（v3）での確認 |
| --- | --- | --- |
| Windows | `usbvideo.sys` | 未確認（v2 では Windows 11 で bind と受信を確認） |
| Linux | `uvcvideo`（V4L2） | 未確認 |
| macOS | 標準の UVC 対応 | 未確認 |

- 動作は、ESP32 を Host にした試験（S3 同士の FS、P4 一台の loopback、P4 と P4X の HS 直結）で確認しています。JPEG の decode と PC での表示は、v3 ではまだ確認していません。
- Host は descriptor に書いた一つの形式・解像度・fps だけを選べます。アプリ側の設定と合わせてください。
- Windows は Function の名前（`name`）をカメラ名として表示します。
- frame を送ったことと、PC がその frame を表示したことは別です。

## 規格との対応と設計の理由

### UVC 1.5 との対応

| 項目 | 実装 |
| --- | --- |
| class 版 | bcdUVC 0x0150、interface protocol PC_PROTOCOL_15 |
| interface | Video Control と Video Streaming の 2 つ（IAD でまとめる）。VC の割込み endpoint はなし |
| entity | Camera Terminal → Output Terminal（USB Streaming） |
| 形式 descriptor | MJPEG または Uncompressed（YUY2 GUID）＋ Frame descriptor 1 つ＋ Color Matching |
| fps の表し方 | Frame descriptor に一つの frame 間隔（100 ns 単位） |
| Probe／Commit（§4.3.1.1） | UVC 1.5 の 48 byte と、短い 26／34 byte の SET を受け付ける。Probe は対応する値に合わせて返し、Commit は対応しない値を拒否 |
| payload header（§2.4.3.3） | 2 byte。EOH、FID、frame の最後で EOF。PTS／SCR は付けない |
| Isochronous | alternate 0 は endpoint なし（帯域 0。USB 2.0 §5.6.3）、alternate 1 に ISO IN（asynchronous） |
| Bulk | alternate 0 に Bulk IN。payload の途中終端が packet 長の倍数なら ZLP を送る |

### 設計の理由

- **方式を実行時に選ぶ。** v2 の `CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE` と `CFG_TUD_VIDEO_STREAMING_BULK` をやめ、同じ binary で Function ごとに選べるようにしました。TinyUSB Video を有効にする旧 build flag は、二重の driver を防ぐため compile error にしています。
- **frame は借用、payload はコピー。** frame に DMA の条件を課さずに済みます。代わりに staging 一つ分の内部 RAM とコピーの費用がかかります。
- **全部の終わり方で返す。** 取消し・bus reset・HALT・停止でも frame を一度だけ返すので、アプリは返却後に安全に再利用できます。
- **単一の global をやめた。** 状態は Function ごとに持ち、複数の Video を置けます。
- **投稿の戻り値を Vendor／CDC の `writeDirect` とそろえた。** `EspUsbTransferSubmission` で理由が分かります。

### v2 からの主な変更

- 転送方式と payload の大きさは `configure()` で指定します（build flag は廃止）。
- `payloadBufferSize()` などの照会は instance ごとで、begin 前は 0 です。
- frame の返却は `onFrameResult()` で、中断した frame も返ります。
- `submitFrame()` は `EspUsbTransferSubmission` を返します。v2 の 3 引数の形はありません。
- callback の登録は `esp_err_t` を返し、稼働中は拒否します。

## 関連する examples

| example | 内容 |
| --- | --- |
| [VideoCamera](../../examples/VideoCamera/) | 試験パターン（160×120 YUY2、15 fps）を送るカメラ。`loop()` から fps に合わせて投稿し、`onFrameResult` で返却を受ける |

関連文書：[USB device 開発ガイド](../usb-device-guide.ja.md)、[troubleshooting](../troubleshooting.ja.md)、[v2 からの移行](../migrating-from-v2.ja.md)
