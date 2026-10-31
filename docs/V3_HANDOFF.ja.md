# v3開発の引き渡し

更新: 2026-10-31。worktree `/home/mt/dev/EspUsbDevice-v3`、branch `v3`。
v3のrelease/tagは未公開で、pushもしていない。

この文書は「いまの状態」と「次に何をするか」だけを書く。
経緯と過去の数値は[過去の記録](V3_PROGRESS_HISTORY.ja.md)、残作業は[TODO](../TODO.ja.md#優先する残作業)にある。

## いまの状態

| 項目 | 状態 |
| --- | --- |
| 実装 | 10クラス（Vendor/CDC/HID/MIDI/MSC/Audio/DFU/NCM/CCID/Video）、EP0/core、DWC2の保守forkまで本体で持つ。TinyUSBへの依存はない |
| 最新の製品変更 | `57ae028`（CCIDの既定messageBytesを自動に）。その前が`aade7e0`と`09470d6`（下記の不具合修正2件） |
| S3（FS） | 二台peerで実機試験済み。HS拡張時の回帰で`s3` 51 passed |
| P4/P4XのHS直結（480 Mbps） | 両方向で検証済み。詳細は次節 |
| P4 一台loopback（FS） | 両controllerで全10クラスの基本通信。30 caseをprobeごとに実行して成功 |
| P4XとLinux PC（HS） | DFU DETACHとMSC媒体交換をpyusbで確認（combo `p4x_pc`） |
| PSRAM | benchのP4系4台すべて32 MiB。外部RAM全域の書込み・読出しも成功。HS直結でも、PSRAM上のMSC保存先（4 MiB）とVendor ringで両方向とも成功（`42d844d`）。[PSRAM](V3_P4_VARIANTS_AND_BENCH.ja.md#psram) |
| 全comboの一括実行 | 未実施。最後に全件を一括で通したのはS3の47件（`80e8676`） |
| S2 / S31 | どちらも機材の調達中。S2はcompileのみ。S31はArduino-ESP32 4.0.0-RC1が必要 |
| CH32RVとのAPI調整 | ESP側は適用済み。残りは先方commitの確認だけ |

## HS直結の結果（2026-10-31）

旧P4（bench 2-3）とP4X（2-4）のHS口どうしを直結し、両方向で試験した。
既存のS3 peer試験39本にP4/P4Xのprofileを加え、HS comboでも実行した。

| combo | Host → Device | 結果 |
| --- | --- | --- |
| `s3`（FSの回帰） | S3 → S3 | 51 passed |
| `p4_p4x_hs` | 旧P4 → P4X | 59 passed |
| `p4x_p4_hs` | P4X → 旧P4 | 58 passed、1 failed（ISO OUT） |

失敗したISO OUTは製品を直し（`aade7e0`）、関連する試験を再実行してすべて成功した。

- HS両方向のaudio/video：各17件
- S3のaudio：9件
- P4 loopbackのaudio：4件
- audio_overflow：10回連続（修正前は旧P4 Deviceで5回中2回失敗）

HS用に足したpeer probeは次のとおり。

- `validation/peer/`：`vendor_link`、`msc_midi`、`cdc_hid`、`ccid`、`dfu`、`psram_link`
- `validation/p4_hs/`：`link`（列挙とVBUS）、`psram`（PSRAM容量）

詳細は[P4/P4Xと試験環境](V3_P4_VARIANTS_AND_BENCH.ja.md#s3-peer試験をhs直結へ広げた結果2026-10-31)の後半にある。

### HS試験で見つけて直した製品の不具合

| commit | 内容 |
| --- | --- |
| `09470d6` | OUT DMAを始めるとき、bufferのcacheを書き戻していなかった。P4Xで借用RXの65535 byteが9216 byte以降で壊れた |
| `aade7e0` | ISO OUTの偶奇（どのmicroframeで受けるか）が合わないとpacketを取りこぼした。ISRの中で偶奇を反転してarmし直すようにした |

### CCIDの既定値の変更

`EspUsbCcidConfig::messageBytes`の既定を0（自動）にした（`57ae028`）。
`begin()`のときに`max(271, Bulk-OUT MPS)`へ決まる。FSでは271、HSでは512になる。
以前の既定320はHSで必ず拒否されていた。ArduinoCore-CH32RV（`7248ca0d`）と同じ決め方にした。
[CH32RVとの調整](V3_CH32RV_API_ALIGNMENT.ja.md)を参照。

## 直近のそのほかの変更（2026-10-30）

CH32RVとのAPI調整を実装し、関連試験はすべて成功した。

| 対象 | commit | 内容 |
| --- | --- | --- |
| Video | `02c7509` | 借用frameの投稿結果を共通の型にそろえた |
| CDC | `22afc15` | `setTxTimeoutMs`と、期限付きのwrite/flush |
| Audio | `2408716` | `playbackOverflowPolicy`（既定はDropOldest） |
| DFU | `72e0743` | `onDetach`（SETUP時にbool）と`setDetachHandler` |
| MSC | `c38d03a` | 媒体の挿入を一度だけ通知し、抜去中のcommandを失敗させる |

DFUとMSCは、P4XをLinux PCへHS直結した実測をもとに仕様を決めた。
[移行](V3_MIGRATION.ja.md#ch32rvとの調整によるv3開発途中の変更2026-10-30)、
[Audio](V3_AUDIO_API.ja.md#espでの実装と実測2026-10-30)、
[DFU](V3_DFU_API.ja.md#判断の根拠となった実測)、
[MSC](V3_MSC_API.ja.md#espでの実装とwire確認2026-10-30)を参照。

## 試験bench

| 位置 | 機材 | 制御/logの経路 | 結線と使うcombo |
| --- | --- | --- | --- |
| 2-1 | 旧P4 v1.3 | CH343 UART | 同じboardのHS口とFS口をつなぐloopback。`p4`、`p4_psram` |
| 2-3 | 旧P4 v1.3 | CH340 UART | 2-4とHS口どうしを直結。`p4_p4x_hs`、`p4x_p4_hs` |
| 2-4 | P4X v3.2 | USB-Serial/JTAG | 2-3とHS口どうしを直結 |
| 3-3/3-4 | P4X v3.2（1枚） | 3-3のUSB-Serial/JTAG | 3-4のHS口をLinux PCへ直結。`p4x_pc` |
| — | S3 二台 | 各boardの書込み口 | native OTGどうしを結線。`s3` |

位置`N-M`はハブNのport M。設備の正本と注意点は[P4/P4Xと試験環境](V3_P4_VARIANTS_AND_BENCH.ja.md#接続中の実機)にある。

## 方針と入口

- Device/Function/config/beginの明示的な構成を保ち、使う機能だけに費用を持たせる。
- 旧版との互換より一貫した最終形を優先する。ただし利点のない改名や互換shimは作らない。
- CH32RVはAPI調整の相手、OEPは利用者の一例。どちらへの移植・特化もしない。
- 確認なしで進められる単位を実装・実測し、区切りごとにlocal commitする。pushしない。

主な文書：

- [開始停止・移行](V3_MIGRATION.ja.md)、[API判断](V3_API_DECISIONS.ja.md)、[所有権](V3_ARCHITECTURE.ja.md)
- [独立試験](../validation/README.ja.md)、[全体計画](V3_DEVELOPMENT_PLAN.ja.md)、[P4/P4Xと試験環境](V3_P4_VARIANTS_AND_BENCH.ja.md)
- [HID](V3_HID_API.ja.md)、[Audio](V3_AUDIO_API.ja.md)、[Remote wakeup](V3_REMOTE_WAKEUP.ja.md)
- [CH32RVとの調整](V3_CH32RV_API_ALIGNMENT.ja.md)、[DWC2の由来と保守](../third_party/tinyusb/PROVENANCE.ja.md)

## 実装のしくみ（要点）

- クラス、descriptor registry、EP0/core、runtime、controller/IRQ/cache、DWC2 forkを本体で持つ。
- 旧TinyUSB APIや、SDK内の同名headerへのfallbackはない。MIT/ST BSDの出所表示は保持している。
- 資源はinstanceの設定に応じて`begin()`で確保し、`end()`や失敗時に回収する。
- 通常の転送中にheapの確保を繰り返さない。使わないFunctionのstateは共通表から引き込まない。
- `started()`は資源を持っている状態、`ready()`はHostが構成した状態、`suspended()`はbusの休止。
- CDCはArduinoの`Stream`、Vendorは`Print`、MIDIはpacket APIを持つ。worker設定は開始configで渡す。

## 試験の運用

- 開発用の試験は`validation/`だけを使う。旧tests/examplesを合否の基準やhelperにしない。
- plugin 1.10.0（local `698bb29`）、Arduino-ESP32 3.3.12、公開EspUsbHost 2.10.1。
- `--noconftest --maxfail=1`。独自のconftest/pluginは作らない。足りない機能はplugin側へ相談する。

登録済みのcombo（`validation/pyproject.toml`）：

| combo | 構成 |
| --- | --- |
| `s3` | S3 Host → S3 Device（FS） |
| `p4` / `p4_psram` | P4一台のloopback（FS）。PSRAMあり/なしのfirmware |
| `p4_p4x_hs` / `p4x_p4_hs` | 旧P4とP4XのHS直結。前者は旧P4がHost、後者はP4XがHost |
| `p4x_pc` | P4XをLinux PCへHS直結。PC側はpyusb |

```sh
cd /home/mt/dev/EspUsbDevice-v3/validation
uv sync --locked
uv run --locked --env-file .env pytest --combo all --collect-only -q
uv run --locked --env-file .env pytest --combo all -q -rs --junitxml=.pytest-results/v3-all.xml
```

- 収集件数は試験の追加で変わるので、上の`--collect-only`で確かめる（2026-10-31時点で210件、エラー0）。
- 実機のpytestは一つずつ実行する。build/試験の間は製品とprobeを変えない。
- 同じ物理boardを使うcombo（`p4`と`p4_psram`、HS直結の2方向）は同時に動かさない。
- 共有の`/tmp/pytest-embedded`の最新directoryを根拠にしない。JUnitの時刻から該当のserial原本を探す。
- 成果物は`validation/.pytest-results/`（git対象外）に置く。

## 過去の主な全体試験

| 製品commit | 結果 | 記録 |
| --- | --- | --- |
| `80e8676`（試験修正`0144bfb`） | S3の全47件、47 passed、1917.29秒。skip/error/failure/unfinished 0 | `remote-all*` |
| `052bcbb` | S3の全44件、44 passed、3745.31秒 | `archive-all*` |
| `80e8676` | 新しい8 examples × S3/S2/P4 = 24 compile、全成功 | `final-examples-audit.json` |

それぞれの監査内容と、ほかの個別試験の記録は[過去の記録](V3_PROGRESS_HISTORY.ja.md#2026-10-30時点の引き渡し原文2026-10-31に整理)にある。

## 次にやること

詳細は[TODO](../TODO.ja.md#優先する残作業)。

1. 全comboを一括で再実行する（2026-10-31時点の収集は210件、収集エラー0）。
2. S2/S31の実機検証（機材の到着後）。
3. Arduino-ESP32 4系の互換性検証。[検証方針](V3_DEVELOPMENT_PLAN.ja.md#arduino-esp32-4系コアの検証方針2026-10-30)
4. CH32RV側のcommitを確認する。
5. PC OSとの互換性、関連Hostライブラリの未解決事項。

S3やP4の成功を、まだ試していない機種の代わりにしない。
remote wakeupの成功を、SoCのsleepやPCの電源管理の保証としない。
