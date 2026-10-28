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
API変更後のS2/P4 compileは成功。製品`d876d50`を固定した全34件は**34 passed、0 skipped/error/failure/unfinished、4651.38秒**。
68 serial全てSTOPPED、STOP_FAILED/panicなし。774取得点で失敗/回復し、heapは346704→346704 byte。
開始mode/workerの実priority/core、reset開始3深度、各classのHALT、TCP3構成も確認した。
S3/S2/P4の全29 sourceの依存記録に旧vendor/SDK fallbackなし。旧Runtime名/空task symbolもない。
原本`/tmp/pytest-embedded/2026-10-28_08-59-19-825854/`、結果は`.pytest-results/native-api-all*`と
`native-api-audit.txt`、各targetのnm。API整理後の全体確認はこの結果を基準にする。

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

## examples・旧資産・CIの整理

公開APIだけでKeyboard/CdcConsole/VendorEcho/MidiEcho/CompositeCdcMidi/ReconfigureVendorを新規作成。
全6例をCore3.3.12/S3でcompile成功。`.pytest-results/v3-examples-s3-build.txt`へ記録した。
native Stream変更後に全6例×S3/S2/P4の18ビルドも成功。実機の確認範囲はS3–S3のまま。
旧examples156/旧tests347ファイルはmainと内容一致を確認して`legacy/v2/`へ保管。
旧unit/互換matrix workflowを保管し、native構造検査＋独立suite収集のCIへ変更。
34件の収集を実機なしで確認。新examplesのビルドCIは各profileを対象にする。
READMEをv3向けに書き直し、[CH32RV比較](V3_CH32RV_API_ALIGNMENT.ja.md)も追加した。

共有toolkit管理のrelease workflowは未変更で、archive exportは旧`tests/`除外のまま。
公開前にvalidation/legacyの配布範囲をtoolkit側と調整する必要がある。release/tag/pushはしていない。
古い非V3文書は履歴資料として残っており、全リンクをv3向けに修復した意味ではない。

## Printの空き容量修正

CDC/VendorのPrint::availableForWriteを実効copy空きへ合わせた。size_tのwriteAvailableは維持し、
標準int照会ではINT_MAXに飽和する。MIDIのPrint byte入力は0を維持する。追加状態/buffer/taskはない。
修正前はCDCに64/128 byteの空きがあっても0で、追加試験は1 failed（51.79秒）。両boardは停止した。
修正後のCDC/Vendor/MIDIは**3 passed、0 skipped/error/failure/unfinished、337.64秒**。
Print経由の空きは20回/10構成の照会全てで一致し、同じ型でのwire送信も通過。
MIDIのbyte write/空き制限とpacket転送も成功。6 serial全てSTOPPED、panicなし。
S2/P4 compile成功。各targetの29 sourceの依存・nmに旧vendor/SDK fallbackや未使用class stateなし。

原本`/tmp/pytest-embedded/2026-10-28_11-54-10-285361/`。
`.pytest-results/print-availability-{baseline,related}*`へXML/transcript/serial/集計を保存。
全34件の結果はこの小変更前の`d876d50`であり、Print変更後は関連3件を実行した。

## native Streamとprotocol-neutral bulk

CDCを直接Arduino Streamとして使えるようにした。VendorはPrintのまま、MIDIはどちらでもない。
共通bulkからPrintを外し、CDC/Vendorが各Arduino interfaceを実装する。旧MIDIのPrint upcastはcompile error。
通常のread/write/Print、begin/config/endは維持。新peekは既存RX領域を非消費で見て、追加cacheを持たない。

4案の最小比較後、CDCだけのStream化を採用。S3のsizeofはCDC224→232、Vendor168→168、
MIDI208→200 byte。未使用adapterの費用は0だが、使用時の別objectが必要になるため採用していない。
Flash/RAM比較と移行は[CDCガイド](V3_CDC_API.ja.md#採用のための最小比較)。
関連5件は**5 passed、465.84秒**。0 skipped/error/failure/unfinished、10 serial STOPPED/panicなし。
Streamのempty/timeout/peek/readBytes、tiny ring、Printの24照会、MIDI、774確保点、登録を確認。
heap346704→346704 byte。S2/P4と新CdcConsoleのcompile、依存/nmも成功。
原本`/tmp/pytest-embedded/2026-10-28_12-23-52-104927/`、`.pytest-results/cdc-native-stream-related*`へ保存。
全34件は次に実行する。実行中は製品/probeを固定する。

## exampleのprofile別ビルド

全6例×S3/S2/P4の18ビルドが成功し、各ELFのarchitectureとbuild.optionsのFQBNを確認した。
最初の並列実行ではArduinoのsketch-pathだけの既定cacheへ異なるarchitectureが混在し、
linkのEM:243/94不一致・同時cleanupが発生した。自分の3 build treeだけを停止し、
自分の6 sketchの生成cacheだけをmetadataで特定して回収した。
helperの出力先を`build/examples/<profile>/<sketch>/`へ分離し、同じ並列実行で18件全て成功。
実機pytestはvalidation配下の別build pathなので継続しており、製品/probeソースは変更していない。
記録は`.pytest-results/stream-examples-*-build.txt`、`stream-examples-build-audit.txt`。
初回の衝突記録は`stream-examples-*-initial-cache-collision.txt`へ分けて保存。

## 次の単位と未完了

1. native Streamのclass構造変更後の全34件を確認する。関連5件は完了。
2. 新examplesの機能範囲を必要に応じて広げる。全クラスcatalogはまだ揃えていない。
3. borrowed bulkの65535 byte上限は現在も内部uint16_tの入口/計数に由来して残る。
   次に、DMA分割を保ちながらこの16 bit制約を外せるか調べる。DWC2 recordのISO専用interval/retryと
   非ISOのDMA進捗を共有できれば、recordを増やさず計数を広げる案もある。実装・採用は未検証。
   既存API/資源管理の残る問題は実測で判断し、旧互換性だけのshimを増やさない。
4. OS互換、suspend/resume・remote wakeup、長時間、S2/P4の実機FS/HSを後段で確認する。
   S31はCore/SDKのcontroller対応を含む新規port対象で、現SDKの対応を約束しない。

任意の全EP0順序や全競合順序を検証済みとは扱わない。
初期実機はS3–S3。将来の常駐案はS3二台＋S31–HS–P4–FS–S2の計五台。
P4は一方がEspUsbDevice、他方がEspUsbHostとなる想定で、二つのDevice controller同時利用を約束しない。
