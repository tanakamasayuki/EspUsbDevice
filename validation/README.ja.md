# v3 の実機試験（validation）

v3 の実機試験の入口です。仕組み、bench、確認済みの範囲は [docs/development/testing.ja.md](../docs/development/testing.ja.md) にあります。

- conftest も独自の pytest plugin も置きません。`--noconftest` で conftest を読みません。
- 標準 plugin（pytest-embedded-arduino-cli 1.10.0 以降）の combo、`dut`、`peers` だけを使います。
- `--maxfail=1` が既定です。最初の失敗の後は、次の実機 case を実行しません。
- `examples/` は読み込みません。

## 使い方

```sh
cd validation
uv sync --locked
cp .env.example .env   # 各 profile の serial port を書く
uv run --locked --env-file .env pytest --combo all --collect-only -q
uv run --locked --env-file .env pytest --combo all -q -rs --junitxml=.pytest-results/v3-all.xml
```

| したいこと | コマンド |
| --- | --- |
| 一つの combo だけ | `pytest --combo s3 -q -rs` |
| 一つの probe だけ | `pytest peer/vendor_link --combo p4_p4x_hs -q -rs` |
| 失敗しても最後まで流す | `pytest --combo all --maxfail=1000 -q -rs` |
| build だけ | `pytest --combo all --run-mode=build -q` |

（どれも `uv run --locked --env-file .env` を前に付けます。）

- 実機の pytest は一つずつ実行します。同じ board を複数の combo が使います。
- 成果物は `.pytest-results/`（git 対象外）に置きます。
- 終了値 0 だけを合格にしません。収集数、JUnit の結果、serial log の停止を確かめます。skip は未検証です。

## combo

| combo | Host → Device | link |
| --- | --- | --- |
| `s3` | S3 → S3 | FS |
| `p4` | 同じ旧 P4 の中（HS 口 ↔ FS 口） | FS |
| `p4_psram` | `p4` と同じ board（PSRAM 有効の firmware） | FS |
| `p4_p4x_hs` | 旧 P4 → P4X | HS |
| `p4x_p4_hs` | P4X → 旧 P4 | HS |
| `p4x_pc` | Linux PC → P4X | HS |

`.env` の key は `.env.example` にあります。bench の結線は [testing.ja.md の bench](../docs/development/testing.ja.md#bench) を見てください。

## ディレクトリ

| 場所 | 内容 | combo |
| --- | --- | --- |
| `s3/` | 二台 peer の probe。S3 の FS と、P4 / P4X の HS 直結の両方で動く | `s3`、`p4_p4x_hs`、`p4x_p4_hs` |
| `s3/common/` | probe が共有する header（link の bulk MPS） | — |
| `p4/` | 一台の P4 の loopback（FS、両 controller）。[p4/README.ja.md](p4/README.ja.md) | `p4`、`p4_psram` |
| `p4_hs/` | HS 直結の列挙と VBUS、P4 系 board の PSRAM 容量 | HS 直結と P4 系の各 combo |
| `peer/` | HS 直結のための peer probe | `p4_p4x_hs`、`p4x_p4_hs` |
| `p4x_pc/` | P4X と Linux PC（pyusb） | `p4x_pc` |
| `build_probes/` | build 専用。未使用の機能がリンクされないことを確かめる | pytest の対象外 |

## 各 probe の目的

### `s3/`（S3 ↔ S3、HS 直結）

| probe | 目的 |
| --- | --- |
| `vendor_roundtrip` | Vendor の往復。同じ binary で容量と送出の方式を変えて再開始できる |
| `vendor_packet_size` | Vendor の FS bulk packet 長。USB 2.0 が許す 8 / 16 / 32 / 64 は動き、それ以外は `begin()` が失敗する |
| `vendor_soak` | 連番付きの frame を流しながら、Host を何度も再列挙させても欠落・重複・破損がない |
| `transfer_limits` | 借用転送が 16 bit を超える長さでも、DMA の分割と一度の完了で通る |
| `cancel_lifecycle` | 借用転送の取消しと、reset / `end()` との競合で、buffer が一度だけ返る |
| `cancel_fence` | event queue の容量を超えて古い完了が残っても、新しい借用を返さない |
| `configuration_epoch` | 構成を選び直した後、queue に残った古い完了が新しい借用を返さない |
| `bulk_standard` | SET_INTERFACE と ENDPOINT_HALT が、対象の stream の借用だけを返し、他を続ける |
| `allocation_failure` | `begin()` の各確保点を順に失敗させ、停止状態へ戻って次の `begin()` が成功する |
| `function_registry` | 7 個以上の Function、登録の作り直しと巻き戻し、7 個の HID の report ID |
| `controller_capacity` | IN endpoint が多すぎる構成を、IRQ と worker を始める前に拒否し、設定を保つ |
| `endpoint_number` | 番号に空きのある endpoint で、TX FIFO を詰めて割り当てる |
| `descriptor_storage` | descriptor を数えてから書く。大きな descriptor、文字列、WebUSB、MS OS 2.0 |
| `control_core` | EP0、標準要求の検査、開始時の event 領域 |
| `reset_start` | 実際の USBRST 割込みで、ENUMDNE と worker の reset 処理より前に古い処理を無効にする |
| `suspend_resume` | 実際の bus の suspend / resume で、構成と借用を保つ。`suspended()` の値 |
| `remote_wakeup` | Device の wake 要求を Host が観測する。Host の許可、解除、reset の境界（[README](s3/remote_wakeup/README.ja.md)） |
| `cdc_stream` | 二つの CDC と Vendor が共通 bulk で独立に動く。line coding |
| `cdc_tx` | CDC の `print` の backpressure、TX timeout、worker の中での安全、実際の完了まで待つ `flush` |
| `midi_stream` | MIDI の packet queue、cable、callback、方向ごとの mode |
| `hid_transport` | report descriptor から決まる HID の容量、複数 packet の report、control、boot mode |
| `hid_report_state` | report ID ごとの GET_REPORT の snapshot、Feature の並行更新、idle、reset |
| `hid_standard` | 複数 packet の HID report を、反対側の endpoint の PID を変えずに回復する |
| `hid_protocol` | 旧形式の IN を DMA に残したまま protocol を切り替え、新しい report を読む（[README](s3/hid_protocol/README.ja.md)） |
| `hid_typed` | Gamepad、Consumer、System Control の送信と GET_REPORT（[README](s3/hid_typed/README.ja.md)） |
| `msc_bot` | BOT の 13 case、不正な CBW、reset の手順、SCSI sense |
| `msc_storage` | MSC の staging 容量、書込みの有無、媒体の状態と error、回復。MSC と CDC の同時通信 |
| `msc_helpers` | RAM disk と FAT12 RAM disk の helper を実際の BOT で通す（[README](s3/msc_helpers/README.ja.md)） |
| `audio_stream` | Audio の PCM buffer と ISO 転送。13 の sample rate、`end()` 後の rate 変更 |
| `audio_alternates` | PCM の形ごとの alternate を Host が選び、ISO の payload を確かめる。間を空けた ISO OUT |
| `audio_events` | 任意の event 領域、あふれ、停止中の設定、Audio の control 要求 |
| `audio_functions` | 4 つの UAC1 duplex Function が endpoint 番号を方向をまたいで共有する |
| `audio_cdc` | 周期の Audio と CDC の bulk が同じ controller と worker を共有する |
| `audio_overflow` | Playback のあふれの方針（`DropOldest` / `DropNewest`）を frame ごとに model と照合する。`test_iso_out_rate` は連続した ISO OUT の受信数を測る（合否はつけない） |
| `video_control` | UVC の control、frame の所有権、MJPEG / YUY2 の wire data。bulk の HALT |
| `video_lifecycle` | Video の format の検査と、借用 frame を返す前に DMA を止めること |
| `ncm_stream` | NCM の資源、NTB へのまとめ、Host との相互運用 |
| `ncm_ntb` | 生の NTB の検査、独立した NCM interface、alternate の回復、方向ごとの HALT |
| `ccid_stream` | CCID の容量、二つの reader、カード固定時の通知の省略。既定 `messageBytes` の値 |
| `dfu_transport` | 独立した DFU interface、開始時の資源、同期の RAM backend |

### `p4/`（一台の P4 の loopback、FS）

| probe | 目的 |
| --- | --- |
| `loopback` | 両 controller で Vendor の copy / 借用 DMA（65537 byte まで）、CDC と Keyboard の複合 |
| `classes` | RAM の MSC と 2 cable の MIDI |
| `audio` | UAC1 / UAC2 の ISO、再生と録音の同時、feedback |
| `video` | Bulk / ISO の frame と借用の返却 |
| `ccid` | 模擬カードの ATR、APDU、Escape |
| `dfu` | RAM backend の download、manifest、abort、runtime detach |
| `ncm` | 生の Ethernet frame の往復（同じ MCU 内の IP 通信は使わない） |
| `psram` | MSC の保存先と Vendor の ring を PSRAM に置く。USB の DMA は内部 RAM（`p4_psram`） |

### `p4_hs/`

| probe | 目的 |
| --- | --- |
| `link` | HS 直結の列挙をその combo の向きで確かめる。Device 側の VBUS の見え方 |
| `psram` | bench の P4 系 board の PSRAM 容量を、その board 自身で読み、全域を書いて読み戻す |

### `peer/`（HS 直結）

| probe | 目的 |
| --- | --- |
| `vendor_link` | MPS 512、packet 境界の長さでの copy echo と借用転送、再開始 |
| `msc_midi` | RAM disk の MSC BOT と 2 cable の MIDI |
| `cdc_hid` | CDC ACM の echo と line coding、boot keyboard の複合 |
| `ccid` | 模擬カード。packet 境界をまたぐ ATR、APDU、Escape、slot の状態 |
| `dfu` | RAM backend の download、manifest、abort、runtime detach |
| `psram_link` | Device の PSRAM に置いた MSC 保存先と Vendor ring を HS で往復する |

### `p4x_pc/`（Linux PC が Host）

| probe | 目的 |
| --- | --- |
| `dfu_detach` | P4X の HS controller で、DFU DETACH の受付と移行の時間を測る |
| `msc_media` | MSC の媒体交換と抜去を、PC の BOT client で wire 上で確かめる（06/28/00、02/3A/00、residue） |

PC の試験は pyusb で生の control / bulk 転送を組みます。OS の class driver が bind していれば detach します。

### `build_probes/`

repo の root で `arduino-cli compile --profile s3_device validation/build_probes/<name>` を実行し、map と `nm` を見ます。`controller_storage` だけは profile が `s2_device` と `p4_device` です。

| probe | 目的 |
| --- | --- |
| `no_library` | library なしの基準 |
| `include_only` | include だけでは費用が増えない |
| `device_only` | Device の宣言だけの費用。`begin()` 前に重い資源を確保しない |
| `vendor_copy`、`cdc_copy`、`midi_packet`、`msc_only`、`audio_only`、`video_only`、`ncm_only`、`dfu_only` | 選んだ class だけがリンクされる |
| `controller_storage` | DWC2 の開始時領域の大きさ |

## 試験を足すとき

[testing.ja.md の「新しい probe を足すとき」](../docs/development/testing.ja.md#新しい-probe-を足すとき) に従います。
