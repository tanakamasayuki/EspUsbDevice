# v3開発の引き渡し

更新: 2026-10-29。作業場所は`/home/mt/dev/EspUsbDevice-v3`、branch `v3`。
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
製品`56a2c9a`を固定した全34件も**34 passed、0 skipped/error/failure/unfinished、2510.86秒**。
68 logs全てSTOPPED、STOP_FAILED/panicなし。774取得点で失敗/回復、heap346704→346704 byte。
native Streamの24照会/12構成、4 timeout、各classのHALT、reset開始3深度、TCP3構成も同じ実行で成功。
原本`/tmp/pytest-embedded/2026-10-28_12-35-43-320235/`。
結果は`.pytest-results/cdc-native-stream-all*`、集計は`cdc-native-stream-all-audit.txt`。
S2/P4 compileと全29 sourceの依存・symbol確認も成功。実行中の製品/probeは変更していない。

## exampleのprofile別ビルド

全6例×S3/S2/P4の18ビルドが成功し、各ELFのarchitectureとbuild.optionsのFQBNを確認した。
最初の並列実行ではArduinoのsketch-pathだけの既定cacheへ異なるarchitectureが混在し、
linkのEM:243/94不一致・同時cleanupが発生した。自分の3 build treeだけを停止し、
自分の6 sketchの生成cacheだけをmetadataで特定して回収した。
helperの出力先を`build/examples/<profile>/<sketch>/`へ分離し、同じ並列実行で18件全て成功。
実機pytestはvalidation配下の別build pathなので継続しており、製品/probeソースは変更していない。
記録は`.pytest-results/stream-examples-*-build.txt`、`stream-examples-build-audit.txt`。
初回の衝突記録は`stream-examples-*-initial-cache-collision.txt`へ分けて保存。

## borrowed bulkの総長を32 bitへ

private core/controllerの投稿長とDWC2のtotal_len/dma_doneをuint32_tへ広げ、65535 byte制約を撤去。
公開メソッド/返却型は維持。logical上限はmin(UINT32_MAX,PTRDIFF_MAX)、RXはMPS/64へ切り下げる。
上限queryをそのままmallocする旧利用は、明示的なapplication予算へ変更が必要。
各DMA armはcounter/uint16範囲、途中はMPS倍数、user callbackは全体に一度。
Control/ISOの16 bit投稿上限は維持。interval/retryとDMA進捗は同時に使うためunion化しない。

record16→20 byte。S3のcontroller確保量はEP0:128→128、Vendor:192→192、CDC＋HID:192→256、
sparse:256→256。追加のbuffer/task/queueや投稿時のheap確保はない。
修正前は131072 byteを確保しても65536投稿を拒否し、1 failed（56.84秒）。
修正後のbulk/Audio/Video関連5件は5 passed（275.46秒）。広いloanのreset/end・core・確保失敗の
3件も3 passed（147.55秒）。774取得点の失敗/回復、heap346704→346704 byte、6 logs STOPPED/panicなし。
さらに131008 byteの位置でcancel/reset/endを行う版が1 passed（48.66秒）、92結果を確認した。
この位置なら最大のuint16 DMA armでも完了済み進捗が65535を超え、最後のFS packetは未完となる。

S2/P4 compile成功。原本は`.pytest-results/wide-loan-{baseline,related,lifecycle,high-progress}*`に
XML/transcript/serial/集計を保存。high-progress原本は
`/tmp/pytest-embedded/2026-10-28_15-02-00-787930/`。
資源比較は`wide-loan-controller-layout.json`。移行は[Vendor API](V3_VENDOR_API.ja.md#65535-byteを跨ぐ借用転送)。
製品`f1c9f29`を固定した全34件も**34 passed、0 skipped/error/failure/unfinished、1631.13秒**。
68 logs全てSTOPPED、STOP_FAILED/panicなし。774取得点で失敗/回復、heap346704→346704 byte。
92結果、131008 byte後のcancel/reset/end、native Stream、各class HALT、実IRQ reset開始3深度、TCP3構成を確認。
S2/P4 compileと全29 sourceの依存/nm/addr2lineも成功。旧tud/usbd/dcd/tusb symbolとSDK fallbackはない。
原本`/tmp/pytest-embedded/2026-10-28_15-15-08-529975/`、結果は`.pytest-results/wide-loan-all*`、
集計は`wide-loan-all-audit.txt`。実行中の製品/probeを変更していない。mainの取り込みも維持している。

## bulk packet方針の共通化

BulkStream/MIDI/CCID/NCMに分散したSoC別MPS判定を、Deviceの既存FS/HS descriptor方針へそろえた。
内部の非virtual helperだけを追加し、object field/buffer/task/queueは増やしていない。
MIDIは従来どおりbeginでconfigureを再評価する。CCIDのmessage>=MPSと既定容量は維持。
S31の条件分岐は共通方針に含まれるが、現SDKでのcontroller port対応を意味しない。

S3のCDC/MIDI/CCID/NCMは**4 passed、0 skipped/error/failure/unfinished、410.65秒**。
8 serial全てSTOPPED、STOP_FAILED/panicなし。CDCの24値/12確認・4 timeout、
CCIDの54 HALT回復、NCMの3構成で8193 byte TCP交換を確認した。
S2/P4 compile成功。S3/S2/P4の29 source依存とnmにも旧stack/SDK fallbackはない。
既存compile probeの静的RAMは変更なし。今回の関連4件と、直前のwide loan全34件は別の実行。
原本`/tmp/pytest-embedded/2026-10-28_15-50-26-412109/`、結果は
`.pytest-results/bulk-packet-policy*`。構造検査とgit diff --checkも成功。
pluginのlocal最新`698bb29`/1.10.0のcombo guideを再確認。標準combo/allとnoconftestを維持する。

## 実busのsuspend/resume

Deviceへsuspended()を追加し、既存coreの状態を公開。object field/task/bufferの追加なし。
ready()は従来どおりHost構成を表し、休止中もtrueを維持する。開始前/metadata/end後はsuspended=false。
独立suspend_resume probeはHostのLLで実際のroot portを休止し、Deviceのeventは注入しない。
queue深さ1/16/31で計12 suspend、9 resume、9往復、30 loanの一度だけの返却を確認。
休止前/休止中の投稿を保持し、休止中cancel/endでも正しいstatus/id/pointer/長さ/canaryを確認した。

最終probeは**1 passed、0 skipped/error/failure/unfinished、43.95秒**。2 logs STOPPED/panicなし。
S2/P4 compileと3 targetの全29 source依存/nmも成功。全体収集は35件で、一括再実行の結果ではない。
原本`/tmp/pytest-embedded/2026-10-28_16-04-36-084373/`、`.pytest-results/suspend-resume*`へ保存。
Host起点のS3 FS L2休止の結果。remote wakeup、消費電力、周期HID/ISO、OS起点の試験は別に残る。

## HID idleの実bus休止

hid_report_stateを拡張し、未完periodic InputのあるHostから3回休止・再開した。
wire長8/130 byte、idle20/100 msの2 IDで、休止中の受信数固定と明示送信拒否、
resume後のGET_IDLE/GET_REPORT保持と再送（350 msで18/4件ずつ）、停止後の静止を確認。
既存の200回Feature整合性とreset後の状態確認も維持。
**1 passed、0 skipped/error/failure/unfinished、80.33秒**。2 serial STOPPED/panicなし。
製品は512edb6から変更なし。原本`/tmp/pytest-embedded/2026-10-28_16-07-53-418156/`、
`.pytest-results/hid-suspend*`。boot系や全packet位置/全状態の休止を網羅した意味ではない。

## Audio ISOの実bus休止

`audio_stream`でalternate settingとHostのISO pipeを維持したまま、root portを実際に休止・再開した。
UAC2双方向48 kHz、UAC1双方向44.1 kHz、UAC2 playback-onlyの1-frame外部ring、
UAC2 capture-onlyのringなしの4構成を確認。休止中はDeviceの転送counterが変わらず、
構成・sample rateを維持する。resume後は有効な各方向の転送量が増え、HostでPCMを照合し、
UAC2のfeedback受信とreject=0、captureのfill/無音も確認した。
新たなSET_INTERFACEやstream再開始は行わない。既存10回のrun/pauseとunderrun確認も維持。

最終版は**1 passed、0 skipped/error/failure/unfinished、69.35秒**。2 serial全てSTOPPED、panic/watchdog resetなし。
製品コードは512edb6から変更していない。初回は4構成の通信を通過した後、二度目のHost停止で失敗。
追加したprobeが停止済みcontrollerのLL registerへ触れていたため、Host.ready()でguardした。
初回失敗も隠さず`.pytest-results/audio-suspend-initial-stop-error*`へ保持している。
最終原本`/tmp/pytest-embedded/2026-10-28_16-13-58-163956/`、結果は`.pytest-results/audio-suspend*`。
全35件の一括結果ではない。S3 FSのHost起点L2休止であり、OS/HS・remote wakeup・
任意のpacket位置/clock drift・全停止競合を網羅したものではない。

## format保存領域とrate照会の容量整理

formatを両方向8個ずつ保持する固定配列を撤去し、addFormatで指定された個数分だけ保存する。
未設定の方向には配列を確保しない。追加時だけ新しい領域を取得し、失敗時は旧領域と件数を保つ。
有効なformatなら1個8 byteのpayloadを保持する（allocatorの管理費は別）。追加中は旧領域と
新領域が一時的に共存し、既存要素をcopyする。通常転送中には確保・拡張しない。
設定はend後も保持し、Function破棄時に解放する。PCM/DMA/event等の開始時資源はendで回収する。

addFormatのbool返却は維持。失敗理由はDevice.lastErrorへNoMemory/InvalidArg/InvalidSizeを設定し、
成功時はESP_OKに戻す。利用側はaddFormatの結果も確認する必要がある。
同一のsample rateを別のPCM表現で指定しても、clockのrate一覧へ重複登録しない。
rate用の8要素copyも撤去し、active中は不変なFunction内のmodelを参照する。
重複除去の探索は設定・control照会時に行い、PCM経路へ新しい処理やtaskを加えない。

S3の同じtarget compilerで測ったsizeof:

| 型 | 変更前 | 変更後 |
| --- | ---: | ---: |
| EspUsbAudioFunction | 444 | 296 |
| AudioStreamModel | 76 | 16 |
| AudioControlState | 80 | 52 |

これは本体の型サイズで、formatのheap領域とallocator管理費を含まない。
測定は`.pytest-results/audio-storage-layout-{before,after}.json`。
内部model/controlでは13 format・12 rate、設定追加14箇所の確保失敗とrollback・再試行を確認。
公開APIでも、追加失敗後のsingle-format beginが成功することを確認した。
この保存領域整理の時点では、descriptor/beginの一方向一形式制限を残していた。
formatの入替えAPI、alternate選択、range応答領域等は続けて整理する。

設定・確保失敗・Audio event/control・PCMの関連3件は**3 passed、0 skipped/error/failure/unfinished、242.85秒**。
774開始取得点の失敗/回復、heap346704→346704 byte、実ISO休止4構成も成功。
6 serial全てSTOPPED、panic/watchdog resetなし。S2/P4 compile成功。
全29 sourceの依存/nmに旧stack/SDK fallbackなし、S2/P4の未選択Audioもリンクされない。
原本`/tmp/pytest-embedded/2026-10-28_21-22-05-002801/`、結果は`.pytest-results/audio-storage*`。
これは関連3件の実行で、全35件を再実行した結果ではない。

## Audio formatの停止後再構成

両streamにclearFormatsを追加。停止中に設定領域を解放しcontrol metadataを無効化する。
方向の選択は残し、稼働中はInvalidStateで無変更。空設定のままbeginは拒否する。
[APIと検証](V3_AUDIO_API.ja.md#停止後のformat入替え)に契約と不利益を記録した。
関連4件は4 passed（200.04秒）、8 logs STOPPED/panicなし。UAC1/UAC2とも同じFunction/参照で
48→44.1→48 kHzの実PCM成功。14＋774箇所の失敗回復、heap346704→346704、S2/P4 compile・依存/nm成功。
原本`/tmp/pytest-embedded/2026-10-28_21-30-54-318752/`、`.pytest-results/audio-reconfigure*`。
初回は空設定beginのエラー期待値を誤ったprobe失敗で、記録を保存済み。現在の全体収集は36件。

## Audioの複数sample rate

同じPCM形状のrate違いをUAC1/UAC2とも受け付ける。共通clockに両方向のrate集合を合わせる。
UAC1はdescriptorの離散一覧・endpoint CUR、UAC2はClock CUR/RANGEへ同じ能力を出す。
最大packet/DMAは全登録rateから求め、UAC2の複数rate時だけRANGE領域を開始時に確保する。
PCM形状変更のalternateはまだ拒否。上限・費用は[Audio API](V3_AUDIO_API.ja.md#同じpcm形状での複数sample-rate)。

関連5件は5 passed（175.61秒）、10 logs STOPPED/panicなし。13 rateの両規格で48→44.1→48 kHz、
158 byte RANGEの10要求長、未知rate拒否、開始794＋設定14取得点の失敗回復、heap346704→346704を確認。
S2/P4 compile・全29 sourceの依存/nmも成功。原本`/tmp/pytest-embedded/2026-10-28_21-46-07-520296/`、
`.pytest-results/audio-rates*`。最適化前5件の記録も保持。全体収集37件、一括再実行の結果ではない。

## AudioのPCM alternateとISO OUT回復

4480513で、間欠ISO OUTの未完frameをDMA停止後に失敗返却し、再armできるようにした。
最初の96 byteのみで停止し、bit21が未処理だった実測と失敗ログを保持している。
PCM形状ごとのalternate、最大frame/ring/packetの予約、fence後の再activate、currentFormat照会も追加。
共有clockなので各形状と両方向に同じrate集合を要求する。方向内のchannel数は引き続き固定。
[契約と結果](V3_AUDIO_API.ja.md#pcm形状のalternate選択)、[OUT回復](V3_CORE_API.ja.md#iso-outの欠落frame回復)。

関連7件は7 passed（358.65秒）、14 logs STOPPED/panicなし。両規格の4 PCM形状、16 ISO交換、
14回の旧ring poison、境界/状態、不正alternate、816＋14取得点の回復、heap346704→346704を確認。
S2/P4 compile・全29 sourceの依存/nmも成功。原本`/tmp/pytest-embedded/2026-10-28_22-26-28-639054/`、
`.pytest-results/audio-alternates-related*`。新Host probeはSDK直接で、conftestや独自pluginはない。
現在の収集は38件。低レイヤ変更を含むため、この製品を固定した全件確認を次に行う。

## 次の単位と未完了

1. native Streamとwide loanの全34件、18 example buildsの確認は完了。
2. 新examplesの機能範囲を必要に応じて広げる。全クラスcatalogはまだ揃えていない。
3. wide loanの全体確認は完了。残る既存API/資源管理の問題は実測で判断する。
   CCIDのmessage>=MPS制約は仕様§5.1で確認済みのため維持する。
   MIDIはbeginでconfigureを再評価しており、controller選択前の自動値を固定する問題はないと確認した。
   class内に分散したSoC別MPS判定は共通化済み。P4 HSではCCIDの既定320がMPS512を満たさない点は残る。
   Audioのformat/rate固定配列は撤去済み。end後の入替えAPIも追加済み。
   PCM形状のalternate選択、frame/packet/ring境界、共有clockの完全なrate集合にも対応した。
   次は最新38件の一括確認と、外部ring境界・不完全な能力表の拒否等の不足を確認する。
   per-alternateで異なるrate集合、方向内channel数変更、borrowed PCMや外部codec clockは別の設計対象。
   旧互換性だけのshimや、根拠のない追加機能は増やさない。
4. S3 FSのHost起点suspend/resumeは確認済み。Vendor HIDの周期InputとAudio ISOの4構成も確認済み。OS互換、remote wakeup、
   長時間、S2/P4の実機FS/HSを後段で確認する。
   S31はCore/SDKのcontroller対応を含む新規port対象で、現SDKの対応を約束しない。

任意の全EP0順序や全競合順序を検証済みとは扱わない。
初期実機はS3–S3。将来の常駐案はS3二台＋S31–HS–P4–FS–S2の計五台。
P4は一方がEspUsbDevice、他方がEspUsbHostとなる想定で、二つのDevice controller同時利用を約束しない。
