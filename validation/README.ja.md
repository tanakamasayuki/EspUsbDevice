# v3の独立したS3–S3試験

このディレクトリだけをv3開発の試験入口とする。`tests/`と`examples/`はv2の参照資産で、
読み込み・収集・helperのimport・sketchの流用をしない。両者との整合性を新APIの制約にしない。
前回作成した独自matrix/結果gateは撤去した。ここにはconftestも独自pytest pluginもない。
`--noconftest`によりconftest読込みを無効にしている。
`--maxfail=1`で本文・USB停止を含む最初の失敗後は、次の実機caseを実行しない。

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
現在はこの一組だけなので26 tests（Videoの6 runtime構成と複数Function制御を含む）。S3だけを選ぶ場合は`--combo s3`を使う。
profileはHostとDeviceに一つずつ。組の列挙にconftestや適用条件マーカーは不要。
実機pytestは一つずつ実行する。build確認だけなら`--run-mode=build`を使う。

将来P4/S2/S31を追加するときは、対応sketchのprofileと結線済みcomboを追加する。
直積の`--profiles=all --peer-profiles=all`はUSBベンチの標準入口にしない。
`--combo all`ではsketchに対応profileがない組は展開されず、どの組にも適合しないtestは
`combo=none`としてskipされる。必要port未設定・同一実機の二役もbuild前にskipされる。
profile別portを使い、未結線の組を登録しない。設定済みportへの接続失敗や通信不一致は失敗として扱う。
共有P4を使うFS/HSの組も逐次実行し、各caseの明示停止とpluginのdevice lockを使う。

参照ガイド: [1.10.0の組合せと適用条件](https://github.com/tanakamasayuki/pytest-embedded-arduino-cli/blob/v1.10.0/TESTING_ADVANCED.ja.md#profile-の組合せと適用条件)。
local pluginの作業ツリーは1.9.0のままだったため、配布版1.10.0と同tagのguideを確認した。
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
最新の全実行はplugin 1.10.0の標準`--combo all`による`runtime-ncm-all.xml`。
**19 passed、0 skipped、0 unfinished、1080.50秒**。全caseで両board STOPPED、panicなし。
serial原本は`/tmp/pytest-embedded/2026-10-27_01-02-46-482679/`、コピーとtranscriptは
`.pytest-results/runtime-ncm-all-serial/`と`runtime-ncm-all-transcript.txt`へ保存した。
`s3/allocation_failure`は内部確保境界を試験sketchだけで差し替え、Vendor/CDC/HID/MIDI/MSC/Audio/DFU/NCM/descriptor/registryの605取得点を失敗させて回復を確認する。
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
