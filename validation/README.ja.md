# v3の独立したS3–S3試験

このディレクトリだけをv3開発の試験入口とする。旧testsは`legacy/v2/tests/`へ保管した。
v2資産から読み込み・収集・helperのimport・sketchの流用をせず、その互換性を新APIの制約にしない。
利用者向けexamplesと試験probeも分ける。
前回作成した独自matrix/結果gateは撤去した。ここにはconftestも独自pytest pluginもない。
`--noconftest`によりconftest読込みを無効にしている。
`--maxfail=1`で本文・USB停止を含む最初の失敗後は、次の実機caseを実行しない。

## Audio外部ring長指定後の関連6件

2026-10-29、Audio ConfigへplaybackBufferBytes/captureBufferBytesを追加。
提供長不足は開始前に拒否し、pointer/長さの片方だけの指定はconfigureで拒否する。
関連6件は**6 passed、0 skipped/failed/error/unfinished、369.22秒**、12 logs全てSTOPPED/panicなし。
不足長16回・不正な組16回の拒否、修正後の再開始、32 ISO交換、36 guard検査、
実bus休止・複数rate・停止後の再構成を確認した。816開始取得点＋14設定取得点の失敗から回復し、
heap346704→346704。Audio本体は304 byte（+8）で、新規heap取得点はない。

S2/P4 AudioPlayback compileも成功。S3/S2/P4各29 sourceの依存とAudio構成のELFで、
SDK TinyUSB fallback/旧stack、未選択NCM/Video/CCID/keymapの混入がないことを確認した。
serial原本は`/tmp/pytest-embedded/2026-10-28_23-29-42-188475/`、
JUnit/transcript/serial/監査と各nmは`.pytest-results/audio-buffer-extents*`。
全38件の再実行ではない。今回の変更前の全体基準とS2/P4 compile-onlyの範囲を区別する。

## 外部Audio ringと能力表拒否の追加確認

2026-10-29、製品は`033f2d8`から変更せずprobeを拡張した。
内蔵/外部ringで32 ISO交換・36 guard照会を確認した2件は124.49秒で成功。
rate組合せ不足・同件数の異なる集合・方向間の不一致・channel不一致の拒否と修正後の再開始、
登録順序の異なる同一集合の許可、最大frame幅での外部領域重複拒否と隣接領域の成功を確認。
計12条件×2回の1件は31.33秒、heap357280→357280で成功した。
共有probeの既存3件も88.15秒で成功。全実行でskip/error/failure/unfinishedなし、計12 logs STOPPED/panicなし。
詳細は[Audio APIの検証記録](../docs/V3_AUDIO_API.ja.md)を参照。

記録は`.pytest-results/audio-external-ring*`、`audio-capability-settings*`、`audio-external-shared-probe*`。
対象ディレクトリ名を誤った収集エラー（0件、実行なし）も`audio-external-shared-probe-selection-error.txt`へ保存した。
今回の範囲で製品修正は不要だった。以下の全38件は追加probe以前の基準結果である。

## PCM alternate対応後の全38件（最新）

2026-10-29、製品/probeを`033f2d8`に固定。標準`pytest --combo all`は
**38 passed、0 skipped/failed/error/unfinished、2544.60秒**。
76 serial全てSTOPPED、STOP_FAILED/panicなし。816取得点の失敗から全て回復し、
heapは346704→346704 byte。設定追加14箇所の失敗回復も確認した。

Audioの4 PCM alternate×UAC1/UAC2による16 ISO交換、14回の旧ring poison、
共有clock/rate一覧・長いRANGE、停止後のformat入替え、bulk/HID/Audioの実bus休止を含む。
131072 byte loanの92返却、reset開始IRQの3深度、CCID/Video/NCM HALT回復、
3構成のTCP8193 byte交換も同じ実行で成功した。

S3 Vendor単独・S2/P4 Vendor＋CCIDについて、全29 sourceのcompiler依存とnm/addr2lineを監査。
旧stack/SDK TinyUSB fallback、旧Runtime/空task、未選択NCM/Video/keymapの混入はない。
S2/P4はcompile-only。OS・他SoC/HS実機や任意の全競合順序を保証する結果ではない。
plugin 1.10.0の標準comboと`--noconftest`を使用し、独自hookは追加していない。

serial原本は`/tmp/pytest-embedded/2026-10-28_22-37-36-110392/`。
JUnit/transcript/serialコピー・監査集計と各targetのnmは`.pytest-results/audio-alternates-all*`。
実行中のコミット`380bd8b`は新AudioPlayback例と文書のみで、製品/probeに変更はない。

## 開始mode・worker API整理後の全34件

2026-10-28、製品`d876d50`を固定。標準`pytest --combo all`は**34 passed、0 skipped、
0 failed/error、0 unfinished、4651.38秒**。68 serial全てSTOPPED、STOP_FAILED/panicなし。
開始modeと実worker設定の`START_MODES 1`/`WORKER_SETTINGS 1`も同じ実行で成功。
774取得点の失敗/回復とheap 346704→346704 byte、reset開始の実IRQ3深度、
CCID/Video/NCMのHALT回復、3構成のTCP8193 byte交換を確認した。

S2/P4 compileも成功。S3 Vendor単独とS2/P4 Vendor＋CCIDの全29 sourceのcompiler依存に、
旧vendor header/SDK同名headerへのfallbackなし。独自controller/PHY/coreへ解決し、
旧TinyUsbRuntime名・空task() symbolと未使用NCM/Video/keymapはない。
`verify_usb_backend.py`、`git diff --check`も成功。
原本`/tmp/pytest-embedded/2026-10-28_08-59-19-825854/`、コピー/JUnit/transcriptは
`.pytest-results/native-api-all*`、集計は`native-api-audit.txt`、各targetのnmも保存した。
旧2.x資産は内容を保ってlegacy/v2へ移し、実行中の製品/probeソースは変更していない。
S3–S3の結果であり、OS・他SoC/HS実機や任意の全競合順序までの完了とは扱わない。

## Printの空き容量の修正前後

標準Print::availableForWriteが実効空きに関係なく0となる問題をCDCで再現した。
修正前はexpected=64/128に対しactual=0で**1 failed、51.79秒**。両boardはSTOPPED、panicなし。
修正後はCDC/Vendor/MIDIの**3 passed、0 skipped/error/failure/unfinished、337.64秒**。
CDCとVendorの20回の照会/10構成でPrintの値が実効空きと一致し、Print経由のcopy送信もwireで比較した。
停止後・copy ringなしも含む。MIDIはpacket転送を維持し、Printのbyte write/空きは0を確認。
6 serial全てSTOPPED、STOP_FAILED/panicなし。
S2/P4 compileも成功し、各targetの全29 sourceの依存・nmに旧vendor/SDK fallbackなし。
S3 Vendor単独では未使用NCM/Video/CCID/keymapもない。

修正前原本`/tmp/pytest-embedded/2026-10-28_11-52-34-461341/`、
修正後原本`/tmp/pytest-embedded/2026-10-28_11-54-10-285361/`。
`.pytest-results/print-availability-{baseline,related}*`へXML/transcript/serial/集計を保存した。
全34件はPrint変更前、変更後は関連3件の結果であり、全件の再実行とは表記しない。

## CDC native StreamとMIDIの型分離

CDCを直接Stream&へ渡せるようにし、共通bulk転送層からPrintを分離した。
VendorはPrint、MIDIはpacket APIとし、MIDIをPrint/Streamとして扱う型変換をcompileで拒否する。
新しいpeekは消費せず、別のcache領域を持たない。

CDC/Vendor/MIDI、allocation_failure、function_registryの**5 passed、0 skipped/error/failure/unfinished、465.84秒**。
1/64/97/1024 byte等のring構成、4回の空・timeout検査、繰返しpeekとStream::readBytes、
Vendorの1 byte ring、Print経由の空き24照会/12構成、実wire payloadを確認した。
774取得点の失敗/回復とheap 346704→346704 byteも成功。10 serial全てSTOPPED、panicなし。
S2/P4 compileと更新したCdcConsoleのS3 compileも成功。全29 sourceの依存・nmに旧vendor/SDK fallbackなし。
原本`/tmp/pytest-embedded/2026-10-28_12-23-52-104927/`、
XML/transcript/serial/集計は`.pytest-results/cdc-native-stream-related*`へ保存した。
変更後の全34件の結果ではなく、関連5件の結果である。

## 新examplesの全profileビルド

native Stream変更後の6例をS3/S2/P4全profileでcompileし、**18 passed、0 failed**。
生成ELFのmachine値（Xtensa94/RISC-V243）と各FQBNも確認した。S2/P4のUSB実機試験ではない。
最初の並列ビルドでは既定のsketch-path cacheがprofile間で衝突したため、helperを
`build/examples/<profile>/<sketch>/`へ分離した。製品/probeを変更せず同じ並列実行で全件成功。
記録は`.pytest-results/stream-examples-*-build.txt`と`stream-examples-build-audit.txt`。
初回失敗/中止の記録は`stream-examples-*-initial-cache-collision.txt`へ分けている。

## native Stream変更後の全34件

2026-10-28、製品`56a2c9a`を固定した標準`pytest --combo all`は**34 passed、0 skipped、
0 failed/error、0 unfinished、2510.86秒**。68 serial全てSTOPPED、STOP_FAILED/panicなし。
Streamの4 empty/timeoutとtiny ring、peek/readBytes、Printの24照会/12構成、MIDIの型分離とpacket通信、
774取得点の失敗/回復、heap346704→346704 byte、reset開始3深度と各class HALT、TCP3構成も成功。
S2/P4 compileと各targetの全29 sourceの依存・nmも確認。旧vendor/SDK fallbackはない。

原本`/tmp/pytest-embedded/2026-10-28_12-35-43-320235/`。
XML/transcript/serialは`.pytest-results/cdc-native-stream-all*`、集計は`cdc-native-stream-all-audit.txt`。
実行中の製品/probeを固定し、並行作業はexample build helperの保存先と文書の変更だけ。
`verify_usb_backend.py`、`git diff --check`も成功。OS/他SoC/HS実機・全競合順序までは未確認。

## 16 bitを跨ぐborrowed bulk

総長/進捗とprivate投稿口を32 bitへ広げ、所有する131072 byte bufferを一つのloanとして扱う。
修正前は確保済みでも65536 byte投稿を拒否（1 failed、56.84秒）。修正後はbulk/Vendor/Audio/Video
関連5件が5 passed（275.46秒）、large reset/end・core・確保失敗の3件も3 passed（147.55秒）。
774取得点の失敗/回復とheap346704→346704 byte、各scopeの全STOPPED/panicなしを確認した。

その後、prefixを131008 byteへ強化したtransfer_limitsが1 passed（48.66秒）。
完了済みDMA進捗が65535を超えた状態でcancel/reset/endを行い、その後の転送も確認する。
最大131072、short/ZLPと継続、92回の一度だけの返却、pointer/id、全byte/canaryを確認。
patternはoffsetの高位bitも含み、64 KiBのpointer折り返しを見逃さない。
S2/P4 compile成功。recordは16→20 byteで、controller確保量はEP0/Vendor/sparseが不変、
CDC＋HIDは192→256 byte。Control/ISOの投稿上限は維持する。

結果は`.pytest-results/wide-loan-{baseline,related,lifecycle,high-progress}*`。
high-progress原本`/tmp/pytest-embedded/2026-10-28_15-02-00-787930/`、資源比較は
`wide-loan-controller-layout.json`。これは関連scopeの結果で、変更後の全34件の確認は次に行う。

## wide loan変更後の全34件

製品`f1c9f29`を固定し、標準`pytest --combo all`は**34 passed、0 skipped、0 failed/error、
0 unfinished、1631.13秒**。68 serial全てSTOPPED、STOP_FAILED/panicなし。
131072 byteまでの転送と92結果、131008 byte後のcancel/reset/end、native Stream、
実IRQ reset開始3深度、CCID/Video/NCM HALTとTCP3構成が同じ実行で成功した。
774取得点の失敗/回復とheap346704→346704 byte、controller確保量128/192/256/256も確認。
S2/P4 compileと全29 sourceの依存/nm/addr2lineも成功。旧tud/usbd/dcd/tusb symbolやSDK fallbackはない。

原本`/tmp/pytest-embedded/2026-10-28_15-15-08-529975/`。
XML/transcript/serialと各nmは`.pytest-results/wide-loan-all*`、集計は`wide-loan-all-audit.txt`。
実行中の製品/probeは変更していない。`verify_usb_backend.py`と`git diff --check`も成功。
実機はS3–S3のみ。OS/他SoC/HS実機と任意の全競合順序までの確認を意味しない。

## 実際のsuspend/resumeと状態照会

`s3/suspend_resume`はSDK Hostの通常のenumeration/転送を使い、休止操作だけをprobe内の
ESP-IDF LLで行う。plugin/conftestやDevice側のevent注入ではない。Hostに未完transferがない状態で
root portを休止し、Deviceの実IRQ由来のsuspended状態と構成維持を照会する。
再開信号は30 ms、解除後の回復待ちは20 ms。20 ms以上/10 ms以上というtimingは
[IntelのUSB resume解説](https://www.intel.com/content/www/us/en/developer/articles/technical/usb-resume-optimization-spec-minimum-delays.html)
とSDK HALの契約を参照した。

queue深さ1/16/31で、休止前のloanと休止中に投稿したloanを保持し、再開後のwire payloadを比較。
休止中のcancelはCancelled、endはStoppedで一度ずつ返す。pointer/id/要求長/実長とcanaryも確認する。
各構成で10結果、計30結果。休止12回、明示resume9回、双方向交換9回、静止確認9回。
ready()はHost構成を示してtrueを維持し、DescriptorsOnlyとend後はsuspended=false。

Host probeの不要なcontrol経路を除いた最終版は**1 passed、0 skipped/error/failure/unfinished、43.95秒**。
2 serial全てSTOPPED、STOP_FAILED/panicなし。S2/P4 compileも成功、全29 source依存/nmに
SDK TinyUSB fallback・旧stack symbolなし。標準combo/allのcollect-onlyは35件となった。
原本`/tmp/pytest-embedded/2026-10-28_16-04-36-084373/`、結果は`.pytest-results/suspend-resume*`。
初回のHost側SDK型不一致のbuild errorと、整理前の成功結果も別名で保持した。
全35件の一括実行とは異なる。実機確認はS3 FSのみ、Host起点のL2休止が対象。
remote wakeup・LPM・低消費電力値・休止中のISO/周期HID・OS suspendの確認は含まない。

## HID idleの実bus休止

`hid_report_state`にもHost LLの休止・再開を追加した。8/130 byteのInput reportを
20/100 msで再送する2 IDを、Hostのperiodic Inputが動く状態で3回休止する。
休止中の受信数固定・Deviceの明示送信拒否、resume後のidle値と保存reportの維持、
再送停止後の静止を確認。3回とも再開後350 msに18/4 reportが正しいpayloadで届いた。
既存の200回Feature読出し、最後のreset後のreport状態確認も維持している。
**1 passed、0 skipped/error/failure/unfinished、80.33秒**。2 serial全てSTOPPED、panicなし。
原本`/tmp/pytest-embedded/2026-10-28_16-07-53-418156/`、`.pytest-results/hid-suspend*`へ保存。
製品コードは`512edb6`から変更していない。全35件の一括実行とは異なる。
OS省電力・remote wakeup・ISO休止や全HID状態の網羅は含めない。

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
複数formatのwire対応を完了した意味ではなく、descriptor/beginの一方向一形式制限は残る。
formatの入替えAPI、alternate選択、range応答領域等は続けて整理する。

設定・確保失敗・Audio event/control・PCMの関連3件は**3 passed、0 skipped/error/failure/unfinished、242.85秒**。
774開始取得点の失敗/回復、heap346704→346704 byte、実ISO休止4構成も成功。
6 serial全てSTOPPED、panic/watchdog resetなし。S2/P4 compile成功。
全29 sourceの依存/nmに旧stack/SDK fallbackなし、S2/P4の未選択Audioもリンクされない。
原本`/tmp/pytest-embedded/2026-10-28_21-22-05-002801/`、結果は`.pytest-results/audio-storage*`。
これは関連3件の実行で、全35件を再実行した結果ではない。

## Audio formatの停止後再構成

両streamのclearFormatsについて、反復解放・空設定拒否・稼働中拒否を故障注入probeで検証。
同じFunction/参照でUAC2/UAC1それぞれ48→44.1→48 kHzの実PCMを確認した。
allocation_failure/audio_events/audio_streamの関連4件は**4 passed、200.04秒**。
8 logs STOPPED/panicなし、設定追加14箇所・開始774箇所の失敗回復、heap346704→346704。
S2/P4 compileと依存/nmも成功。原本`/tmp/pytest-embedded/2026-10-28_21-30-54-318752/`、
`.pytest-results/audio-reconfigure*`。初回の期待値誤りも別名保存。収集は36件、一括実行とは異なる。
契約と制限は[Audio API](../docs/V3_AUDIO_API.ja.md#停止後のformat入替え)。

## Audioの複数sample rate

`audio_sample_rates`を追加し、同じPCM形状で13 rateを登録したUAC1/UAC2の48→44.1→48 kHzを確認。
clock未変更での未知rate拒否、158 byte RANGEの10要求長でのpayload/実長/guardを検査する。
関連5件は**5 passed、175.61秒**、10 logs STOPPED/panicなし。開始794取得点の失敗/回復、
設定追加14箇所、heap346704→346704、既存のISO休止と再構成も成功。S2/P4 compile・依存/nm成功。
原本`/tmp/pytest-embedded/2026-10-28_21-46-07-520296/`、`.pytest-results/audio-rates*`。
最適化前の5件も別名保存。現在の収集は37件、一括実行とは異なる。
範囲・費用・残るalternate制限は[Audio API](../docs/V3_AUDIO_API.ja.md#同じpcm形状での複数sample-rate)。

## AudioのPCM alternateと間欠ISO OUT

`audio_alternates`はSDK Hostでalternateをclaim/releaseし、4 PCM形状を各2 rateで選択する。
既存Host libraryのclaim制限を回避するためのprobe内の通常のSDK操作で、conftest/独自pluginはない。
関連7件は**7 passed、358.65秒**、14 logs STOPPED/panicなし。16 ISO交換、14旧ring poison、
状態・frame境界、不正alternate、通常PCM、休止・再構成・core、816＋14取得点の回復を確認した。
S2/P4 compile・依存/nmも成功。原本`/tmp/pytest-embedded/2026-10-28_22-26-28-639054/`、
`.pytest-results/audio-alternates-related*`。Host probeの初期誤りと、DWC2 OUT停止のbaselineも別名保存。
契約と限界は[Audio API](../docs/V3_AUDIO_API.ja.md#pcm形状のalternate選択)。収集は38件、一括結果は別に記録する。

## 実行

S3 HostとS3 DeviceのOTGを結線し、それぞれ独立した書き込み・ログportを使う。
`.env.example`を`.env`へコピーして実際のportを設定する。P4/S2/S31は現在の対象に含めない。

```sh
cd /home/mt/dev/EspUsbDevice-v3/validation
uv sync --locked
uv run --locked --env-file .env pytest --combo all --collect-only -q
uv run --locked --env-file .env pytest --combo all -q -rs --junitxml=.pytest-results/v3-all.xml
```

plugin 1.10.0の標準`--combo`を採用した。`pyproject.toml`の`arduino_combos`に
`s3: primary=s3_host,device=s3_device`を登録し、`--combo all`で結線済みの全組を実行する。
現在はこの一組だけなので34 tests（reset開始、core/control、NCM/VideoのHALT回復と6 runtime構成を含む）。S3だけを選ぶ場合は`--combo s3`を使う。
profileはHostとDeviceに一つずつ。組の列挙にconftestや適用条件マーカーは不要。
実機pytestは一つずつ実行する。build確認だけなら`--run-mode=build`を使う。

将来P4/S2/S31を追加するときは、対応sketchのprofileと結線済みcomboを追加する。
直積の`--profiles=all --peer-profiles=all`はUSBベンチの標準入口にしない。
`--combo all`ではsketchに対応profileがない組は展開されず、どの組にも適合しないtestは
`combo=none`としてskipされる。必要port未設定・同一実機の二役もbuild前にskipされる。
profile別portを使い、未結線の組を登録しない。設定済みportへの接続失敗や通信不一致は失敗として扱う。
共有P4を使うFS/HSの組も逐次実行し、各caseの明示停止とpluginのdevice lockを使う。

参照ガイド: [1.10.0の組合せと適用条件](https://github.com/tanakamasayuki/pytest-embedded-arduino-cli/blob/v1.10.0/TESTING_ADVANCED.ja.md#profile-の組合せと適用条件)。
導入時は配布版1.10.0と同tagのguideを確認した。2026-10-28にlocal pluginも再確認し、
`698bb29`（v1.10.0）、標準comboと適用条件のguideに更新済みだった。
`uv.lock`は配布版1.10.0に更新済み。plugin側の作業ツリーには変更を加えていない。

`s3/vendor_roundtrip`は新規sketchと普通のtest関数一本で、1/63/64/65/127/128/129 byteの
Vendor往復を検証する。両側のpayloadと受信長を確認し、終了時は両boardを停止する。
USB開始・停止はtest本文に見える通常のsketchコマンドで、fixture差替えや自動hookではない。
Vendorは独自クラスdriverへ移行した。短いwriteは自動送出し、容量・送出方式・RX方式を
同じbinaryで変更する。外部ring、複数instance、borrowed TX、reset/end返却も同じtestで確認する。
この公開API試験はbuild_opt.hとTinyUSB内部APIを使わない。APIは[Vendorガイド](../docs/V3_VENDOR_API.ja.md)を参照。

`s3/cancel_fence`は別caseの内部adapter故障注入試験。同じprobe sketchの専用variantで、
古い完了eventを16件残し、queue満杯でfence予約が失敗する条件を作る。
取消しcallbackで即再投稿しても旧完了が新しい領域を返さないことを確認する。
このvariantだけがprivate headerを使う。公開APIの利用例にはしない。
`s3/cancel_lifecycle`はworkerを停止した状態で、取消しと実bus reset/endが重なる3条件を各3回確認する。
`s3/transfer_limits`は公開APIだけで8128/16256 byteの分割境界前後、最大TX 65535/RX 65472 byte、
payload・境界・末尾終端・分割後の取消しを確認する。
当時の全実行はplugin 1.10.0の標準`--combo all`による`runtime-core-all.xml`。
最新結果は末尾のnative DWC2確認を参照。
**27 passed、0 skipped、0 unfinished、1743.12秒**。全caseで両board STOPPED、panicなし。
serial原本は`/tmp/pytest-embedded/2026-10-27_03-35-23-682254/`、コピーとtranscriptは
`.pytest-results/runtime-core-all-serial/`と`runtime-core-all-transcript.txt`へ保存した。
`s3/allocation_failure`は内部確保境界を試験sketchだけで差し替え、Vendor/CDC/HID/MIDI/MSC/Audio/DFU/NCM/Video/descriptor/registryの712取得点を失敗させて回復を確認する。
文字列なし/INのみHIDでの不要領域省略、WebUSB URL、HID共有state・snapshotと解析用stack、
MIDIのqueue/callback/外部ring/片方向/複数instance、MSCの内部/外部DMA bufferと複数instanceも含む。
`s3/midi_stream`はevent容量1から、外部ring、callback、片方向、2 MIDI、CDC+Keyboard併用、
manual flush、IN16/OUT1、全helperとSysEx/realtime、誤event除外、並行200 event、送信中endと停止後設定を確認する。
[MIDI API](../docs/V3_MIDI_API.ja.md)を参照。
`s3/cdc_stream`は2 CDCのruntime容量・line coding・borrowedと、CDC+Vendor+HIDの構成を検証する。
`s3/function_registry`は7/8 Function、登録変更、名前、identity、heap回復と7 HID reportの実通信を確認する。
251 Functionの長い名前とstring index 255、252 Functionの拒否もdescriptor-onlyで確認する。
`s3/descriptor_storage`はFS/HSのwriter44条件、optional領域、長さ超過/不安定なサイズ申告と、
846 byte configuration / 516 byte MS set / 366 byte HID reportの実列挙・通信を確認する。
UTF-8の日本語/絵文字、長さ境界と不正入力、254 byte文字列/255 byte URL、
入力配列の変更後も同じ応答を返すことと、不正なcontrol要求後の復帰を実USBで確認する。
`s3/hid_transport`は独自HID driverのreport容量、12サイズの実IN/OUT/Feature、
無効control要求、boot/NKRO切替とLED、稼働中の破棄を検証する。
InputのGET_REPORT、Mouseのboot/report切替、relative値をzeroへ戻したidle再送も確認する。
`s3/hid_report_state`は2 IDのInput/Output/Feature snapshot、アプリからのFeature更新、
並行更新中の200 GET_REPORT、ID別idleと停止、resetでの状態/Feature保持を確認する。
`s3/hid_protocol`はIDFの公開Host APIでINを手動投稿し、旧INを未完了のまま
boot/reportを切り替える。Keyboard/Mouse各4回の旧packet排出と新packetの完全性を確認する。
自動pollを止めるための独自pytest hookやHost library patchは使わない。
手順・範囲は[専用probe](s3/hid_protocol/README.ja.md)を参照。
`s3/msc_storage`はruntime staging容量、外部buffer、read-only、CDC併用、media/SenseとI/O拒否を確認する。
`s3/msc_bot`は手動Hostで511/512 byte sectorの各BOT13ケース、不正CBW、reset/Sense/回復、
隣接sectorのguardと独立2 MSCを確認する。HostのPID同期は公開IDF APIでinterfaceを再取得して行う。
`s3/audio_events`は任意容量/無効化、overflow、並列producerとUAC2の実Mute制御、稼働中破棄を確認する。
手順・移行・PCMの対応範囲は[Audioガイド](../docs/V3_AUDIO_API.ja.md)を参照。
通常実行は20 testsであり、各caseで同じS3–S3を順次使い、finallyで両boardを明示停止する。

## plugin側で解決する候補

以下は必要性が出た時の追加依頼候補で、依頼や実装はまだ行っていない。
conftestを別helperや別pluginへ隠して持ち込まない。

- 指定した実機試験が全件skipまたは未実行の場合に失敗終了する標準option。
  現在はpytest終了値0だけを合格にせず、今回の通常全実行が`20 passed`であることを確認する。
- 必要になればserialログの監査とartifact保存の標準機能。nodeid依存の許可リストは先に作らない。

pluginのprofile/port解決、device lock、dut/peers、compile/upload、JUnitをそのまま使う。
機材不足のskipは未検証であり、通信不一致・接続失敗をskipへ変えない。
API判断は[変更方針](../docs/V3_API_DECISIONS.ja.md)、次の作業は[引き渡し](../docs/V3_HANDOFF.ja.md)を参照。

## リンク確認

`build_probes/device_only`は未使用Vendorのリンク除外を調べる新規build専用sketchで、pytestではuploadしない。

```sh
cd build_probes/device_only
arduino-cli compile --profile s3_device --build-path build/s3_device .
```

対応するtoolchainのnmで`EspUsbDeviceVendor`/`EspUsbDeviceCdcSerial`/`EspUsbDeviceBulkStream`/`EspUsbBulkState`がないことを確認する。
他クラスの静的bufferはまだ残るため、ライブラリ全体の未使用コストが解決済みとはしない。

`build_probes/vendor_copy`は普通のcopy writeだけを使うbuild専用sketch。
同じcompile方法で、未使用の`cancelDirect*`/`cancelPump`/`dcd_edpt_abort`がリンクされないことを確認する。

`build_probes/cdc_copy`はCDCのコピー送信だけを使う。Vendor copyと相互に固有処理が
リンクされず、どちらも未使用取消しとTinyUSB CDC/Vendor/HID/MIDI/MSCを含まないことをnmで確認する。
5 probeともHID未使用なので、独自EspUsbHidFunction/EspUsbHidStateもリンクされない。

`build_probes/midi_packet`はMIDI helperの送信だけを使い、CDC/Vendor/HID固有処理と未使用取消しを含まないことを確認する。
MIDI未使用の他4 probeにはEspUsbDeviceMidiもリンクされない。

MSC追加後の通常収集は14件。関連4件はmsc-driver-related.xmlで4 passed（148.36秒）。
全14件の一括実行と5最小build/nmも成功した。
msc_storage/msc_botの手順・Hostの制約・362取得点の失敗/回復は[MSCガイド](../docs/V3_MSC_API.ja.md)を参照。

`build_probes/msc_only`はread-only MSCのみのbuild専用sketch。5最小build/nmで
未使用MSCとTinyUSB MSCの不在、MSCのみのbuildに他の独自Functionがないことを確認した。
結果は`.pytest-results/*-msc-nm.txt`へ保存した。

Audio追加後の収集は15件。直近はaudio_events/allocation_failureの関連2件が成功した（77.39秒、413取得点）。
これはeventのみの段階の記録。Audio置換後の全16件は上記runtime-audio-all.xmlを参照。


`s3/audio_stream`はUAC1/UAC2の実PCMを検証する。48 kHz/44.1 kHz stereo 16 bit、
双方向、小容量Playback、ringなしCapture、外部buffer、停止・再開、無音補完を含む。
Hostの公開FIFO設定を{64,32,64} linesへ変更し196 byte endpointを収容する。
EspUsbHost 2.10.1では存在しないOutput方向へのRunning/Feedback統計queryがnull参照となるため、
選択した方向だけのqueryを呼ぶ。Host libraryへのpatchやpytest hookは使わない。
`build_probes/audio_only`はring/eventなしCaptureの最小build。
DeviceのみとAudioのみのbuild/nmで、未使用独自Audioと旧TinyUSB Audio symbolの除外を確認する。


独自PCM driverの関連試験audio-pcm-related.xmlは**3 passed、0 skipped、0 unfinished、145.99秒**。
PCM・event制御・438取得点の失敗/回復を含み、両board STOPPED、panicなし。
serial原本は/tmp/pytest-embedded/2026-10-26_18-51-19-326855/、コピーとJUnit/transcriptは.pytest-results/audio-pcm-related*。
Audioのみ/Deviceのみの最小buildとnmも成功し、旧TinyUSB Audioと未使用独自Audioの不在を確認した。
通常収集は16件。この関連3件の後に実行した全16件の結果は下記を参照。


## Audio置換後の全体確認

2026-10-27。標準の`--combo all`で**16 passed、0 skipped、0 unfinished、823.56秒**。
全16 caseで両board STOPPED、32本のserialにpanicなし。438取得点の確保失敗/回復、
UAC1/UAC2 PCMとevent、CDC/Vendor/HID/MIDI/MSC、descriptor/registryと取消し・転送境界を含む。
serial原本は`/tmp/pytest-embedded/2026-10-26_18-54-11-801467/`、コピーとJUnit/transcriptは`validation/.pytest-results/runtime-audio-all*`。
Audioのみ/Deviceのみの最小build/nmも成功。TinyUSB treeは51 file / 15 compiled sourcesで一致し、
旧tests/examplesはmainと同一、mainはv3の祖先。残るclass/core/DCDと他SoC/HSの置換・検証は未完である。


`s3/dfu_transport`は公開IDF Hostと同期RAM backendで、複数DFUの実control転送、runtime容量、
最大65535 byte block、エラー/中断/manifestation/DETACHを検証する。保存先は試験RAMなのでOTA更新は行わない。
`build_probes/dfu_only`はRuntimeのみ。旧TinyUSB DFUと未使用DFUのリンク除外も確認する。
API・移行と未検証範囲は[DFUガイド](../docs/V3_DFU_API.ja.md)。


## S3–S3の実測結果

`dfu-runtime-related.xml`は**2 passed、0 skipped、0 unfinished、74.27秒**。
64/129/4097/65535 byte、内外buffer、独立2 Download + Runtime、GETSTATUSとblock順序、
不正要求のSTALL/CLRSTATUS、write/manifest失敗、ABORTとend時の一度だけの回収、
manifestation tolerantとreset待ち、DETACHのtimeout復帰を確認した。metadata反復のheapも回復した。
502取得点の確保失敗/回復は338520→338520 byte、両case両board STOPPED、panicなし。
serial原本は`/tmp/pytest-embedded/2026-10-26_19-25-20-261549/`、コピーとJUnit/transcriptは`validation/.pytest-results/dfu-runtime-related*`。
DFUのみ/Deviceのみの最小build/nmで、旧TinyUSB DFUと未使用独自DFUの不在を確認した。
試験は公開IDF Host APIのRAM backendで行い、conftestや独自pytest hookを使わない。
Coreのenum filterが有効なため、Hostは対象VID/PIDのfilterを明示する。
通常収集は17件。関連2件の後に行った全17件の結果は下記を参照。


## DFU置換後の全体確認

2026-10-27。標準`--combo all`は**17 passed、0 skipped、0 unfinished、841.47秒**。
全17 caseで両board STOPPED、34本のserialにpanicなし。502取得点の確保失敗/回復、
DFUの最大block/複数interface/manifestationと、従来のAudio/CDC/Vendor/HID/MIDI/MSC、
descriptor/registryと取消し・転送境界を含む。
serial原本は`/tmp/pytest-embedded/2026-10-26_19-27-27-183733/`、コピーとJUnit/transcriptは`validation/.pytest-results/runtime-dfu-all*`。
DFUのみ/Deviceのみの最小build/nmも成功。TinyUSB treeは51 file / 15 compiled sourcesで一致、
旧tests/examplesはmainと同一で、mainはv3の祖先。実OTAの適用・再起動と他SoC/HSは未検証。


## Function固有の補助文字列

2026-10-27。NCMのMAC文字列をFunction単位へ移す前提として、EspUsbDeviceClassに
auxiliaryStringCount/auxiliaryString/assignAuxiliaryStringIdsを追加した。
Function名の後へ連続したindexを割り当て、既存のexact UTF-16 snapshotへ一緒に保存する。
使用しないFunctionの追加領域はなく、既存の名前indexと251 Functionの上限を保つ。
countとtextはbeginの処理中に安定していることがwriter契約であり、宣言した文字列のnullptrは拒否する。
稼働中のUSB応答はsnapshotなので元配列の変更に依存しない。

`function-auxiliary-strings.xml`は**3 passed、0 skipped、0 unfinished、138.04秒**。
複数Functionの別領域、空の名前、index255、超過/SIZE_MAX/null/不正UTF-8からの回復、
実USB上の名前5と補助6/7・配列変更後の応答、既存registryと502取得点の失敗/回復を確認した。
両board STOPPED、panicなし。serial原本は`/tmp/pytest-embedded/2026-10-26_19-51-05-602028/`、コピーとJUnit/transcriptは
`validation/.pytest-results/function-auxiliary-strings*`。
通常収集は17件のまま。今回の関連3件は、DFU段階の全17件成功とは別の結果である。
NCMのclass/NTB pool/MAC globalはまだlegacyのままで、この変更だけで複数NCM通信に対応したとはしない。


`s3/ncm_stream`はnative Hostで容量/方向/buffer/外部pool、frame往復とまとめ送り、
NTB容量変更、DHCP/TCP、IP停止後のraw復帰を検証する。
`s3/ncm_ntb`は公開IDF Hostで不正NTB、独立2 NCM、alt/PIDと長いOUTを検証する。
`build_probes/ncm_only`はraw transportのみで、未使用IP glueのリンク除外を確認する。
APIと移行は[NCMガイド](../docs/V3_NCM_API.ja.md)。


## S3–S3の実測

`ncm-driver-related.xml`は**3 passed、0 skipped、0 unfinished、143.77秒**。
NTB容量2048/3200/8192、1/2/3 buffer、外部pool、14..1514 byte frame、ZLP、まとめ送り、
容量再交渉と不正要求後の復帰、heap回復と605取得点の確保失敗/回復を含む。
手動Hostで22種類の不正NTBを捨てて直後の正常frameを返し、block長0、末尾NDP、逆順datagram、
8128 byteのhardware armを跨いだpayload、独立2 NCMとMAC、12回のalt切替とPID初期化を確認した。
serial原本は`/tmp/pytest-embedded/2026-10-26_20-17-53-324449/`。

`ncm-ip-related.xml`は**2 passed、0 skipped、0 unfinished、154.11秒**。
3構成ともDHCPで192.168.7.2を取得しTCPの8193 byteが一致、IP停止後にraw通信へ戻れた。
2 NCMの別IP/keyでの同時作成・片方停止・再作成も成功した。
serial原本は`/tmp/pytest-embedded/2026-10-27_00-57-48-429795/`。
両実行とも各case両board STOPPED、panicなし。コピーとJUnit/transcriptは
`validation/.pytest-results/ncm-driver-related*`と`ncm-ip-related*`。
Netのみ/Deviceのみの最小build/nmも成功し、旧TinyUSB NCMと未使用NCM/IP glueの不在を確認した。
通常収集は19件。関連試験の後に行った一括試験の結果は下記を参照。


## NCM置換後の全体確認

2026-10-27。標準`--combo all`は**19 passed、0 skipped、0 unfinished、1080.50秒**。
全19 caseで両board STOPPED、38本のserialにpanicなし。NCMのraw/control/2 interface、
DHCP/TCPとIP回収、605取得点の確保失敗/回復、および既存の全USB経路を含む。
serial原本は`/tmp/pytest-embedded/2026-10-27_01-02-46-482679/`、コピーとJUnit/transcriptは`validation/.pytest-results/runtime-ncm-all*`。
NCMのみ/Deviceのみの最小build/nmも成功し、旧TinyUSB NCMと未使用NCM/IP glueの不在を確認した。
TinyUSB treeは51 file / 15 compiled sourcesで一致。旧tests/examplesはmainと同一で、mainはv3の祖先。
このNCM時点ではVideoがlegacy classだった。後述のVideo独自化後も、共通EP0/core/DCDと他SoC/HSの置換・実機検証は未完。


## Video独自化前の設定・寿命の修正

2026-10-27。Videoの稼働中破棄はDeviceを停止し、USB worker/DMAが止まってから状態を解放する。
prepareToStopで新規frameを拒否するが、稼働中フラグは終了処理まで保持して設定変更を防ぐ。
未登録/二重begin/別runtimeの稼働中beginはInvalidStateとし、startErrorで理由を返す。
raw descriptor writerも稼働中の書換えを拒否する。

format/size/fps/最大frameを候補全体で検証し、失敗時に旧値を保持する。
未知format、YUY2の奇数幅・実サイズと異なる固定frame、32 bit bit rateのoverflowを拒否する。
MJPEGの奇数サイズと1..255 fpsを受け付け、transportと関係しない120 fps制限を撤去した。
開始時にも構成を再検証する。最大4096の寸法制約とVideoの一形式という構成は現状維持。

`video-lifecycle-related.xml`は**2 passed、0 skipped、0 unfinished、344.96秒**。
metadataの反復とheap回復、257/511/512/1021 byteの実ISO frame一致と完了callback、
未登録Videoの生成/破棄後の正常通信、64 KiB送信中のFunction破棄とbuffer解放、既存descriptor試験を含む。
Hostの公開FIFO設定はRX160/NPTX16/PTX16 lines。両case両board STOPPED、panicなし。
serial原本は`/tmp/pytest-embedded/2026-10-27_01-30-16-696638/`、コピーとJUnit/transcriptは`validation/.pytest-results/video-lifecycle-related*`。
通常収集は20件。全体の直近成功はNCM時点の19件で、今回の関連2件と区別する。

Videoのclass/FIFO/global ownerはまだTinyUSBのまま。payload容量/ISO・BulkのAPI化と独自driver、
Probe/Commit検証、reset/alt/end時のframe所有権通知、callback contextの統一は次の実装対象。
今回の実転送はsynthetic MJPEG payloadのUSB転送検証であり、JPEG decode・PC/OS・他SoC/HSの検証ではない。

`s3/video_lifecycle`は新規のS3–S3 ISO probe。設定拒否と実frame、送信中破棄を検証する。
現段階の範囲は[Videoガイド](../docs/V3_VIDEO_API.ja.md)。

## Videoの独自driverとruntime構成

2026-10-27の独自Video driver最終関連試験: `video-driver-cancel.xml`、
**7 passed、0 skipped、0 unfinished、212.48秒**。
ISO/Bulk各64/65/512-byte payloadの6構成を同一binaryの実行時APIで選び、
各構成で4 frameの全byte一致、成功結果のpointer/id/byte数、64 KiB frameの取消しと古いticket拒否、
取消し直後の再投稿、送信中破棄によるStopped一度だけの返却を確認した。
65-byte構成は64-byte alignment・128-byte以上の外部DMA領域を利用する。
既存の独立descriptor probeを含む。14 serial log全てSTOPPED、panicなし。
serial原本は`/tmp/pytest-embedded/2026-10-27_01-53-40-022879/`、コピー/JUnit/transcriptは`validation/.pytest-results/video-driver-cancel*`。
VideoOnlyとDeviceOnlyの最小buildも成功し、nmでTinyUSB Video symbol不在、
DeviceOnlyには独自Video実装もリンクされないことを確認した（`*-video-nm.txt`）。
通常収集は25件。全体の直近成功はNCM時点の19件で、今回の関連7件と区別する。
Videoの確保失敗・複数Function・不正control・cancel/reset/recommit競合は後続の試験対象。
共通EP0/core/DCDと他SoC/HSは未完了であり、v3全体の完成ではない。

## Videoの確保失敗・複数Function・制御要求

2026-10-27。`video-driver-extended.xml`は**9 passed、0 skipped、0 unfinished、175.58秒**。
Video ISO/Bulkの内部/外部staging、2 Function、65535-byte stagingを含む670取得点を
一つずつ失敗させ、全点でNoMem、全資源回収、再開始とheap回復を確認した。
`video_control`は2 Functionを同時に列挙し、MJPEGの65-byte payloadとYUY2の128-byte payloadを実通信した。
26/34/48-byte Probe/Commit、GET_LEN/INFO/MIN/MAX/RES/DEF、22件の不正要求のSTALLと復帰、
不正frame/NULL/busy/別Functionのticket拒否、片方のalt/recommit/cancel中に他方のframeを保持・完了できることを確認。
62-byte frameのBulk ZLP終端、両方の送信中endとStopped一度だけの返却、外部DMA領域前後のcanaryも含む。
ProbeのGET_MIN payloadを3、GET_RESを1に直し、受け付ける範囲と一致させた。
この関連9件には既存のISO/Bulk各3構成と独立descriptor試験も含む。
18 serial log全てSTOPPED、panicなし。原本は`/tmp/pytest-embedded/2026-10-27_02-07-12-940635/`、
コピー/JUnit/transcriptは`validation/.pytest-results/video-driver-extended*`。
通常収集26件。全体の直近成功19件とは区別する。
実USB bus resetと取消し/recommitの競合全順序、PC/OS、HS/他SoCは引き続き未確認。

## Videoとlegacy classソース除去後の全体試験

2026-10-27。TinyUSBの無効化済みclass実装10本と関連headerを削除した。
残るvendor選択は27 file / 5 compiled sources。共通EP0/core/DCDとAudio/CDC/HID/MIDIのprotocol定義は残る。
検証scriptと更新scriptのmanifestも揃え、削除したNCMのlocal patchを現manifestから外した。
出所とMIT表示は維持する。既に無効だったソースの削除なのでFlash/速度の改善を主張しない。

標準`--combo all`の`runtime-video-all.xml`は**26 passed、0 skipped、0 unfinished、1703.91秒**。
670取得点の確保失敗/回復、全独自classの既存経路、複数Video制御、ISO/Bulk各3構成を一括実行した。
52 serial log全てSTOPPED、panicなし。Video deviceのnmでもlegacy class driver symbol不在を確認した。
serial原本は`/tmp/pytest-embedded/2026-10-27_02-12-17-085198/`、コピー/JUnit/transcriptは
`validation/.pytest-results/runtime-video-all*`。symbolは`video-control-cleanup-nm.txt`。
Core 3.3.12 / Host 2.10.1 / plugin 1.10.0、S3–S3のみ。conftestや独自pytest hookは使わない。

次は共通EP0/core/DCDを独自実装へ置き換える。reset/新SETUP/STALL/status、
標準要求、event順序とDMA停止、suspend/remote wakeupを分けて検証する。
他SoC/HS、PC/OS、長時間試験、出荷exampleと最終API整理は未完了。

## 共通coreの関連検証

2026-10-27。`core-controls-related.xml`は**5 passed、0 skipped、0 unfinished、323.22秒**。
control_core、712取得点の確保失敗、cancel_fence、cancel_lifecycle、video_controlを実行した。
core用private取得点はbegin時の一つで、全失敗点で回復・終了時の未回収0とheap回復を確認した。
control_coreはキュー深さ1/4/16/31で、空・短い・64-byte境界を跨ぐIN/OUT、19件の不正標準要求、
configuration解除と同値再選択、借用返却、意図的なqueue過負荷と自動再列挙後の正常通信を確認した。
各構成でVendor control 20回（IN11、OUT7、no-data2）、借用TX成功2・reset返却3。
FreeRTOSの自己削除taskのTCBはidle taskが回収するため、heap比較は各終了後10 ms待って行う。
10 serial log全てSTOPPED、panicなし。原本`/tmp/pytest-embedded/2026-10-27_03-16-42-737527/`。
コピー/JUnit/transcriptは`validation/.pytest-results/core-controls-related*`。

`core-standard-final.xml`は1件成功（81.61秒）。GET_DESCRIPTORの長さ0/1/8/18/64も追加した。
続いて未使用bootstrap/FIFO sourceを除去した`core-no-legacy-helpers.xml`も1件成功（52.57秒）。
各2 serial logで停止とpanicなしを確認し、nmで独自coreの存在と旧core state/FIFO symbol不在を確認した。
現在のvendor選択は24 file / 2 compiled sourcesで、コンパイルするTinyUSB sourceはDWC2関連だけ。
ABI/header依存を含む全撤去はまだ完了していない。

現在のcore APIと残りは[共通coreガイド](../docs/V3_CORE_API.ja.md)。標準combo allの全27件を次に実行する。

## 独自coreでの全体試験

2026-10-27、実装`ce6a444`。標準`--combo all`の`runtime-core-all.xml`は
**27 passed、0 skipped、0 unfinished、1743.12秒**。
712取得点の確保失敗/回復、独立coreの4キュー深さ、過負荷と自動再列挙、各既存クラスとVideoの全経路を一括実行した。
54 serial log全てSTOPPED、panicなし。
原本は`/tmp/pytest-embedded/2026-10-27_03-35-23-682254/`、コピー/JUnit/transcriptは
`validation/.pytest-results/runtime-core-all*`。Core 3.3.12 / Host 2.10.1 / plugin 1.10.0、S3–S3のみ。
TinyUSB manifestは24 file / 2 compiled sourcesで照合成功し、旧test/sampleは変更していない。

この成功は全標準要求・全競合順序や他SoC/HSの検証完了ではない。
転送中のSET_INTERFACEとHALT/CLEAR_FEATUREにおけるクラス別PID・所有権回復、
新SETUPと旧status/dataの到着順、物理short/overrun、suspend/remote wakeupは引き続き対象。
残るDWC2・ABI/header依存、公開APIの最終整理、出荷exampleとOS互換性も未完了。


## Vendor/CDC標準要求の回復

2026-10-27。独立bulk_standardを追加し、収集対象は28件となった。
共通bulkのSET_INTERFACE、HALT/CLEAR_FEATUREでのDMA停止・fence返却・DATA PID回復を検証した。
2 Vendor、Vendor+CDC、2 Vendor copyを同一binaryで切り替える。
借用のpointer/id/status/actual/payload/canary、一度だけの返却、他Function/方向の継続、
CDC control/data interfaceの区別、DTR/line coding維持、部分転送からの回復を確認する。
copy modeでは対象queue/ringの破棄、HALT中write/writeAvailableが0、解除後の再通信も確認する。

- `bulk-standard-related.xml`: **10 passed、0 skipped、0 unfinished、676.03秒**。
  control_core、bulk_standard、vendor_roundtrip、cdc_stream、cancel_fence、cancel_lifecycle、
  allocation_failure、descriptor_storage、audio_stream、video_control。
  原本`/tmp/pytest-embedded/2026-10-27_07-00-04-793606/`、20 serial log全てSTOPPED、panicなし。
- 最後のHALT中copy書込み拒否の補強後、`bulk-standard-final.xml`: **4 passed、0 skipped、0 unfinished、247.86秒**。
  control_core、bulk_standard、vendor_roundtrip、cdc_stream。
  原本`/tmp/pytest-embedded/2026-10-27_07-18-07-957970/`、8 serial log全てSTOPPED、panicなし。
- Vendor copy最小buildも成功。`vendor-copy-standard-nm.txt`で必須abort/fenceの存在、
  公開cancelDirectTx/Rx、未使用CDC/Audio/Video、legacy Vendor/FIFO symbolの不在を確認した。

JUnit/transcript/serialコピーとnmは`validation/.pytest-results/`（git対象外）。
Core 3.3.12 / Host 2.10.1 / plugin 1.10.0、S3–S3のみ。conftestや独自pytest hookは使わない。
最新の全体試験27件は`ce6a444`に対する結果で、この変更後の全28件一括成功を意味しない。
他クラスの同要求での回復、EP0世代境界、残存DWC2/ABI/header依存の撤去は引き続き未完了。


## CDC controlの境界確認

2026-10-27。SET_LINE_CODINGのDATA段階に受信実長7 byteの検査を加えた。
bulk_standardでは要求長0～6 byte、baud=0、stopBits=3、parity=5、dataBits=4/9を拒否し、
GET_LINE_CODINGの値とcallback回数を維持することを確認した。
`cdc-control-boundaries.xml`はbulk_standard、cdc_stream、control_coreの
**3 passed、0 skipped、0 unfinished、179.38秒**。6 serial log全てSTOPPED、panicなし。
原本`/tmp/pytest-embedded/2026-10-27_07-31-55-291424/`、コピー/JUnit/transcriptは
`validation/.pytest-results/cdc-control-boundaries*`。

wLength=7のまま実送信を0～6 byteにする実験はHost SDKが送信前に拒否したため、
物理short OUTのDevice側挙動は未検証。`cdc-short-before*`はその失敗記録であり、Deviceの不具合再現結果ではない。
受信長guard自体のshort経路を実機で通した証拠として、上の正常・不正SETUP/設定値試験を扱わない。


## Vendor controlの公開actualBytes

2026-10-27。callback引数にactualBytesを追加し、実data量の取得にprivate headerを要求しない形にした。
control_coreはSETUP=0、DATA/ACKのIN/OUT実量、0 byte返信、short返信、末尾ZLP、
129 byte応答をHost要求の5 byteへ切り詰める条件を確認する。
各キュー深さでVendor controlは21回（IN12、OUT7、no-data2）となった。
`vendor-control-actual.xml`はcontrol_core、vendor_roundtrip、bulk_standardの
**3 passed、0 skipped、0 unfinished、347.35秒**。6 serial log全てSTOPPED、panicなし。
原本`/tmp/pytest-embedded/2026-10-27_07-36-13-108957/`、コピー/JUnit/transcriptは
`validation/.pytest-results/vendor-control-actual*`。
short OUTの物理検証や、全28件の一括再試験はこの結果に含まない。
TinyUSB manifestは24 file / 2 compiled sourcesで一致し、旧tests/examplesはmainと同じ。


## HID標準要求の回復

2026-10-27。HIDのSET_INTERFACE alt 0とHALT/CLEAR_FEATUREに方向別DMA停止・fence回復を追加した。
新規hid_standardは129 byteレポートの部分IN/OUTを3回再初期化し、未完成OUTの通知なしと再通信を確認する。
HALT有無のCLEAR_FEATUREを両方向で行い、反対方向のDATA1から残り65 byteを継続する。
両方向とも部分転送後にHALT/解除した条件ではHost pipeだけを再生成し、余分なSET_INTERFACEで回復を隠さない。
公開HID APIはcopy受理のまま。Hostの初期化で受理済みの未完レポートが破棄され得ることを明記した。

`hid-standard-related.xml`はhid_standard、hid_protocol、hid_transport、hid_report_stateの
**4 passed、0 skipped、0 unfinished、636.88秒**。8 serial log全てSTOPPED、panicなし。
原本`/tmp/pytest-embedded/2026-10-27_07-45-09-725494/`、コピー/JUnit/transcriptは
`validation/.pytest-results/hid-standard-related*`。
Vendor copy最小buildのnmではHID本体・方向別drain処理のsymbol不在を確認した
（`hid-standard-vendor-copy-build.txt`、`hid-standard-vendor-copy-nm.txt`）。

plugin repoは1.10.0、commit `698bb29`で、combo/profileの最新ローカルガイドを再確認した。
新試験もconftestや独自hookなし。標準combo allで29件を収集したが、現在の全29件一括実行は未完了。
SET_INTERFACEとidleの同時条件、人工的な旧完了/queue満杯/reset/endの全競合順序、他SoC/OSは未検証。
残るDWC2/ABI/header依存と他クラスの回復検証も継続する。


## MSC interface再選択とBOT回復

2026-10-27。SET_INTERFACE alt 0をMSCのepoch付きDMA停止/fenceへ接続した。
通常のCommand待ち・CSW待ち・部分READ/WRITEを破棄して新しいCBWを受ける。
不正CBW後のBOT reset要求と、BOT reset途中の未完CLEAR_FEATUREは維持する。
WRITEは処理済み64 byteが残り、残りのsectorは変更されないことを実測した。
2つのMSCそれぞれで再初期化と容量/readbackを確認した。他instanceの同時進行コマンドの維持は未検証。

`msc-interface-verified.xml`はmsc_bot、msc_storage、allocation_failureの
**3 passed、0 skipped、0 unfinished、252.88秒**。6 serial log全てSTOPPED、panicなし。
712取得点で失敗/回復、heap before/after一致。原本は
`/tmp/pytest-embedded/2026-10-27_08-04-42-101108/`、コピー/JUnit/transcriptは
`validation/.pytest-results/msc-interface-verified*`。
既存BOT 13ケース、不正CBW 6条件、511 byte sector、media/sense、複数MSCも継続成功した。
最初の試験ではBOT reset中のSET_INTERFACEがHALTを早期解除する条件を検出し、未完の解除手順を保持した。
次の失敗はHost試験が前回の511 byte geometryを保持したためで、Host開始時に既定512 byteへ戻した。
各失敗transcriptも`.pytest-results/msc-interface-before-halt-preserve*`と`msc-interface-host-geometry*`に残す。

TinyUSB manifestは24 file / 2 compiled sourcesで一致。旧tests/examplesはmainと同じ。
全29件の一括再試験、任意Host HALTからの進行中SCSI継続、bus reset/endとの全競合順序、OS/実mediaは未完了。


## CCIDのruntime容量と独立instance

2026-10-27。CCIDの固定message配列、単一global owner/driver/bufferを撤去した。
EspUsbCcidConfigでmessageBytes・removable・nameを設定し、Functionごとのbegin時資源へ移した。
返信DMAをcallbackの出力先と共有し、IN/ZLP完了まで次のOUTを投稿しない。
固定カードは開始前の挿入を要求し、通知endpoint/bufferと稼働中の着脱を省く。
API・移行と残課題は[CCIDガイド](../docs/V3_CCID_API.ja.md)を参照。

新規ccid_streamはSDK Hostで2つのCCIDを同時claimし、各descriptor容量と通知endpoint有無を読む。
同じbinaryで64/271/320/1024 byteを基準に、2番目は+64 byteの独立容量へ切り替える。
ATR、APDU、Escape、slot通知、Abort sequence不一致、不正slot、容量超過、32-bit長さfieldの異常値、
未読返信のSET_INTERFACE中断を検証した。metadataでは64/271/320/1024/65535 byteと反復開始終了のheap一致も確認した。

- 初回SDK試験`ccid-runtime-sdk.xml`: **1 passed、0 skipped、0 unfinished、234.90秒**。
  原本`/tmp/pytest-embedded/2026-10-27_08-21-27-507168/`、2 serial log全てSTOPPED、panicなし。
- `ccid-runtime-final.xml`: ccid_stream、allocation_failure、control_core、cdc_streamの
  **4 passed、0 skipped、0 unfinished、527.51秒**。
  774取得点で失敗/回復、heap before/after一致。原本`/tmp/pytest-embedded/2026-10-27_08-29-47-559553/`、
  8 serial log全てSTOPPED、panicなし。
- その後、SET_INTERFACEで古いAbort待ちも取り消す処理を加えた。
  `ccid-runtime-boundaries-final.xml`: **1 passed、0 skipped、0 unfinished、80.51秒**。
  各容量いっぱいのEscape、末尾ZLP、T=1パラメータ、PowerOff、Abort待ちからの再初期化も成功。
  原本`/tmp/pytest-embedded/2026-10-27_08-41-40-156434/`、2 serial log全てSTOPPED、panicなし。

JUnit/transcript/serialコピーは`validation/.pytest-results/ccid-runtime-*`。
追加試験で容量超過Escapeへの応答typeがSlotStatusになる旧処理を検出し、要求別の応答typeへ統一した。
最終sourceのVendor copy最小buildでは未使用CCID本体/state/bufferのsymbol不在を確認した。
記録は`ccid-runtime-vendor-copy-build.txt`と`ccid-runtime-vendor-copy-nm.txt`。
試験入口は標準combo、conftest/独自hookなし。現在の収集は30件で、全30件の一括再試験は未完了。
HS/他SoC、PC/SC・OS・実カード、任意HALTと進行中command、全競合順序は未検証として残す。


## DWC2 runtime storageでの全体試験

2026-10-27。標準`--combo all`の`dwc2-runtime-all-final.xml`は
**30 passed、0 skipped、0 unfinished、1952.20秒**。
774取得点の確保失敗/回復、Audio/HID/MIDI/MSC/NCM/CCID/DFU/Video、取消し・reset/end、
大きいDMA転送とEP0を一括検証した。60 serial log全てSTOPPED、panicなし。
原本`/tmp/pytest-embedded/2026-10-27_08-55-42-330489/`、コピー/JUnit/transcriptは
`validation/.pytest-results/dwc2-runtime-all-final*`。

DWC2の転送配列/controller state/SETUP DMAを独自coreの開始時領域へ移し、
全alternateのdescriptor maskに現れる方向だけrecordを確保する。別のheap取得点は増やしていない。
Functionなしはbackend起動なし/0 byte、EP0だけのDFU Runtimeは2 record/128 byte、
Vendorは4 record/192 byte、着脱可能な2 CCIDは疎なOUTを含む8 record/256 byteを実測した。
終了後の確保0と反復時heap一致も確認した。

新規`build_probes/controller_storage`はS2/P4 HSでcompile成功。
S3/S2/P4のnmで旧xfer_status、_dcd_data、_dcd_usbbufの不在とruntime経路の存在を確認した。
これらはS3以外の実機確認ではない。Core 3.3.12のboards.txtにはS31定義がなく、S31 buildは未実施。
記録は`.pytest-results/dwc2-storage-{s3,s2,p4}-nm.txt`とS2/P4 build transcript。

manifestは24 file / 2 compiled sourcesで一致し、updater dry-runも差分0。旧tests/examplesはmainと同じ。
初回の一括実行は「Functionなしでもbackendが起動する」という追加試験の誤った前提を見つけて停止した。
修正版ではFunctionなし=0、DFU Runtime=EP0のみと区別した。停止記録は`dwc2-runtime-interrupted-transcript.txt`。
DWC2のregister/FIFO処理と内部ABI/headerはまだvendor依存であり、全撤去完了ではない。
P4の初回SETUP DMA前のcache同期、他SoC/HS実機、全EP0世代境界・各classのHALT競合などは継続する。


## DWC2初回cache引渡しの補強

全30件成功の実装`105d998`の後、SETUP lineの初期化によるdirty cacheを初回DMA前に同期する処理を加えた。
clean/invalidateに成功するまでruntime pointerを公開せず、失敗時はESP_FAILでqueue/領域を返す。
S3 probeは既存no-op backend hookを置換し、clean失敗とinvalidate失敗、未公開/64-byte alignment/長さ、
続く再開始と実通信を確認した。製品用のtest flagやconftestは追加していない。

`dwc2-cache-start.xml`はcontrol_coreとallocation_failureの
**2 passed、0 skipped、0 unfinished、93.10秒**。774取得点の失敗/回復とheap一致も継続成功。
4 serial log全てSTOPPED、panicなし。原本は
`/tmp/pytest-embedded/2026-10-27_13-28-12-564789/`、コピー/JUnit/transcriptは
`validation/.pytest-results/dwc2-cache-start*`。
最終sourceのS2/P4 HS probeもcompile成功し、nmで旧DWC2 static stateの不在を確認した。
記録は`dwc2-cache-{s2,p4}-build.txt`と`dwc2-cache-{s2,p4}-nm.txt`。

この追加後に全30件を再実行したわけではない。全体試験は`105d998`、追加後の関連確認は上の2件と区別する。
P4/HSの実cache coherency、他SoC実機、残るvendor DWC2 register/FIFOとABI/headerの撤去は未完了。


## 再選択した構成への古い完了event混入

2026-10-27。新規configuration_epochで、実HostのSET_CONFIGURATION 1をqueueへ入れた後に
新規借用を投稿するdeferred job、旧TX/RX完了eventを並べ、workerを再開した。
修正前はHost data転送前に`old=2 success=2 tx=0 rx=0 bad=63`となり、
新規RXの未受信buffer内容まで成功として通知した。
失敗時の試験後片付けではDeviceを先に切断したためHost SDKのhub abortも発生した。
workerの解除→Host停止→Device停止へ試験側の順序を修正し、製品側は構成世代のfilterを追加した。
失敗時の原本コピー/結果は`configuration-epoch-before*`として残した。

構成世代はclass全体のreset時に進め、古いnon-control完了をendpoint flagsの変更前に捨てる。
EP0のSETUP/bus世代、deferred fenceの後片付けは既存契約を維持する。
queue深さ4/16/31で各3回、新規借用が実dataまで保持され、旧借用だけBusResetで返ることを確認した。

`configuration-epoch-related.xml`はconfiguration_epoch、control_core、cancel_fence、
cancel_lifecycle、allocation_failureの**5 passed、0 skipped、0 unfinished、204.34秒**。
774取得点の失敗/回復を含む。10 serial log全てSTOPPED、panicなし。
原本`/tmp/pytest-embedded/2026-10-27_13-42-09-573721/`、コピー/JUnit/transcriptは
`validation/.pytest-results/configuration-epoch-related*`。
S2/P4 HSのcompile-only probeも成功。記録は`configuration-epoch-{s2,p4}-build.txt`。

収集対象は31件となったが、全体試験の最新は構成世代追加前`105d998`の30件で、全31件一括成功ではない。
今回の識別対象はqueue投入済みの旧完了であり、遅れて実行されるDCD ISRやlatched IRQ、
armごとのtoken、全EP0順序、他SoC/HS実機の検証を完了した扱いにはしない。

## DWC2停止・IRQ同期の全体確認

2026-10-27。構成世代filterだけでは防げない実latched XFRCをS3–S3で再現した。
修正前は旧INの完了後、IRQを保留して再構成・新規armすると、新規TXが実data前にSuccessになった。
close_allのDMA停止待ち・残存割り込み消去と、endpoint処理のhardware lock同期を追加した。
queue深さ4/16/31で、queue内の旧完了と実latched完了を各2回、計12回の再構成で確認する。
単独確認は**1 passed、28.57秒**。続く標準`pytest --combo all`は
**31 passed、0 skipped、0 failed/error、0 unfinished、1071.24秒**。
62 serial log全てSTOPPED、panicなし。原本は
`/tmp/pytest-embedded/2026-10-27_14-05-33-983106/`、
JUnit/transcript/serialコピーは`validation/.pytest-results/dwc2-quiesce-all*`。
S2/P4 HSのcompile-only probeも成功し、`dwc2-quiesce-{s2,p4}-build.txt`へ保存した。
vendor検証は24 files/2 compiled sources、updater dry runは差分0。
旧tests/examplesはmainとの差分なし。他SoC/HS実機、hardware故障時の停止timeout、
全EP0順序とvendor ABI/header/DWC2の完全撤去は未完了。

2026-10-27にplugin手元repoの1.10.0 CHANGELOGと`TESTING_ADVANCED.ja.md`の
「結線で決まる組は --combo で列挙する」を再確認した。
現在の`arduino_combos`によるS3一組と`--combo all`、`--noconftest`を維持する。
将来のP4/S2/S31は結線ごとの組を追加し、存在しない直積を作らない。
設定済みportの故障やcompile/upload失敗をskipへ置き換えない。
`arduino_reset_dirs`はsketchが固定outputへ書き出す場合の標準機能で、現在のUSB probeには不要。

## class登録表独自化後の全体確認

2026-10-27。native class登録を独自`UsbClassDriver`とC++ getterへ移した。
unused name/xfer_isr枠を削除し、native classのendpoint宣言を独自headerへ限定した。
標準`pytest --combo all`は**31 passed、0 skipped、0 failed/error、0 unfinished、1368.18秒**。
774取得点の失敗/回復を含み、62 serial log全てSTOPPED、panicなし。
原本`/tmp/pytest-embedded/2026-10-27_14-29-11-226117/`、
JUnit/transcript/serialコピーは`validation/.pytest-results/class-dispatch-all*`。
S2/P4 HSのcompile-onlyも成功。`class-dispatch-{s2,p4}-build.txt`とnm結果を保存した。
S3 Vendor-onlyとS2/P4 ELFで独自getterがあり、旧class table ABIのsymbolがないことを確認。
S3 Vendor-onlyではHID/MSC/Audio/Video/NCM/CCID/DFU stateのsymbolもない。
記録は`class-dispatch-s3-vendor-nm.txt`。旧tests/examplesはmainとの差分なし。
残るwire/protocol header、endpoint/control C symbol、core/DWC2のprivate headerと
DWC2実装の完全置換、各classのHALT競合・全EP0順序・他SoC/HS実機は次の作業対象。

## controller境界移行後の全体確認

2026-10-28。controller event/IRQ/転送/cache hookと型付き設定を独自境界へ移し、
vendor DCD/private、FIFO、OS抽象、umbrellaの7 headersと無効slave/FIFO経路を撤去した。
初回はcoreがFreeRTOS宣言とcontrol callback型を旧umbrella経由で得ていたためcompile error。
直接includeと独自driver型の参照へ修正し、初回を実機の合格記録には含めない。
初回失敗logは`controller-boundary-*-initial-build-failed.txt`等へ分けて保存した。

修正版の標準`pytest --combo all`は**31 passed、0 skipped、0 failed/error、0 unfinished、1402.90秒**。
774確保点全てで失敗と回復が成功し、試験前後のheapは同量。
62 serial log全てSTOPPED、panicなし。DWC2 storageはEP0のみ128、Vendor192、sparse256 byteで、
今回のpointer削除はこれらの64byte丸め後の値を変えない。
原本`/tmp/pytest-embedded/2026-10-27_14-58-51-720359/`、
JUnit/transcript/serialコピーは`validation/.pytest-results/controller-boundary-all*`。
S2/P4 HSはcompile-only成功。S3 Vendor/S2/P4 ELFで独自controllerを確認し、旧DCD/FIFO入口は不在。
P4のcache clean/invalidateはstrong実装、S2はweakのno-opを維持する。
S3 Vendor-onlyで未使用class stateは不在。各targetのライブラリ28ソースの依存記録にも、
削除したvendor headerやSDK内の同名headerへの暗黙fallbackはない。
Arduino SDK自身の別ソースが持つTinyUSB依存と、古いbuild directoryの残存artifactはこの判定に含めない。
記録は`controller-boundary-{s2,p4}-build.txt`、各nmと`controller-boundary-audit.txt`。

vendor検証17 files/2 sources、updater dry run差分0。旧tests/examplesはmainとの差分なし。
残るdevice向けheader、wire/protocol type、DWC2 register/port実装、全EP0順序・class HALT競合、
S2/P4/S31とHSの実機検証は未完了。APIの最終整理とv3 sample整備も引き続き必要。

## native busとCDC/MIDI記述子移行後の全体確認

2026-10-28。標準`pytest --combo all`は**31 passed、0 skipped、0 failed/error、0 unfinished、1502.57秒**。
774確保点の失敗/回復が成功し、heapは試験前後で同量。62 serial log全てSTOPPED、panicなし。
CDCの列挙・制御・通信、MIDIの対称1/16 cableとIN16/OUT1非対称構成を含む。
原本`/tmp/pytest-embedded/2026-10-27_15-28-37-180106/`、JUnit/transcript/serialコピーは`validation/.pytest-results/native-bus-all*`。
S2/P4 HSのcompile-onlyも成功し、`native-bus-{s2,p4}-build.txt`へ記録した。
S3 Vendor/S2/P4 ELFに旧`tud_*`/`usbd_*`/`dcd_*` symbolはなく、独自bus/endpointが存在する。
S3 Vendor-onlyでは未使用class stateも不在。各targetのライブラリ28ソースのcompiler依存記録に、
削除したdevice/Audio/CDC/MIDI headerやSDK同名headerへのfallbackはない。
各nmと`native-bus-audit.txt`を保存した。vendor検証13 files/2 sources、updater dry run差分0。
旧tests/examplesはmainとの差分なし。
wire type・HID table・DWC2とcapability定義、HALT競合・全EP0順序・他SoC/HS実機、
最終API/sampleの整理は未完了。逆向きIN1/OUT16の追加試験はこの全体結果には含めない。

## MIDI逆向き非対称cableの追加確認

2026-10-28。`c67fe1c`の製品実装を変更せず、MIDI probeへIN1/OUT16を追加した。
Hostが方向別cable数を読み取り、全16 OUT cableへ計64 eventを送りDeviceで全byteを確認する。
OUTのcable 1～15はINには存在しないため、そのままechoする期待にはしない。
続いて唯一のIN cable 0へhelper/SysEx等の15 eventを送り、Hostで全byteを比較した。
拡張したMIDI probeは**1 passed、0 skipped、0 unfinished、43.28秒**。
既存の対称1/16、IN16/OUT1と複合/取消しを含む11 runtime modeも同じ実行で通過した。
2 serial logともSTOPPED、panicなし。原本`/tmp/pytest-embedded/2026-10-27_15-54-28-307048/`。
JUnit/transcript/serialコピーは`validation/.pytest-results/native-midi-asym*`。
全体31件の一括成功は追加前の`c67fe1c`であり、追加後はMIDIのみ再実行した。
製品コードは全体試験時と同一。S2/P4の追加実機確認を行った意味ではない。

## native USB型とVBUS設定の全体確認

2026-10-28。最終版の標準`pytest --combo all`は**31 passed、0 skipped、0 failed/error、0 unfinished、1479.82秒**。
MIDIの追加IN1/OUT16を含む11 modeも同じ一括実行で成功した。
774確保点の失敗/回復が成功し、heapは前後で同量。62 serial log全てSTOPPED、panicなし。
S3のVBUS policyはfalse→true→falseでB-valid overrideを実registerから確認し、その後の通常通信も成功。
単独のcontrol_coreも1 passed（48.04秒）。検出回路を使った実ケーブル抜去の試験は行っていない。
原本`/tmp/pytest-embedded/2026-10-27_16-05-17-748180/`、JUnit/transcript/serialコピーは`validation/.pytest-results/native-wire-all*`。
S2/P4 HSはcompile-only成功。nmとcompiler依存記録を`native-wire-{s2,p4}-*`へ保存した。
S3 Vendor-onlyにも旧wire/stack symbol、未使用class state/keymapはない。
S3/S2/P4のnative C++ 26ソースはvendor header/configやSDK同名headerへの依存なし。
DWC2のC 2ソースだけはregister/common helperに依存しており、完全撤去とは区別する。
記録は`native-wire-audit.txt`と各nm。vendor検証12 files/2 sources、updater dry run差分0。
旧tests/examplesはmainとの差分なし。

初回のprobe buildではvolatile registerのコピーと公開結果型との名前衝突を修正した。
公開EspUsbTransferResultは維持し、内部結果enumをEspUsbControllerResultへ分離してから実機試験した。
初回build失敗は`native-wire-vbus-probe-build-failed.txt`、`native-wire-type-name-build-failed.txt`へ保存し、
実機の不合格/合格結果に数えていない。残るDWC2/common/capability、HALT競合・全EP0順序、
他SoC/HS実機と最終API/sampleは継続対象。

## ESP platform移行後の全体確認

2026-10-28。標準`pytest --combo all`は**31 passed、0 skipped、0 failed/error、0 unfinished、1123.26秒**。
単独control_coreも1 passed（39.39秒）。最終一括実行にIRQ_RECOVERY 1とVBUS_POLICY 1を確認した。
IRQ取得失敗を2回繰り返し、beginはESP_FAIL、runtime storage/PHY/workerを回収して再開始できた。
再開始後は取得失敗を強制したままmask/unmaskを3回繰り返し、IRQ登録関数の呼出しが増えないことを確認した。
metadata反復のheapは前後同量。774取得点の失敗/回復も成功。
実latched IRQを含む構成再選択と各class通信も成功し、62 serial log全てSTOPPED、panicなし。
原本`/tmp/pytest-embedded/2026-10-27_18-44-28-712070/`、JUnit/transcript/serialコピーは`validation/.pytest-results/dwc2-platform-all*`。
S2/P4 HSのcompile-onlyも成功。全29 library sourceのcompiler依存記録に、削除した汎用TinyUSB
header/configやSDK同名headerへのfallbackはない。S3 Vendor-onlyに未使用class state/keymap/旧IRQ配列はない。
S2のcacheはweak no-op、P4はstrong msyncを維持する。記録は各targetの`dwc2-platform-*-build.txt`、nm、
`dwc2-platform-audit.txt`。vendor検証4 files/2 sources、updater dry run差分0。

最終のlegacy flag guardは旧ifdefのDFU/MSC容量指定も含めて維持し、S3 compilerのpreprocessで
flagなしが成功、55旧flagの0指定も全て明示拒否することを確認した。通常ビルドの生成codeは変わらない。
結果は`dwc2-platform-legacy-flags.txt`。旧tests/examplesはmainとの差分なし。
DWC2のregister/PHY/endpointアルゴリズムは残るvendor 4 fileに依存している。
mask/unmask SDK自身の故障注入、全EP0順序・class HALT競合、他SoC/HS実機、最終API/sampleは未完了。


## native DWC2整理後の全体確認

2026-10-28、実装`cd6dc24`。標準`pytest --combo all`は**31 passed、0 skipped、
0 failed/error、0 unfinished、1150.57秒**。62 serial log全てSTOPPED、panicなし。
774取得点で失敗/回復し、heapは346564→346564 byte。
control_coreで14 interval条件、不正open 64回後の正常open、IRQ/VBUS/storage回復も成功した。

S2/P4 HSはcompile-only成功。S3/S2/P4の現在の全29 C/C++ sourceについて、旧vendor path、
TinyUSB設定header、SDK内TinyUSBへのfallback依存がないことを確認した。
`nm`と`addr2line`でendpoint open/runtimeが`EspUsbDwc2Device.c`、PHY初期化が
`EspUsbDwc2Phy.c`に解決し、旧`dwc2_core_init`がないことも確認した。
S2/S3のweak cache no-op、P4のstrong cache処理、Vendor単独での未使用class除去を維持する。

serial原本は`/tmp/pytest-embedded/2026-10-27_19-24-35-352209/`。
JUnit/transcript/serialコピーは`validation/.pytest-results/native-dwc2-all*`、
依存・symbol記録は`native-dwc2-audit.txt`と各targetのnm/build記録。
`python3 tools/verify_usb_backend.py`と`git diff --check`も成功した。
mainは全て取り込み済み、旧tests/examplesはmainとの差分なし。
DWC2由来コードの本体保守への移行と、全protocol/全SoC実機確認の完了は区別する。


## CCID endpoint回復の実測

2026-10-28。Hostの対象PIDをDATA0にそろえても、修正前はHALT解除後の次の応答が戻らず
失敗した（`ccid-halt-data0-baseline.xml`、33.71秒）。修正後の単独境界試験は61.19秒で成功。
最後に反復CLEARと別readerの通信継続を追加し、CCID/control_core/allocation_failureの
**3 passed、0 skipped/failed/error/unfinished、146.41秒**を確認した。

4 runtime容量×2 reader（0→1→0）で、待機中bulk、部分OUT、未読IN、通知、両方向同時HALT、
両解除順序、HALTしていないendpointへの反復CLEARを確認した。
計260 HALT/CLEAR要求、34の状態照会、通知復帰をまたぐ未読返信6回が成功。
別readerの応答が成功しても対象readerのHALTが残ることも確認した。
774取得点の失敗/回復、heap 346564→346564 byte、6 serial全てSTOPPED、panicなし。
S2/P4はcompile-only成功。製品側の変更はCCIDに限定し、直前の全31件は再実行していない。

原本は`/tmp/pytest-embedded/2026-10-27_19-56-13-268381/`。
JUnit/transcript/serialコピーと集計は`validation/.pytest-results/ccid-halt-final*`、
`ccid-halt-audit.txt`。最初の試験用buffer不足・Host PID不一致と、PIDをそろえた製品回帰を
区別してtranscriptを保存した。旧tests/examplesはmainとの差分なし。
この実測は全EP0順序、bus reset/endとの全競合、OS/PCSC、他SoC/HS実通信を証明しない。


## Video bulk HALTの実測

2026-10-28。修正前はSET_FEATURE成功後も借用frameが返らず、`HALTED 0`となった
（`video-halt-baseline.xml`、41.34秒）。修正後の境界単独は31.29秒で成功。
関連control 2件、ISO/bulkの6 runtime構成、allocation_failure、control_coreの
**10 passed、0 skipped/failed/error/unfinished、154.16秒**を確認した。

2 Videoで16回のSET/CLEAR、途中payload読出し2回、復帰後/別Functionの正常frame 10回を確認。
各Videoのloan返却は計8件（Success 5、Cancelled 1、TransferError 2）、重複/誤id/残留loanなし。
送信中HALTの4件はactualBytesKnown=false。HALT中の投稿は拒否し、CLEAR後は再Commitなしで再開した。
774取得点の失敗/回復、heap 346564→346564 byte、20 serial全てSTOPPED、panicなし。
S2/P4はcompile-only成功。全体の収集は32件で、今回の製品変更後は関連10件を実行した。

標準pluginは同じsketchを複数のtest関数で再利用するため、試験sketchの通常のstartで
カウンタを初期化する。stopで旧loanを退役してから次を開始し、conftestによるresetは使わない。
この初期化不足で一括実行が一度失敗した記録も残した。

原本は`/tmp/pytest-embedded/2026-10-27_20-05-52-165319/`。
JUnit/transcript/serialコピーは`validation/.pytest-results/video-halt-final*`、集計は`video-halt-audit.txt`。
全EP0順序、SET/CLEARの全競合とbus reset/end、OSや他SoC/HS実通信は別の未検証範囲として残す。


## NCM方向別HALT回復の実測

2026-10-28。修正前はHALT解除の制御要求が成功しても次のframe交換が戻らなかった
（`ncm-halt-baseline.xml`、43.33秒）。方向別退役後の単独は38.43秒、実STALLを含む補強は44.69秒で成功。
最後に反対方向のOUTと通知をDATA1へ置く条件を加え、NCM raw 2件、NCM stream、
allocation_failure、control_coreの**5 passed、0 skipped/failed/error/unfinished、201.81秒**を確認した。

HALT試験は2 NCM、NTB IN 3200 / OUT 16384 byte、TX/RX各2 buffer。
36 SET/CLEAR、8回の実STALL、24 frame交換、部分NTBの送信10回、未読TXの準備8回が成功。
OUTの回復で未読INを維持し、INの回復で部分OUTとDATA1を維持する。
通知のDATA1もbulk HALT中に維持し、通知回復では既存のbulk返信を維持する。
同時HALTで片方だけを解除し、もう片方がwire上でSTALLを続けることも確認した。

既存NCM streamの3 runtime構成でDHCP/TCP 8193 byte交換が成功。
回復後の2 IP interfaceの開始/回収、774取得点の失敗/回復、heap 346712→346712 byte、
10 serial全てSTOPPED、panicなし。S2/P4はcompile-only成功で、VendorとCCIDを使うprobeのELFに未使用NCMの
state/関数が入らないことをnmで確認した。

原本は`/tmp/pytest-embedded/2026-10-28_06-40-07-341424/`。
JUnit/transcript/serialコピーは`validation/.pytest-results/ncm-halt-final*`、集計は`ncm-halt-audit.txt`。
HostのSTALL用コマンド受付を追加し忘れた試験側失敗も記録した。
全体収集は33件。今回の製品変更後に全33件を実行したわけではない。
任意のEP0/reset/end競合、TCP進行中のHALT/OS実機、他SoC/HS実通信は未検証として残す。


## class HALT整理後の全33件確認

2026-10-28、製品コード`8064804`。標準`pytest --combo all`は**33 passed、0 skipped、
0 failed/error、0 unfinished、2588.65秒**。66 serial全てSTOPPED、STOP_FAILED/panicなし。
774取得点の失敗/回復とheap 346712→346712 byte、3構成のDHCP/TCP 8193 byte交換も確認した。
CCIDの260 SET/CLEAR、NCMの36 SET/CLEARと8 wire STALL、Videoの16 SET/CLEARを含む。
未読返信、部分RX、別Function、反対方向/通知のDATA1を維持する条件も一括実行で通過した。

現在の全29 C/C++ sourceについて、S3/S2/P4のcompiler依存に旧vendor path、TinyUSB設定header、
SDK内TinyUSBへのfallbackがないことを確認。nm/addr2lineでcontroller/DMAとPHYが
`EspUsbDwc2Device.c`/`EspUsbDwc2Phy.c`へ解決し、旧dwc2_core_initがないことを確認した。
S3はVendor単独、S2/P4のbuild probeはVendorとCCIDの構成である。
S2/P4の構成をVendor単独と書いた直前のNCM記録は訂正した。未使用NCM/Video/keymapの除去は成立する。
実機はS3–S3、S2/P4はcompile-only。今回は製品コードを固定した一括確認で、追加の製品変更はない。

serial原本は`/tmp/pytest-embedded/2026-10-28_06-45-32-126204/`。
JUnit/transcript/serialコピーは`validation/.pytest-results/class-halt-all*`、集計と各nmは
`class-halt-audit.txt`、`class-halt-{s3,s2,p4}-nm.txt`。
`python3 tools/verify_usb_backend.py`、git diff --checkも成功し、mainは取り込み済み。
旧tests/examplesはmainとの差分なし。


## reset開始を実IRQで観測するprobe

`s3/reset_start`はprivate IRQ allocatorのwrapperでUSBRSTの開始直後を観測する。
workerをparkしたまま通常のHost stop/startで実resetを発生させ、古いmounted/job-readyが無効になること、
endpoint再登録・data/EP0 DMAが拒否されることを確認する。IRQ内では待機/ログ出力をしない。
ENUMDNE後はworkerをreleaseし、旧loanが一度だけ返り、新しいHost往復が正しいことを確認する。
Hostは独立v3 configuration_epochのraw SDK sketchを再利用し、conftestやplugin追加操作はない。

修正前は実IRQ直後もmounted=1/jobs=1で失敗。修正後は深度4/16/31の3 resetを含む
1 passed、0 skipped/unfinished、119.10秒。2 logs全てSTOPPED、panicなし。
原本`/tmp/pytest-embedded/2026-10-28_07-43-39-489897/`、結果は`.pytest-results/reset-start-guard*`。
この変更後の全34件の結果は次節を参照。


## reset開始guard後の全34件確認

2026-10-28、製品`929f206`を固定。標準`pytest --combo all`は**34 passed、0 skipped、
0 failed/error、0 unfinished、2274.81秒**。68 serial全てSTOPPED、STOP_FAILED/panicなし。
reset開始の実IRQ probeはqueue深度4/16/31で成功し、native再登録/data DMA/EP0 DMAを拒否した。
各resetで旧loan 2件が一度だけ返り、新しい転送2件が正しいid/payloadで完了した。

既存の取消し/reset/end、configuration世代、EP0標準要求・overflow回復、Audio/HID/MIDI/MSC、
CCID/Video/NCMのHALT回復も通過。774取得点の失敗/回復とheap 346712→346712 byte、
3構成のDHCP/TCP 8193 byte交換も確認した。
S2/P4はcompile-only成功。S3/S2/P4全29 sourceのcompiler依存に旧vendor/SDK TinyUSB fallbackなし。
controller/DMA/PHYは内部DWC2、io-ready guardはEspUsbDeviceCore.cppへリンクしている。
S3 Vendor単独とS2/P4 Vendor＋CCIDの構成で未使用NCM/Video/keymapが入らないことも確認した。

原本`/tmp/pytest-embedded/2026-10-28_07-49-13-401967/`。
JUnit/transcript/serialコピーは`validation/.pytest-results/reset-start-all*`、集計と各nmは
`reset-start-audit.txt`、`reset-start-{s3,s2,p4}-nm.txt`。
verify_usb_backend、git diff --checkも成功。mainは取り込み済み、旧tests/examplesはmainとの差分なし。
実機はS3–S3のみ。任意の全競合順序やOS/他SoC/HS実機までの完了とは扱わない。
