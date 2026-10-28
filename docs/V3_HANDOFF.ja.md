# v3開発の引き渡し

更新: 2026-10-28。作業場所は`/home/mt/dev/EspUsbDevice-v3`、branch `v3`。
mainの文書commit `3fed50d`を取り込み済み。ローカルでまとまりごとにcommitし、pushはしていない。

## 方針と入口

Device/Function/config/beginの明示的な利用と、使う機能だけへ費用を持たせる基本方針を維持する。
旧APIの互換性より一貫した最終形を優先するが、利点のない改名はしない。
CH32RVは設計事例・API調整相手、OEPは利用者の一例。どちらへの移植・特化でもない。

最初に読むもの:

- [移行と開始・停止](V3_MIGRATION.ja.md)、[API変更の判断](V3_API_DECISIONS.ja.md)
- [独立試験](../validation/README.ja.md)、[所有権](V3_ARCHITECTURE.ja.md)
- [全体計画](V3_DEVELOPMENT_PLAN.ja.md)、[DWC2の由来と保守](../third_party/tinyusb/PROVENANCE.ja.md)

[過去の実装・実測記録](V3_PROGRESS_HISTORY.ja.md)は履歴資料。各節の暫定状態を現状と混同しない。
詳細な検証記録はvalidation README末尾と各APIガイドにある。

## 現在の実装

| 層 | 状態・主なファイル |
| --- | --- |
| Function | Vendor/CDC/HID/MIDI/MSC/Audio/DFU/NCM/CCID/Videoを独自化。instance設定、必要資源の開始時確保、停止時回収 |
| descriptor | registry、configuration/HID/BOS/MS OS、文字列/URLは必要量で確保しimmutable snapshotへ |
| core | `src/internal/EspUsbDeviceCore.cpp`。EP0、bus/configuration世代、event queue、標準要求・overflow回復 |
| runtime | `src/internal/EspUsbRuntime.{h,cpp}`。worker/job、開始停止と設定検証。旧TinyUsbRuntime名は撤去 |
| controller | 独自event/IRQ/endpoint/cache境界。旧tud/usbd/dcd APIやSDK同名headerへのfallbackなし |
| DWC2 | `src/internal/EspUsbDwc2*`の保守fork。vendor build snapshot/configを撤去、由来・MIT/BSD表示は保持 |

`python3 tools/verify_usb_backend.py`はofflineの構造検査。これだけで機能的独立性を証明したとは扱わず、
各targetのcompiler依存記録・nm/addr2lineと実機試験も合わせて確認する。
未使用Functionをdriver表から参照せず、Vendor単独のELFに他クラスstate/keymapがないことを検証している。
比較ベンチマークは必須にせず、資源回収・所有権・通信の正しさを重視する。

## 直近のAPI変更

製品commit `d876d50`。metadata/headerは3.0.0。開発版でありrelease/tagは作っていない。

- `startTinyUsb`を`EspUsbDeviceStartMode::{Usb,DescriptorsOnly}`へ置換。既定のUSB開始は維持。
- `started()`は資源所有、`ready()`はHost構成済み。DescriptorsOnlyはstarted=true/ready=falseでworkerなし。
- 空の`device.task()`を削除。旧名aliasやno-op互換層は残さない。
- `taskStackBytes`/`taskPriority`を公開。既定4096 byte/最高priorityを維持し、core/queueと同じbegin/end契約へ。
- 不正modeは設定保存前、不正worker設定はPHY取得前に拒否。DescriptorsOnlyではworker設定を使わない。

mode変更後のcontrol_core/Vendorは2 passed（432.33秒）。worker/started補強後のcontrol_coreは
1 passed（89.59秒）。不正値、workerなし、実priority/core、停止・rollbackとheap回復を確認した。
API変更後のS2/P4 compileは成功。全34件の一括実行は進行中で、結果確定後に記録する。

## 直前の全体確認

製品`929f206`を固定した標準`pytest --combo all`: **34 passed、0 skipped/error/failure/unfinished、2274.81秒**。
68 serial全てSTOPPED、STOP_FAILED/panicなし。774取得点の失敗/回復、heap前後同量。
reset開始の実IRQ、深度4/16/31、旧loan返却と新規転送、各classのHALT回復、3構成のTCPも確認。
原本`/tmp/pytest-embedded/2026-10-28_07-49-13-401967/`。
`validation/.pytest-results/reset-start-all*`と`reset-start-audit.txt`へ保存した。

この全体結果は開始/worker API変更前。最新単独試験と一括試験を区別する。
S2/P4はVendor＋CCIDのcompile probe、S3の最小nmはVendor単独。

## 試験運用

開発用試験は`validation/`のみ。旧tests/examplesは新APIの合否基準・helper・sketchとして流用しない。
plugin配布版1.10.0、local `698bb29`、Core3.3.12、Host2.10.1。
標準combo `s3: primary=s3_host,device=s3_device`と`--combo all`を使い、`--noconftest --maxfail=1`を維持する。
conftestや別helper/pluginへ独自hookを移さない。不足機能はplugin側への追加相談を優先する。

```sh
cd /home/mt/dev/EspUsbDevice-v3/validation
uv sync --locked
uv run --locked --env-file .env pytest --combo all --collect-only -q
uv run --locked --env-file .env pytest --combo all -q -rs --junitxml=.pytest-results/v3-all.xml
```

実機pytestは一つずつ。対応するbuild/test中は製品・probe sketchを変更しない。
失敗を隠すskipは追加しない。両boardの明示停止までをtestの合否に含める。

## 次の単位と未完了

1. API整理後の全34件を確定し、serial・依存記録・symbol・回収を監査して記録する。
2. 公開APIから新しい基本/複合/再構成exampleを用意し、旧examples/testsとCI・READMEを整理する。
3. 新APIの残る不自然さとprotocol/所有権の不足を実測で検討し、必要な変更だけ行う。
4. OS互換、suspend/resume・remote wakeup、長時間、S2/P4の実機FS/HSを後段で確認する。
   S31はCore/SDKのcontroller対応を含む新規port対象で、現SDKの対応を約束しない。

任意の全EP0順序や全競合順序を検証済みとは扱わない。
初期実機はS3–S3。将来の常駐案はS3二台＋S31–HS–P4–FS–S2の計五台。
P4は一方がEspUsbDevice、他方がEspUsbHostとなる想定で、二つのDevice controller同時利用を約束しない。
