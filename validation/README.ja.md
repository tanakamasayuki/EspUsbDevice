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
現在はこの一組だけなので17 tests。S3だけを選ぶ場合は`--combo s3`を使う。
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
最新の全実行はplugin 1.10.0の標準`--combo all`による`runtime-audio-all.xml`。
**16 passed、0 skipped、0 unfinished、823.56秒**。全caseで両board STOPPED、panicなし。
serial原本は`/tmp/pytest-embedded/2026-10-26_18-54-11-801467/`、コピーとtranscriptは
`.pytest-results/runtime-audio-all-serial/`と`runtime-audio-all-transcript.txt`へ保存した。
`s3/allocation_failure`は内部確保境界を試験sketchだけで差し替え、Vendor/CDC/HID/MIDI/MSC/Audio/DFU/descriptor/registryの502取得点を失敗させて回復を確認する。
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
通常実行は17 testsであり、各caseで同じS3–S3を順次使い、finallyで両boardを明示停止する。

## plugin側で解決する候補

以下は必要性が出た時の追加依頼候補で、依頼や実装はまだ行っていない。
conftestを別helperや別pluginへ隠して持ち込まない。

- 指定した実機試験が全件skipまたは未実行の場合に失敗終了する標準option。
  現在はpytest終了値0だけを合格にせず、今回の通常全実行が`17 passed`であることを確認する。
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
通常収集は17件。今回の関連2件は直近の全16件成功とは別である。
