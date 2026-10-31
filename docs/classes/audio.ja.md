# USB Audio（UAC1 / UAC2）

> English: [audio.md](audio.md)

`EspUsbAudioFunction` は、USB Audio Class のスピーカーとマイクを作る Function です。

- **Playback**（Host→Device）：PC から見て「出力装置（スピーカー）」になります。
- **Capture**（Device→Host）：PC から見て「入力装置（マイク）」になります。
- 両方を選ぶと、一つの Function で双方向（ヘッドセット）になります。

PCM は ring buffer（以下 ring）を通して `read()`／`write()` します。
Host からの音量・ミュート・sample rate・stream 開始の変化は event で受け取れます。
TinyUSB の Audio class は使わず、本ライブラリ独自の driver で動きます。

## 概要と使う場面

| 用途 | 選ぶもの |
| --- | --- |
| PC の音を ESP32 の DAC・I2S・スピーカーで鳴らす | Playback |
| ESP32 のマイクや生成音を PC へ録音させる | Capture |
| 通話用のヘッドセット、loopback の試験 | Playback ＋ Capture |

- UAC1 と UAC2 を選べます。既定は UAC1 です。UAC2 はコンストラクタで明示します。
- 音量やミュートの値は通知するだけです。PCM に自動で掛けることはしません。
- codec や I2S の設定、外部 codec の clock への追従は行いません。アプリの仕事です。

## 基本の使い方

手順は「Device と Function を作る → stream と format を登録 → `configure()` → `device.begin()`」です。

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbAudioFunction audio(device, EspUsbAudioProtocol::Uac2); // 省略すると Uac1
EspUsbAudioPlaybackStream &playback = audio.addPlaybackStream();
uint8_t pcm[192];

void setup() {
  // {sampleRate, channels, bytesPerSample, bitsPerSample}
  if (!playback.addFormat({48000, 2, 2, 16})) return; // 理由は device.lastError()

  EspUsbAudioConfig config;
  config.playbackBufferFrames = 480; // 10 ms 分（480 frame × 4 byte）
  if (audio.configure(config) != ESP_OK) return;

  if (!device.begin()) { /* device.lastError() を確認 */ }
}

void loop() {
  size_t n = playback.read(pcm, sizeof(pcm)); // frame 単位に切り下げた byte 数
  // n byte の PCM を DAC などへ渡す

  EspUsbAudioEvent event;
  while (audio.pollEvent(event)) {
    // SampleRateChanged、MuteChanged、VolumeChanged、StreamStateChanged
  }
}
```

- Function の開始と終了は `device.begin()`／`device.end()` で行います。Function の `begin()` は公開していないので、呼ぶと compile error になります。
- 設定を変えるときは `device.end()` → `configure()`／`clearFormats()`／`addFormat()` → `device.begin()` の順です。再ビルドは要りません。

## 設定

### コンストラクタ

| 引数 | 既定値 | 意味 |
| --- | --- | --- |
| `device` | — | 登録先の `EspUsbDevice` |
| `protocol` | `EspUsbAudioProtocol::Uac1` | `Uac1` または `Uac2` |

### format（`EspUsbAudioFormat`）

`addFormat()` に渡す値です。stream ごとに登録します。

| 項目 | 既定値 | 受け付ける値 |
| --- | --- | --- |
| `sampleRate` | 48000 | 1 以上（Hz） |
| `channels` | 2 | 1 または 2 |
| `bytesPerSample` | 2 | 2、3、4（1 sample の byte 数。UAC の subslot） |
| `bitsPerSample` | 16 | 16、24、32 のいずれかで、`bytesPerSample × 8` 以下（有効 bit 数） |

- 1 packet（1 区間分の PCM ＋ 1 frame）が **FS の上限 1023 byte 以下**であること。HS でつながる場合も同じ上限で検証します（制限事項）。
- 「sample frame」（以下 frame）は全 channel の 1 時刻分です。1 frame の byte 数は `channels × bytesPerSample` です。

### `EspUsbAudioConfig`

`configure()` は停止中だけ受け付けます。

| 項目 | 既定値 | 範囲・意味 | FS/HS の違い |
| --- | --- | --- | --- |
| `eventCapacity` | 8 | event queue の件数。0 なら queue を作らない | なし |
| `playbackBufferFrames` | 256 | Playback ring の容量（frame 数）。0 なら ring を作らない | なし |
| `captureBufferFrames` | 256 | Capture ring の容量（frame 数）。0 なら ring を作らない | なし |
| `playbackBuffer` / `captureBuffer` | `nullptr` | ring に使う外部領域。`nullptr` なら begin で自動確保 | なし |
| `playbackBufferBytes` / `captureBufferBytes` | 0 | 外部領域の長さ（byte）。外部領域を渡すときは必須 | なし |
| `playbackOverflowPolicy` | `DropOldest` | Playback ring が満杯のときに捨てる側。`DropOldest` または `DropNewest` | なし |

- ring の byte 数は「その方向に登録した format の最大 frame byte 数 × bufferFrames」です。
- 設定項目そのものに速度による違いはありません。packet の大きさと間隔は速度で変わります（下の「FS と HS の違い」）。

### FS と HS の違い

ESP32-S3／S2 は FS（12 Mbps）でつながります。ESP32-P4 の HS controller は HS（480 Mbps）でつながります。
descriptor はつながった速度に合わせて作り分けます。

| 項目 | FS | HS |
| --- | --- | --- |
| データの間隔 | 1 ms（bInterval 1） | 125 µs（bInterval 1） |
| 1 packet の大きさ | `ceil(rate × frame byte ÷ 1000)` ＋ 1 frame | `ceil(rate × frame byte ÷ 8000)` ＋ 1 frame |
| 例：48 kHz ステレオ 16 bit | 196 byte | 28 byte |
| 例：44.1 kHz ステレオ 16 bit | 181 byte | 27 byte |
| ISO packet の上限（USB 2.0 §5.6.3） | 1023 byte | 1024 byte（ただし format は FS の 1023 byte で検証） |
| UAC2 の feedback endpoint | 3 byte、bInterval 1（1 ms） | 4 byte、bInterval 4（2^(4−1) × 125 µs ＝ 1 ms） |
| feedback の値 | 10.14 固定小数点。rate × 2^14 ÷ 1000（1 ms あたりの sample 数） | 16.16 固定小数点。rate × 2^16 ÷ 8000（125 µs あたりの sample 数） |

- sample rate が区間で割り切れないとき（例：44.1 kHz）は、端数を区間ごとに振り分けます。
- FS で表せない format（例：192 kHz ステレオ 32 bit）は、HS でも登録できません。

## API

### stream の選択

| 関数 | 戻り値・動作 |
| --- | --- |
| `addPlaybackStream()` / `addCaptureStream()` | stream の参照。稼働中に呼ぶと既存の参照を返し、構成は変えず、`device.lastError()` を `ESP_ERR_INVALID_STATE` にする |
| `hasPlaybackStream()` / `hasCaptureStream()` | 選択済みなら `true` |
| `protocol()` | `Uac1` または `Uac2` |

### format の登録（各 stream）

| 関数 | 戻り値 | 失敗の理由（`device.lastError()`） |
| --- | --- | --- |
| `bool addFormat(format)` | 成功で `true`（`lastError` は `ESP_OK` に戻る） | 稼働中：`ESP_ERR_INVALID_STATE`。値が範囲外・rate 0・同じ format の重複：`ESP_ERR_INVALID_ARG`。1 packet が 1023 byte 超：`ESP_ERR_INVALID_SIZE`。保存領域を確保できない：`ESP_ERR_NO_MEM` |
| `esp_err_t clearFormats()` | `ESP_OK`。空でも `ESP_OK` | 稼働中：`ESP_ERR_INVALID_STATE`（何も変えない） |
| `bool currentFormat(format)` | Host が選んだ形状と現在の rate を写す。未選択・停止中・切替中は `false` で、引数を変えない | — |

**rate の集合**

- 同じ PCM 形状（channels・bytesPerSample・bitsPerSample が同じ）を、rate だけ変えて複数登録できます。Host が登録済みの rate から選びます。
- 一つの Function の clock は共有です。**すべての形状と両方向が同じ rate の集合を持つ**必要があります。登録の順番は問いません。
- 開始時の rate は、最初に登録した Playback の format（Playback がなければ Capture）の rate です。
- 未登録の rate を Host が要求すると拒否し、現在の rate を保ちます。

**形状ごとの alternate setting**

- 同じ PCM 形状は一つの alternate setting にまとまります。形状を初めて登録した順に 1〜255 を割り当てます。0 は stream 停止です。
- 同じ方向で channel 数が違う format は登録できません（`begin()` で拒否）。方向が違えば channel 数は違ってもかまいません。
- Host が alternate を切り替えると、古い DMA を止めてから endpoint を開き直します。前の形式の PCM が残る ring は捨てます。切替中の `read()`／`write()` は 0 を返します。
- アプリは event や `currentFormat()` を見て、PCM の処理形式を合わせてください。

`clearFormats()` は停止中に format を全部消します。方向の選択、もう一方の stream の format、`EspUsbAudioConfig` は残ります。
空のままでは `begin()` が失敗するので、再登録してから開始します。

### 設定と開始

| 関数 | 戻り値・エラー |
| --- | --- |
| `esp_err_t configure(config)` | 稼働中：`ESP_ERR_INVALID_STATE`。外部領域の pointer と長さの片方だけ、frame 数 0 で外部領域を指定、未知の policy：`ESP_ERR_INVALID_ARG`。byte 数の計算があふれる：`ESP_ERR_INVALID_SIZE`。失敗時は前の設定を保つ |
| `configuration()` | 設定した値をそのまま返す |
| `startError()` | 直近の begin の失敗理由 |

`device.begin()` が失敗したときの `device.lastError()`：

| 値 | 原因 |
| --- | --- |
| `ESP_ERR_INVALID_ARG` | 設定どうしの不整合。stream がない、format がない、rate の集合が形状や方向で食い違う、同じ方向で channel 数が違う、外部領域が両方向で重なる |
| `ESP_ERR_INVALID_SIZE` | 容量の不足。外部 ring が必要量より短い、UAC1 の rate が 1 一覧 82 個を超える、UAC2 の rate が 5461 個を超える、endpoint や descriptor の容量が足りない、構成全体の IN endpoint が controller の送信 FIFO に収まらない |
| `ESP_ERR_NO_MEM` | メモリを確保できない |
| `ESP_ERR_INVALID_STATE` | Function がこの Device に登録されていない、など |

`configure()` の失敗は戻り値で確認します。`configure()` が `device.lastError()` を更新することは前提にしないでください。

### PCM の読み書き

| 関数 | 戻り値 |
| --- | --- |
| Playback `size_t read(data, length)` | 読んだ byte 数。frame 境界に切り下げ |
| Playback `int available()` | 未読 byte 数 |
| Capture `size_t write(data, length)` | ring に入れた byte 数。入らない分は書かない |
| Capture `size_t availableForWrite()` | 空き byte 数（frame 境界に切り下げ） |
| `size_t capacity()` | ring 容量（現在の format の frame 境界に切り下げ）。begin 前と end 後は 0 |
| `size_t packetCapacity()` | packet 用 DMA 領域の容量。begin 前と end 後は 0 |
| `bool clearBuffer()` | 未読（未送信）の PCM を消す |
| `stats()` / `resetStats()` | 統計の取得と初期化 |

- **Capture の `write()` は部分書込みです。** 古いデータを上書きしません。入らなかった分は `overrunCount`／`overrunBytes` に数えます。戻り値を必ず確認してください。
- Host が読みに来たときに Capture の PCM が足りなければ、足りない分を無音で埋め、`underrunCount`／`underrunBytes` に数えます。
- Capture は alternate 0 の間に `write()` で先に貯めておけます。最初の stream 開始でその PCM を送ります。停止や alternate の再設定では ring を消します。
- `clearBuffer()` は ring だけを消します。すでに USB へ出した packet は取り消しません。
- ring 容量を 0 にすると、Playback は受信をすべて捨てて overrun に数え、Capture は無音を送って underrun に数えます。
- `stats().transferredBytes` は USB で送受信できた byte 数です。アプリが読んだ量とは別です。
- カウンタは `uint32_t` です。長時間の集計では一周を考えてください。統計は begin で 0 に戻ります。

### Playback のあふれ方針

Playback ring が満杯のとき、新着と未読のどちらを捨てるかを `playbackOverflowPolicy` で選びます。

| 方針 | 動作 | 向く用途 |
| --- | --- | --- |
| `DropOldest`（既定） | 古い未読を押し出し、最新の完全な frame を残す | ライブ再生。遅れた後に古い音を流し続けない |
| `DropNewest` | 未読を残し、新着のうち入らない部分を捨てる | 読み出しの順序を優先する処理 |

例（文字は frame）：

| 状態 | 新着 | DropNewest | DropOldest |
| --- | --- | --- | --- |
| 容量 4、未読 A/B/C | D/E/F | A/B/C/D | C/D/E/F |
| 容量 2、空 | A/B/C | A/B | B/C |

- 破棄があった packet ごとに `overrunCount` を 1 増やし、捨てた byte 数を `overrunBytes` に足します。DropOldest では押し出した過去の PCM も数えます。
- packet 末尾の不完全な frame は、どちらの方針でも捨てます。次の packet とつなぐ隠れ buffer はありません。
- どちらの方針も、欠けのない録音は保証しません。

### 音量・ミュート

| 関数 | 説明 |
| --- | --- |
| `hasMute(direction, channel)` / `hasVolume(direction, channel)` | その control があるか |
| `getMute(mute, direction, channel)` / `setMute(mute, direction, channel)` | ミュートの取得・設定 |
| `getVolume(volume, direction, channel)` / `setVolume(volume, direction, channel)` | 音量（`int16_t`、1/256 dB 単位） |
| `getVolumeRange(range, direction, channel)` | 音量の範囲 |
| `currentSampleRate()` | 現在の sample rate |

- `direction` の既定は Playback、`channel` の既定は 0（master）です。1 以上は各 channel（1＝左、2＝右）です。
- 音量は −90 dB〜0 dB、1 dB 刻みです（値は −23040〜0、256 刻み）。範囲外や刻みに合わない値は `false` です。
- 値が変わると event を作ります。アプリからの変更でも作ります。

### event

| 関数 | 説明 |
| --- | --- |
| `bool pollEvent(event)` | 1 件取り出す。空なら `false` |
| `size_t pendingEvents()` | 未読の件数 |
| `uint32_t droppedEvents()` | queue が満杯で捨てた件数 |
| `void clearEvents()` | 未読と破棄数を消す |
| `size_t eventCapacity()` | 確保した件数。begin 前と end 後は 0 |

`EspUsbAudioEvent` の `type` は `SampleRateChanged`、`MuteChanged`、`VolumeChanged`、`StreamStateChanged` です。
`target`（`Function`／`Playback`／`Capture`）、`channel`、`alternateSetting`、`sampleRate`、`volumeDb256`、`muted`、`enabled` を持ちます。
queue が満杯なら新しい event を捨て、`droppedEvents()` を増やします。

### 呼べる文脈

| 操作 | 文脈 |
| --- | --- |
| `addFormat`、`clearFormats`、`configure` | 停止中（`device.begin()` 前か `device.end()` 後）のアプリの task |
| `read`、`write`、`pollEvent`、音量・ミュート | アプリの task。内部は lock で守る |
| Host の要求の処理と DMA | ライブラリの USB worker task。アプリの callback は呼ばない |

- 稼働中に Function を破棄すると Device を止め、USB worker と DMA が止まってから領域を解放します。
- 安全性を確かめていない組合せ：`setMute`／`setVolume` と Host の要求の同時更新、`device.end()` と `read()`／`write()` の同時実行。

## バッファとメモリ

| 領域 | 置き場所 | 確保 |
| --- | --- | --- |
| packet 用 DMA 領域（方向ごと 1 つ） | DMA 可能な内部 RAM、64 byte 境界 | begin。全 format の FS 最大 packet の大きさ |
| UAC2 の feedback 領域 | DMA 可能な内部 RAM | begin（UAC2 で Playback があるとき） |
| UAC2 の RANGE 応答（`2 + 12 × rate 数` byte） | 通常の `malloc` | begin（UAC2 で rate が 2 個以上のとき） |
| ring（方向ごと） | 自動なら通常の `malloc`。外部領域も可 | begin。または `configure()` で渡す |
| event queue | 通常の `malloc` | begin（`eventCapacity` が 1 以上のとき） |
| format の一覧（1 個 8 byte） | 通常の heap | `addFormat()` のたび |

- packet 用 DMA 領域は、HS でつながっても FS の最大 packet で確保します。begin の時点では速度が決まっていないためです。
- packet と ring は最大の形状で一つだけ確保します。alternate を足しても積み増さず、切替時の heap 確保もありません。
- ring は CPU がコピーするだけです。DMA の条件も alignment も要りません。原理上は **PSRAM を使えます**。ただし PSRAM 上の ring は実機では未確認です。
- 外部 ring の必要量は「最大の frame byte 数 × bufferFrames」です。例：ステレオ 16 bit（4 byte）と 32 bit（8 byte）を登録して 65 frame なら 520 byte です。
- 外部 ring は `device.end()` まで有効に保ち、その間アプリから直接書き換えないでください。足りないときは begin が失敗し、外部領域には書き込みません。
- 確保に失敗すると begin は `ESP_ERR_NO_MEM` で失敗し、確保済みの領域はすべて回収します。

## Host OS 側の扱い

| OS | 標準の driver | この library での確認 |
| --- | --- | --- |
| Linux | `snd-usb-audio`（UAC1／UAC2） | 未確認 |
| Windows | `usbaudio.sys`（UAC1）、`usbaudio2.sys`（UAC2） | 未確認 |
| macOS | 標準の Audio driver（UAC1／UAC2） | 未確認 |

- 動作は、ESP32 を Host にした試験（S3 同士の FS、P4 一台の loopback、P4 と P4X の HS 直結）で確認しています。PC の OS とつないだ確認はまだありません。
- Windows の UAC2 driver は、Clock Source の sample rate の RANGE 要求に正しく答えることを求めます（[Microsoft の説明](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/usb-2-0-audio-drivers)）。本ライブラリは登録した rate の一覧を返します。
- OS の音量 UI の値は event で届きます。PCM には掛かりません。必要ならアプリで gain を掛けてください。
- 同じ VID/PID で構成を変えると、OS が古い構成を覚えていることがあります（[troubleshooting](../troubleshooting.ja.md)）。

## 規格との対応と設計の理由

### UAC1 と UAC2 の違い

| 項目 | UAC1（[Audio 1.0](https://www.usb.org/sites/default/files/audio10.pdf)） | UAC2（Audio 2.0） |
| --- | --- | --- |
| Playback の同期方式（USB 2.0 §5.12.4.1） | adaptive。feedback endpoint なし | asynchronous ＋ feedback endpoint |
| Capture の同期方式 | asynchronous | asynchronous |
| rate の通知 | Type I format descriptor の rate 一覧（最大 82 個。descriptor 長が 1 byte のため） | Clock Source の CUR／RANGE 要求（最大 5461 subrange。要求長が 16 bit のため） |
| rate の変更 | endpoint への SET_CUR（Sampling Frequency Control） | Clock Source への SET_CUR |
| 音量・ミュート | Feature Unit（master と各 channel） | Feature Unit（master と各 channel） |

### feedback の形式

- USB 2.0 §5.12.4.2 に従い、FS は 10.14 形式を 3 byte、HS は 16.16 形式を 4 byte で送ります。
- 値は USB の公称 clock から計算した一定値です。外部 codec の clock のずれには追従しません。codec の clock で動かすときは、ring の水位を見てアプリ側で吸収してください。

### 設計の理由

- **容量を実行時に決める。** v2 は build flag で固定長の FIFO と単一の global instance を持っていました。v3 は Function ごとに `configure()` で容量を決め、使う方向と format の分だけ begin で確保します。通知が要らなければ queue を省けます。再ビルドせずに容量を変えられます。開始の失敗は戻り値で分かります。
- **packet 領域は FS の最大で確保する。** 速度が決まる前に確保するので、どちらの速度でも足りる大きさにしています。その代わり、format の検証も FS の上限で行います。
- **Capture は部分書込み。** 古い PCM を上書きせず、入った量を返します。データを黙って失わないためです。
- **Playback の既定は DropOldest。** ライブ再生で、読み出しが遅れた後に古い音を流し続けないためです。
- **endpoint 番号の幅。** IN と OUT は別々に番号を割り当て、反対方向なら同じ番号を共有します。後に続く Function の endpoint 番号は詰まるので、Host は descriptor からアドレスを読んでください。

### v2 からの主な変更

- `CFG_TUD_AUDIO` 系の build flag は使えません（定義すると compile error）。format は `addFormat()`、容量は `configure()` で指定します。
- Capture の `write()` は部分書込みになりました。上書きに頼っていたコードは戻り値を確認してください。
- Playback ring があふれたときの既定は `DropOldest` です。
- 一つの方向に複数の rate と複数の PCM 形状を登録できます。

未対応のもの：同じ方向での channel 数の切替、形状ごとに違う rate の集合、外部 codec の clock に合わせた feedback、callback や借用方式の PCM API。

## 関連する examples

| example | 内容 |
| --- | --- |
| [AudioSpeaker](../../examples/AudioSpeaker/) | UAC1 の Playback。PCM を読み、event と統計を表示する |
| [AudioMicrophone](../../examples/AudioMicrophone/) | UAC1 の Capture。440 Hz の正弦波を PC へ送る |
| [AudioHeadset](../../examples/AudioHeadset/) | Playback と Capture の loopback |
| [AudioPlayback](../../examples/AudioPlayback/) | UAC2。2 つの PCM 形状 × 2 つの rate と `currentFormat()` |
| [AudioSpeakerM5](../../examples/AudioSpeakerM5/) | M5Stack のスピーカーで鳴らす |
| [AudioMicrophoneM5](../../examples/AudioMicrophoneM5/) | M5Stack のマイクを PC へ送る |
| [AudioHeadsetM5](../../examples/AudioHeadsetM5/) | M5Stack のスピーカーとマイクでヘッドセット |

関連文書：[USB device 開発ガイド](../usb-device-guide.ja.md)、[troubleshooting](../troubleshooting.ja.md)、[v2 からの移行](../migrating-from-v2.ja.md)
