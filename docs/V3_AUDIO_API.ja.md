# EspUsbAudioFunction API（v3）

USB Audio Class（UAC1 または UAC2）のスピーカー（Playback：Host→Device）とマイク（Capture：Device→Host）を作るクラスです。
PCM はリングバッファ（ring）を通して `read()`／`write()` し、Host からの音量・ミュート・sample rate の変更は event として受け取れます。
TinyUSB の Audio class は使わず、本体内部の独自 driver で動きます。

## 基本の使い方

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbAudioFunction audio(device, EspUsbAudioProtocol::Uac2); // 省略時は Uac1
EspUsbAudioPlaybackStream &playback = audio.addPlaybackStream();
uint8_t pcm[192];

void setup() {
  // {sampleRate, channels, bytesPerSample, bitsPerSample}
  if (!playback.addFormat({48000, 2, 2, 16})) return; // 失敗理由は device.lastError()

  EspUsbAudioConfig config;
  config.eventCapacity = 0;          // event を使わないなら 0
  config.playbackBufferFrames = 256; // 256 frame × 4 byte = 1024 byte
  if (audio.configure(config) != ESP_OK) return;

  if (!device.begin()) { /* device.lastError() を確認 */ }
}

void loop() {
  size_t n = playback.read(pcm, sizeof(pcm)); // frame 単位に切り下げた byte 数
  // n byte の PCM を DAC などへ渡す
}
```

手順は「Device と Function を作る → stream と format を登録 → `configure()` → `device.begin()`」です。
Function の `begin()` を自分で先に呼ぶ必要はありません。開始と終了は Device の `begin()`／`end()` でそろえます。
設定を変えるときは `device.end()` → `configure()`／`clearFormats()`／`addFormat()` → `device.begin()` の順です。再ビルドは要りません。

### 主な設定

`EspUsbAudioConfig` の項目です。`configure()` は停止中だけ受け付けます。

| 項目 | 既定値 | 意味 | FS/HSでの違い |
| --- | --- | --- | --- |
| `eventCapacity` | 8 | event queue の件数。0 なら event を作らない | なし |
| `playbackBufferFrames` | 256 | Playback ring の容量（sample frame 数） | なし |
| `captureBufferFrames` | 256 | Capture ring の容量（sample frame 数） | なし |
| `playbackBuffer` / `captureBuffer` | `nullptr` | ring に使う外部領域。`nullptr` なら begin 時に自動確保 | なし |
| `playbackBufferBytes` / `captureBufferBytes` | 0 | 外部領域の長さ（byte）。外部領域を渡すときは必須 | なし |
| `playbackOverflowPolicy` | `DropOldest` | Playback ring があふれたときにどちらを捨てるか | なし |

- コンストラクタの第2引数 `EspUsbAudioProtocol` の既定は `Uac1` です。UAC2 を使うときは明示します。
- `EspUsbAudioFormat` の既定値は `{48000, 2, 2, 16}`（48 kHz、ステレオ、1 sample 2 byte、有効 16 bit）です。
- 「sample frame」は全 channel の 1 時刻分です。1 frame の byte 数は `channels × bytesPerSample` です（有効 bit 数 `bitsPerSample` からは計算しません）。
- ring の byte 数は「その方向に登録した format のうち最大の frame byte 数 × bufferFrames」です。
- USB の packet の大きさは FS と HS で変わります（[FSとHSの違い](#fsとhsの違い)）。設定項目自体に速度による違いはありません。

## stream と format

### stream を選ぶ

- `addPlaybackStream()`／`addCaptureStream()` で使う方向を選びます。両方選ぶと双方向（duplex）になります。
- 稼働中に新しい方向を追加しようとすると、既存の stream 参照を返して構成は変えず、`device.lastError()` を `ESP_ERR_INVALID_STATE` にします。
- `hasPlaybackStream()`／`hasCaptureStream()` で選択済みかを確認できます。

### format を登録する（`addFormat`）

`bool addFormat(const EspUsbAudioFormat &format)` は stream ごとに呼びます。失敗すると false を返し、理由を `device.lastError()` に入れます（成功時は `ESP_OK` に戻します）。

| 失敗理由 | `device.lastError()` |
| --- | --- |
| 稼働中に呼んだ | `ESP_ERR_INVALID_STATE` |
| 保存領域を確保できない | `ESP_ERR_NO_MEM` |
| 1 packet が FS の上限（1023 byte）を超える | `ESP_ERR_INVALID_SIZE` |
| channels・bytesPerSample・bitsPerSample が範囲外、rate が 0 | `ESP_ERR_INVALID_ARG` |

受け付ける値は次のとおりです。

- channels：1 または 2
- bytesPerSample：2、3、4
- bitsPerSample：16、24、32 のいずれかで、`bytesPerSample × 8` 以下
- 1 packet（1 ms 分＋1 frame）が FS の上限 1023 byte 以下に収まること。HS でつながる場合も、FS で表せる format だけを受け付けます。

format は登録した個数分だけ保存します（1 個 8 byte。固定長の配列は持ちません）。設定は `end()` 後も残り、Function の破棄で解放します。

### 同じPCM形状での複数sample rate

同じ PCM 形状（channel 数・byte 数・有効 bit 数が同じ）を、rate だけ変えて複数登録できます。Host が登録済みの rate から選びます。

```cpp
if (!playback.addFormat({48000, 2, 2, 16})) return;
if (!playback.addFormat({44100, 2, 2, 16})) return;
```

- 一つの Function の clock は共有です。両方向を使うときは、Playback と Capture の rate の集合を同じにします。
- 開始時は、最初に登録した Playback format（Playback がなければ Capture）の rate を使います。
- UAC1 は format descriptor に rate の一覧を載せ、endpoint の SET_CUR／GET_CUR も実際の clock に合わせます。UAC2 は Clock Source の CUR／RANGE 要求で一覧を返します。未登録の rate は拒否し、現在の rate を変えません。
- rate の上限数：UAC1 は 1 一覧 82 rate まで（descriptor 長が 1 byte のため）、UAC2 は 5461 subrange まで（control 長が 16 bit のため）。超える設定は拒否します。
- UAC2 で rate が 2 個以上あるときだけ、RANGE 応答用に `2 + 12 × rate数` byte を begin 時に確保し、end で解放します。
- rate 変更時の無音期間や、外部 codec の clock への追従は保証しません。

参考：[USB Audio 1.0](https://www.usb.org/sites/default/files/audio10.pdf)、[Microsoft の UAC2 clock/range 要件](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/usb-2-0-audio-drivers)

### PCM形状のalternate選択

byte 数や有効 bit 数が違う format も同じ stream に登録できます。

```cpp
for (uint32_t rate : {24000u, 16000u}) {
  if (!playback.addFormat({rate, 2, 2, 16})) return; // alternate 1
  if (!playback.addFormat({rate, 2, 3, 24})) return; // alternate 2
  if (!playback.addFormat({rate, 2, 4, 32})) return; // alternate 3
}
```

- 同じ PCM 形状は一つの alternate setting にまとまります。形状を最初に登録した順に 1〜255 を割り当てます。0 は stream 停止です。
- 同じ形状に rate を足しても alternate は増えません。
- すべての形状と両方向が同じ rate の集合を持つ必要があります。足りない組合せや、同じ方向で channel 数が変わる設定は `device.begin()` で拒否します（実測では `ESP_ERR_INVALID_SIZE`）。登録順が違っても、集合が同じなら受け付けます。
- Host が alternate を切り替えると、古い DMA を止め、古い完了 event を排出してから endpoint を有効にし直します。前の形式の PCM が残った ring は捨てます。切替中の `read()`／`write()` は 0 を返します。
- `bool currentFormat(EspUsbAudioFormat &format) const` で、選ばれている形状と現在の rate を取得できます。未選択・停止・切替中は false で、引数を変えません。取得した値はその時点の写しで、次の `read()`／`write()` まで形式が固定される意味ではありません。
- アプリは event や `currentFormat()` に合わせて PCM の処理形式を変えてください。codec や I2S の自動設定は行いません。

### 停止後のformat入替え

両 stream の `esp_err_t clearFormats()` で、登録した format を消せます。

```cpp
device.end();
if (playback.clearFormats() != ESP_OK) return;
if (!playback.addFormat({44100, 2, 2, 16})) return;
if (!device.begin()) return;
```

- 停止中だけ使えます。稼働中は `ESP_ERR_INVALID_STATE` を返し、何も変えません。空でも `ESP_OK` です。
- 方向の選択は解除しません。format が空のままでは `begin()` が失敗するので、再登録してから開始します。
- もう一方の stream の format、`EspUsbAudioConfig`、Function の登録は残ります。
- `clearBuffer()`（PCM データを消す）とは別の操作です。
- clear と再登録の間は設定が空になるので、再登録の失敗（確保失敗など）を処理してください。

## PCM の読み書き

### Playback（Host→Device）

| 関数 | 戻り値 |
| --- | --- |
| `size_t read(void *data, size_t length)` | 読んだ byte 数。frame 境界に切り下げ |
| `int available() const` | 未読 byte 数 |
| `size_t capacity() const` | 現在の format の frame 境界に切り下げた ring 容量。begin 前と end 後は 0 |
| `size_t packetCapacity() const` | packet 用 DMA 領域の容量（全 format の FS 最大 packet）。begin 前と end 後は 0 |
| `bool clearBuffer()` | 未読の PCM を消す |
| `EspUsbAudioStreamStats stats() const` / `void resetStats()` | 統計の取得と初期化 |

ring があふれたときの動きは [Playbackのあふれ方針](#playbackのあふれ方針playbackoverflowpolicy) を参照してください。

### Capture（Device→Host）

| 関数 | 戻り値 |
| --- | --- |
| `size_t write(const void *data, size_t length)` | ring に入った byte 数。入りきらない分は書かない |
| `size_t availableForWrite() const` | 空き byte 数（frame 境界に切り下げ） |
| `capacity()`／`packetCapacity()`／`clearBuffer()`／`stats()`／`resetStats()` | Playback と同じ |

- `write()` は入る分だけコピーし、実際の byte 数を返します。古いデータを上書きしません。入りきらなかった分は `overrunCount`／`overrunBytes` に数えます。戻り値を必ず確認してください。
- Host が読みに来たときに ring の PCM が足りなければ、足りない分を無音で埋め、`underrunCount`／`underrunBytes` に数えます。
- sample rate が USB の 1 区間（FS 1 ms、HS 125 µs）で割り切れないとき（例：44.1 kHz）、端数を区間ごとに振り分けます。
- alternate 0 の間に `write()` で先に貯めておけます。最初に alternate 1 が選ばれたとき、その PCM を保持します。停止や稼働中の alternate 再設定では ring を消します。

### 共通の注意

- 長さは常に現在の frame 境界へ切り下げます。frame の途中で区切った PCM は保存も返却もしません。
- `clearBuffer()` は ring だけを消します。すでに USB 側に出した（arm した）packet は取り消さないので、その後の完了で PCM が再び入ることがあります。
- ring 容量を 0 にすると ring を確保しません。Playback は受信をすべて捨てて overrun に数え、Capture は無音を送って underrun に数えます。USB 転送用の DMA 領域は残ります。
- `stats()` の `transferredBytes` は USB で正常に送受信した byte 数です。アプリが `read()` した量や ring に入った量とは別です。
- カウンタは `uint32_t` なので、長時間の集計では一周（wrap）を考慮してください。統計は `begin()` で 0 に戻ります。

## Playbackのあふれ方針（playbackOverflowPolicy）

Playback ring が満杯のとき、新しく届いた PCM と未読の PCM のどちらを捨てるかを選びます。Capture の `write()` には関係しません。

```cpp
enum class EspUsbAudioOverflowPolicy : uint8_t {
  DropNewest, // 古い未読を残し、新着のうち入らない部分を捨てる
  DropOldest, // 古い未読を押し出し、最新の完全な frame を残す（既定）
};
```

- 既定は `DropOldest` です。ライブ再生で、読み出しが遅れた後に古い音を流し続けにくくするためです。
- v3 の当初の動作（未読を残す）が必要なら `DropNewest` を指定します。どちらも欠落のない録音は保証しません。
- `configure()` で停止中だけ指定できます。稼働中は `ESP_ERR_INVALID_STATE`、未知の値は `ESP_ERR_INVALID_ARG` で、どちらも既存の設定を保ちます。`configuration()` で現在の値を読めます。

例（文字は sample frame）：

| 状態 | 新着 | DropNewest の結果 | DropOldest の結果 |
| --- | --- | --- | --- |
| 容量4、未読 A/B/C | D/E/F | A/B/C/D | C/D/E/F |
| 容量4、満杯 A/B/C/D | E/F | A/B/C/D | C/D/E/F |
| 容量2、空 | A/B/C | A/B | B/C |
| 容量0 | A/B | 空 | 空 |

正確な規則は次のとおりです。F＝1 frame の byte 数、B＝受信 byte 数、C＝ring の実効容量（物理容量を F の倍数へ切り下げた値）、U＝未読 byte 数、A＝`floor(B/F)×F`（新着のうち完全な frame の部分）とします。

| 方針 | 処理後の ring | その packet で数える破棄 byte 数 |
| --- | --- | --- |
| DropNewest | 古い U byte＋新着の先頭 K byte（K＝min(A, C−U)） | B−K |
| DropOldest | R＝min(A, C)、D＝max(0, U+R−C)。古い ring の先頭 D byte を捨て、新着の完全 frame 部分の末尾 R byte を足す | D＋B−R |

- 破棄があった packet ごとに `overrunCount` を 1 増やし、`overrunBytes` に上の値を足します。DropOldest では押し出した過去の PCM も数えます。packet 末尾の不完全な frame の破棄も含みます。
- 末尾の不完全な frame は両方とも捨てます。次の packet と結合する隠れ buffer はありません。
- この計算は内部 lock の中で 1 回にまとめて行います。アプリが `capacity()` と `available()` を別々に読んでも、同じ時点の値になるとは限りません。
- 追加の ring・断片 buffer・監視 task・破棄ごとの callback はありません。

照合先：[AudioTransport の受信処理](../src/internal/EspUsbAudioTransport.cpp)

## 外部ringのbyte長指定

ring に自分で用意した領域を使えます。内部確保の既定のままなら、この指定は要りません。

```cpp
uint8_t storage[520]; // device.end() まで有効にしておく
EspUsbAudioConfig config;
config.playbackBufferFrames = 65;
config.playbackBuffer = storage;
config.playbackBufferBytes = sizeof(storage);
if (audio.configure(config) != ESP_OK) return;
if (!device.begin()) { /* device.lastError() を確認 */ return; }
```

- 必要な長さは「その方向に登録した最大の frame byte 数 × bufferFrames」です。例：ステレオ 16 bit（4 byte）と 32 bit（8 byte）を登録して 65 frame なら 520 byte。16 bit だけで計算した 260 byte では足りません。
- format は後から足せるので、長さの最終確認は `begin()` で行います。足りなければ `begin()` が失敗し（`ESP_ERR_INVALID_SIZE`）、外部領域には書き込みません。直して `configure()` → `begin()` をやり直せます。
- pointer と長さの片方だけを指定する、frame 数 0 で外部領域を渡す、などは `configure()` が `ESP_ERR_INVALID_ARG` を返します。
- 両方向の外部領域が重なる指定、pointer と長さの overflow も拒否します。
- 渡した長さが必要量より大きくても、使うのは要求した frame 数分だけです。
- `device.end()` まで有効に保ち、その間アプリから直接書き換えないでください。領域が本当に有効かどうかはライブラリでは確かめられません。
- ring は CPU がコピーするだけなので、DMA 用の alignment は要りません。

`configure()` の失敗は戻り値で、`device.begin()` の失敗は `device.lastError()` で確認します。`configure()` の失敗で `device.lastError()` が更新されることは前提にしないでください。

## 音量・ミュートと event

### 音量とミュート

| 関数 | 説明 |
| --- | --- |
| `hasMute(direction, channel)` / `hasVolume(direction, channel)` | その control があるか |
| `getMute` / `setMute` | ミュートの取得・設定 |
| `getVolume` / `setVolume` | 音量の取得・設定（`int16_t`、1/256 dB 単位） |
| `getVolumeRange(range, direction, channel)` | 音量の範囲 |
| `currentSampleRate()` | 現在の sample rate |

- `direction` の既定は Playback、`channel` の既定は 0（master）です。1 以上は各 channel です。
- 音量の範囲は −90 dB〜0 dB、1 dB 刻みです（値では −23040〜0、256 刻み）。範囲外や刻みに合わない値は false を返します。
- 値が変わると event を作ります（アプリからの変更でも作ります）。

### event

| 関数 | 説明 |
| --- | --- |
| `bool pollEvent(EspUsbAudioEvent &event)` | 1 件取り出す。空なら false |
| `size_t pendingEvents() const` | 未読の件数 |
| `uint32_t droppedEvents() const` | queue 満杯で捨てた件数 |
| `void clearEvents()` | 未読と破棄数を消す |
| `size_t eventCapacity() const` | 確保した件数。begin 前と end 後は 0（要求値は `configuration().eventCapacity`） |

event の種類（`EspUsbAudioEventType`）は `SampleRateChanged`、`MuteChanged`、`VolumeChanged`、`StreamStateChanged` です。`EspUsbAudioEvent` の `volumeDb256` は 1/256 dB 単位です。

- 容量 0 なら event を作らず、`pendingEvents()`／`droppedEvents()` とも 0 です。
- queue が満杯なら新しい event を捨て、`droppedEvents()` を増やします。保存済みの順序は保ちます。
- 配列は begin 時に一度だけ確保し、取り出しや追加で heap を使いません。queue は短い critical section で守ります。
- 確保に失敗すると `begin()` が失敗し、`startError()`／`device.lastError()` が `ESP_ERR_NO_MEM` になります。確保済みの領域はすべて回収します。
- byte 数が overflow する容量は、`configure()` が `ESP_ERR_INVALID_SIZE` を返します。

## 状態と制約

- 稼働中は `configure()`（`ESP_ERR_INVALID_STATE`）、`addFormat()`（false）、`clearFormats()`（`ESP_ERR_INVALID_STATE`）を拒否します。
- Device に登録していない Function の手動 `begin()` は `ESP_ERR_INVALID_STATE` で拒否します。
- 稼働中に Function を破棄すると Device を止め、USB worker と DMA が止まってから領域を解放します。
- 未対応：同じ方向での channel 数の切替、PCM 形状ごとに違う rate 集合、外部 codec の clock に合わせた feedback、callback／借用方式の PCM API。
- 安全性を確かめていない組合せ：`setMute`／`setVolume` と Host の要求の同時更新、`end()` と `read()`／`write()` の同時実行。
- 2.x の `CFG_TUD_AUDIO` 系の build flag は使えません（有効にすると compile error）。format は `addFormat()`、容量は `configure()` で指定します。

## FSとHSの違い

ESP32-S3 は FS（Full Speed、12 Mbps）、ESP32-P4／P4X の HS 側 controller は HS（High Speed、480 Mbps）でつながります。packet の大きさと間隔は、つながった速度に合わせて descriptor が変わります。

| 項目 | FS | HS |
| --- | --- | --- |
| データの送受信間隔 | 1 ms ごと（bInterval 1） | 125 µs ごと（bInterval 1） |
| 1 packet の大きさ | `ceil(rate × frame byte ÷ 1000)` ＋ 1 frame | `ceil(rate × frame byte ÷ 8000)` ＋ 1 frame |
| 例：48 kHz ステレオ 16 bit | 196 byte | 28 byte |
| 例：44.1 kHz ステレオ 16 bit | 181 byte | 27 byte |
| packet の上限 | 1023 byte | 1024 byte（ただし format は FS の上限 1023 で検証） |
| UAC2 の feedback endpoint | 4 byte、1 ms ごと（bInterval 1） | 4 byte、1 ms ごと（bInterval 4＝8 microframe） |
| feedback の値 | rate × 65536 ÷ 1000（1 ms あたりの sample 数、16.16 固定小数点） | rate × 65536 ÷ 8000（125 µs あたりの sample 数） |

- packet 用の DMA 領域（`packetCapacity()`）は、HS でつながっても FS の最大 packet で確保します。begin の時点では速度が決まっていないためです。
- format は FS の上限で検証するので、FS で表せない format（例：192 kHz ステレオ 32 bit）は HS でも登録できません。
- UAC2 の Playback は非同期（asynchronous）endpoint と feedback endpoint の組です。feedback は USB の公称 clock から計算した値で、外部 codec の clock のずれには追従しません。UAC1 は feedback endpoint を持ちません。
- HS で Host が 1 区間あけて送ってきたとき、以前は次の packet まで取りこぼしていました。aade7e0 で直しています（[記録](#hs直結での-iso-out-の取りこぼし修正2026-10-31)）。

## PSRAMとバッファ

- packet 用の DMA 領域と UAC2 の feedback 領域は、DMA 可能な内部 RAM（64 byte 境界）に確保します。
- 自動確保の ring は通常の `malloc` で確保します。外部 ring の置き場所（内部 RAM か PSRAM か）は検査しません。
- Audio の ring を PSRAM に置いた構成は、実機ではまだ確認していません。

## 設計判断の理由

- **容量を実行時に指定する**：2.x は build flag で固定長の FIFO と単一の global instance を持っていました。v3 は Function ごとに `configure()` で容量を決め、使う方向と format の分だけ begin 時に確保します。利点は、通知不要なら配列を省ける・再ビルドなしで容量を変えられる・開始失敗が明示される・稼働中の構成破壊を防げることです。代わりに begin 時の確保失敗、コピーと lock の費用、Capture の戻り値確認が必要になります。
- **Capture は部分書込み**：旧 TinyUSB FIFO のように上書きせず、入る分だけ受け取って実数を返します。上書きに頼っていたアプリは戻り値の確認が必要です。
- **Playback の既定を DropOldest にした**：ArduinoCore-CH32RV と合意した仕様です（[調整状況](V3_CH32RV_API_ALIGNMENT.ja.md)）。v3 の当初の実装は DropNewest 相当でした。移行時は既定の変更に注意してください（[移行ガイド](V3_MIGRATION.ja.md)）。
- **packet 用 DMA 領域は FS の最大で確保**：速度が決まる前に確保するため、どちらの速度でも足りる大きさにしています。
- **alternate ごとの配列を持たない**：packet と ring は最大の形状で一つだけ確保し、FIFO も物理 endpoint ごとの最大 packet で一度だけ予約します。alternate を足しても積み増さず、切替時の heap 取得もありません。
- **endpoint 番号の予約幅**：Audio は IN と OUT を独立に割り当て、反対方向なら番号を共有します。予約幅は IN の必要数と OUT の必要数の大きい方です（下の記録を参照）。後続 Function の endpoint 番号は詰まるので、Host は descriptor からアドレスを読んでください。

## 実測の記録

ここから下は、実装の各段階で行った試験の記録です。commit hash、所要時間、件数は当時の値です。serial の原本は `/tmp/pytest-embedded/…`、JUnit／transcript のコピーは `validation/.pytest-results/` にあります。特記がない限り S3–S3（FS）での結果です。

### HS直結での ISO OUT の取りこぼし修正（2026-10-31）

- 症状：P4X Host → 旧 P4 Device の HS 直結で、audio_overflow が 5 回中 2 回失敗しました。Host が 1 区間あけて送ると、届いた packet が 1 つおきに欠けました。
- 原因：DWC2 の ISO OUT は、arm するときに次の (micro)frame の偶奇を指定します。Host が 1 区間あけると偶奇が合わず、incomplete ISO OUT になります。以前は endpoint を止めて失敗を返し、worker が arm し直していましたが、HS の 125 µs には間に合わず次の packet も失っていました。
- 修正（aade7e0）：割り込み処理（ISR）の中で、同じ DMA 領域のまま偶奇を反転して arm し直します。OUT には古いデータを送る問題がないので、回数の上限は設けません。
- 結果：audio_overflow は 10 回連続で成功。HS 両方向の audio／video 各 17 件、S3 の audio 9 件、旧 P4 loopback の audio 4 件が成功しました。連続 200 packet の受信率（`test_iso_out_rate`）は修正前から両方向・S3 とも 200/200 で、欠けるのは Host が間をあけたときだけでした。
- 前段の修正：FS の間欠 OUT で最初の packet しか受信できない問題を 4480513 で直していました（incomplete OUT 割り込みを ISO OUT endpoint を開いたときだけ有効にし、失敗を返す前に DMA を止める）。
- 詳細：[P4/P4X と追加試験環境](V3_P4_VARIANTS_AND_BENCH.ja.md)、[共通 core](V3_CORE_API.ja.md) の「ISO OUTの欠落frame回復」。

### P4 loopback での Audio ISO（e577698）

UAC1／UAC2 × 両 controller の 4 件、33.95 秒で成功。16 kHz／mono／16 bit で双方向に PCM を流し、開始後 100 ms を warmup として約 350 ms を照合しました。8 測定で playback 89600 byte、capture 88064 byte、不一致なし、overrun／underrun 0。UAC2 は feedback 更新あり・reject 0。初回は UAC1 にも feedback を要求した試験条件の誤りで失敗し、試験を直しました（製品の変更なし）。記録 `p4-audio-{initial,after,duplex}*`。

### ESPでの実装と実測（2026-10-30）

playbackOverflowPolicy の実装（2408716）。受信完了の処理だけを方針で分岐し、policy は config の 1 byte です。`validation/s3/audio_overflow` は、raw SDK Host が長さと個数を決めた OUT packet に frame 番号入りの PCM を送り、Device が読んだ番号列と統計を、Python で上の式を実装した model と比べます。

| 条件 | 内容 |
| --- | --- |
| 両方針 | 520 byte の ring で 10 frame 読んだ後に wrap・満杯・末尾 2 byte の不完全 frame。DropNewest は 10〜139、DropOldest は 94〜223 を保持 |
| ring が packet より小さい | 32 byte（8 frame）、40 byte（F=8 で 5 frame）。DropNewest は 2〜4, 48〜49、DropOldest は 68〜72 |
| F=6 と非倍数容量 | 物理 520 byte を 516 byte（86 frame）として扱う |
| 容量0 | 全量を overrun に数え、読出しは 0 |
| 設定 | 既定 DropOldest、未知の値は INVALID_ARG で設定を保持、開始中の configure は INVALID_STATE |
| alternate 解除 | 未読を破棄し available 0 |

各条件で overrunCount／overrunBytes／transferredBytes も model と一致。計 11 開始で 1 passed、31.49 秒。受信の欠落 0 が前提で、ISO packet が欠ければ失敗とします。既定変更後の Audio 関連回帰（S3 audio_* 6 probe＋P4 Audio、計 12 件）も 12 passed、690.49 秒。記録 `audio-overflow*`、`audio-overflow-related*`。

### Playback保持方針のAPI仕様案

2026-10-30 に ArduinoCore-CH32RV と調整した案です。命名と DropOldest 既定で合意し、CH32RV 側は X035C8 実機で試験済みと報告を受けました（先方の結果で、ESP の検証ではありません）。合意した仕様は [Playbackのあふれ方針](#playbackのあふれ方針playbackoverflowpolicy) のとおりです。

### CH32RV調整向け：Playbackあふれ仕様の初稿（2026-10-30）

調整前の ESP 実装（22afc15）は、今の DropNewest と同じ動作でした（未読を保持し、入らない新着を捨てる）。overrunBytes に packet 末尾の不完全 frame の破棄も含むこと、`clearBuffer`／`resetStats` の意味、Capture の部分書込みは、調整後も変わっていません。

### Functionのendpoint番号予約幅

`endpointCount()` は、次の Function へ進める endpoint 番号の幅を返します。

| 構成 | 旧予約幅 | 新予約幅 |
| --- | --- | --- |
| UAC1 Playback のみ／Capture のみ | 1 | 1 |
| UAC1 duplex | 2 | 1 |
| UAC2 Playback＋feedback | 2 | 1 |
| UAC2 Capture のみ | 1 | 1 |
| UAC2 duplex＋feedback | 3 | 2 |

修正前は 4 つの UAC1 duplex（24 kHz／ステレオ 16 bit）が番号 1/3/5/7 を必要とし、`ESP_ERR_INVALID_SIZE` で開始できませんでした。修正後は開始でき、各組を 0→3→1→2→0 の順に選んで Function ごとに異なる PCM を照合しました（全 8 endpoint の同時転送ではなく、構成を保ったまま順に転送）。1 passed、67.36 秒。関連 6 件も 6 passed、441.14 秒、816 開始＋14 設定の確保失敗から回復、heap 346704→346704。記録 `audio-function-span*`、`audio-function-span-related*`。

### Audio＋CDCの同時通信とTX FIFO割当て

`validation/s3/audio_cdc` は UAC1／UAC2、24／48 kHz、Audio 先／CDC 先の 4 構成で、再生・録音・feedback と CDC を同時に動かします。修正前の Audio 先 UAC2 では、DWC2 が endpoint 5 に FIFO 5 を割り当てていたため、CDC の送信が Host に届きませんでした。選んだ IN endpoint の順に FIFO を詰めて割り当てるよう直しました（endpoint アドレスは変えません）。修正後は 1 passed、36.60 秒。4 構成で 752／752／938／924 回の CDC 往復と 187 回の PCM 進行の重なりを確認しました（進行の確認値で、性能指標ではありません）。製品 f5d1571 を固定した全 40 件も 40 passed、1997.81 秒。記録 `audio-cdc*`、`tx-fifo-all*`。

### 外部ringと不完全なformat設定の確認

製品 033f2d8 のまま、UAC1／UAC2 で内蔵・外部 ring それぞれ 4 PCM alternate を切り替え、計 32 回の ISO 交換を確認しました。外部 ring は非整列 pointer から 520 byte を使い、切替後と end 後に前後の guard を検査。6 byte/frame で capacity 516、8 byte/frame で 520。2 passed、124.49 秒。共有 clock を満たさない設定（rate 不足、集合違い、方向間の不一致、方向内の channel 数変更）は `ESP_ERR_INVALID_SIZE` で拒否し、その後も同じ Function を開始・終了できることを 12 条件 × 2 回で確認（1 passed、31.33 秒）。記録 `audio-external-ring*`、`audio-capability-settings*`。

外部 ring の長さ指定を加えた後の関連 6 件は 6 passed、369.22 秒。長さ 32／63 byte 不足を計 16 回、pointer と長さの不正な組を計 16 回拒否し、guard を維持。Function 本体は 296→304 byte（長さ 2 項目）。記録 `audio-buffer-extents*`。

### PCM形状の alternate 選択の実測

S3 の関連 7 件は 7 passed、358.65 秒。UAC1／UAC2 それぞれで 16-bit/2-byte、24-bit/3-byte、32-bit/4-byte、24-bit/4-byte を選び、計 16 回の ISO 往復。14 回は旧 ring に 0xa5 を置き、新形式へ混入しないことを確認。32 個の不正 alternate 要求、816 開始＋14 設定の確保失敗回復、heap 346704→346704。製品 033f2d8 固定の全体試験も 38 passed、2544.60 秒。記録 `audio-alternates-related*`、`audio-alternates-all*`。EspUsbHost 2.10.1 は列挙時に claim した alternate しか開始しないため、probe は SDK の claim／release と ISO 転送を使います。

[AudioPlayback 例](../examples/AudioPlayback/AudioPlayback.ino) は 2 PCM 形状 × 2 rate、192 byte の ring、event 容量 0、`currentFormat` を示します。Core 3.3.12 の S3／S2／P4 で compile を確認しました（sketch 自体の実機試験ではありません）。記録 `audio-playback-example*`。

### 複数 sample rate の実測

S3 の関連 5 件は 5 passed、175.61 秒。13 rate を登録した UAC1／UAC2 で、再構成せずに 48→44.1→48 kHz を転送し、clock と PCM を照合。未登録の 22050 Hz は拒否し、clock を維持。158 byte の UAC2 RANGE を 2〜200 byte の各要求長で、内容・実長・guard まで確認しました。記録 `audio-rates*`。

### 停止後の format 入替えの実測

UAC2／UAC1 で同じ Function／stream 参照のまま 48→44.1→48 kHz を各 1 系列、計 6 回 wire で確認。10 構成で稼働中の clear 拒否を確認。関連 4 件は 4 passed、200.04 秒。初回の probe 期待値の誤りは `audio-reconfigure-initial-expectation-error*` に残しています。記録 `audio-reconfigure*`。

### format保存領域とrate照会の容量整理

format を両方向 8 個ずつ持つ固定配列をやめ、登録数分だけ保存するようにしました。S3 で測った sizeof：

| 型 | 変更前 | 変更後 |
| --- | ---: | ---: |
| EspUsbAudioFunction | 444 | 296 |
| AudioStreamModel | 76 | 16 |
| AudioControlState | 80 | 52 |

その後、外部 ring の長さ 2 項目で Function は 304 byte になりました（format の heap 領域は含みません）。関連 3 件は 3 passed、242.85 秒、774 開始取得点の失敗回復、heap 346704→346704。記録 `audio-storage*`、`audio-storage-layout-{before,after}.json`。

### Audio ISOの実bus休止

`audio_stream` で alternate と Host の ISO pipe を保ったまま、root port を休止・再開しました。UAC2 双方向 48 kHz、UAC1 双方向 44.1 kHz、UAC2 playback のみ（1 frame の外部 ring）、UAC2 capture のみ（ring なし）の 4 構成です。休止中は転送カウンタが止まり、再開後に PCM と UAC2 feedback を確認しました。新たな SET_INTERFACE は行いません。1 passed、69.35 秒（製品 512edb6）。初回の失敗（probe が停止済み controller の register に触れた）は `audio-suspend-initial-stop-error*` に残しています。S3 FS の Host 起点の休止で、OS／HS／remote wakeup は含みません。

### 独自 driver への置換（2026-10-27）

TinyUSB の Audio class／FIFO と単一 global instance を撤去しました。event のみの段階で 2 passed（77.39 秒、413 取得点の確保失敗回復）、独自 PCM driver の関連 3 件で 3 passed（145.99 秒、438 取得点）、標準 `--combo all` で 16 passed（823.56 秒）。記録 `audio-events-related*`、`audio-pcm-related*`、`runtime-audio-all*`。全体の記録は [validation README](../validation/README.ja.md) を参照してください。

残作業は [TODO](../TODO.ja.md#優先する残作業) を参照してください。
