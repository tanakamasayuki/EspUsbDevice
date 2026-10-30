# v3 Audio: PCM転送とevent通知のruntime構成

2026-10-27。Audio classを独自driverへ移し、TinyUSB Audio class/FIFOと単一global instanceを撤去した。
通知とPCM ringの容量を停止中APIで指定する。Function、control/model、queueの管理情報は残る。
共通EP0/coreとDWC2も本体内部で保守する。

```cpp
EspUsbDevice device;
EspUsbAudioFunction audio(device, EspUsbAudioProtocol::Uac2);

void setup() {
  auto &playback = audio.addPlaybackStream();
  if (!playback.addFormat({48000, 2, 2, 16})) return;
  EspUsbAudioConfig config;
  config.eventCapacity = 0; // No event polling in this application.
  config.playbackBufferFrames = 256; // 1024 bytes for stereo 16-bit PCM.
  if (audio.configure(config) != ESP_OK) return;
  device.begin();
}
```

DeviceとFunctionを作り、stream/formatを設定してDevice.beginする通常の手順は維持する。
Function.beginを手動で先に呼ぶ構成にはせず、開始/終了はDeviceへ揃える。
Device.end→audio.configure→Device.beginで通知容量を変更でき、再ビルドは不要。
既定容量は8件。eventCapacity()は開始前/終了後0、開始後は確保した件数を返す。
configuration().eventCapacityは要求値を保持する。byte数のoverflowはconfigureでInvalidSize、
開始時の確保失敗はstartError/Device.lastErrorのNoMemoryへ伝え、回復時は全領域を回収する。

容量0なら通知を生成せず、pendingEvents/droppedEventsとも0となる。
有効なqueueが満杯なら新しいeventを捨て、既に保存した順序を保ってdroppedEventsを増やす。
pollEventは一件ずつ返す。clearEventsは未読とdrop数を消す。空のqueueはfalseを返す。
容量分の配列をbegin時に一度だけ確保し、push/popでheapを使わない。
queueは短いcritical sectionで保護し、USB workerとアプリのevent追加が競合してもslotを共有しない。

稼働中のconfigure、addFormat、新しい方向のaddPlaybackStream/addCaptureStreamは拒否する。
configureはInvalidState、addFormatはfalseを返す。streamを返す従来APIは既存参照を返して
構成を保ち、Device.lastErrorにInvalidStateを設定する。raw descriptor writerも稼働中の再構築を拒否する。
停止後のformat入替えはclearFormatsで行う。PCM形状ごとのalternateと共有clockの複数rateに対応する。

未登録Audioの手動beginはInvalidStateで拒否し、そのstreamから既存Audioのbufferへアクセスしない。
稼働中のFunction破棄はDeviceを停止し、USB worker/DMAが止まってからevent領域を解放する。
Audio全体の制御状態とsetMute/setVolumeの同時更新、endとPCM read/writeの同時操作まで
安全性を証明した変更ではない。これらの所有権は独自の転送/control stateで再整理する。

## 実測と移行

新規validation/s3/audio_eventsはS3–S3だけを使い、conftest/独自fixture/hookを追加しない。
0/1/17/257件のmetadata反復とheap回復、2 taskから計200件の各task内順序を確認する。
実USBはUAC2 Playback 48 kHz/stereo/16 bitの構成で、0/1/8/17件に20回のMute変更を行い、
保存件数、overflow数、payload順序、HostのGET_CURとの一致を確認する。
未登録Audioを生成・破棄した後も制御要求が動くこと、稼働中破棄後の停止も確認する。

最初のaudio-events.xmlは1 passed（83.46秒）。
eventのみの段階のaudio-events-related.xmlは**2 passed、0 skipped、0 unfinished、77.39秒**。
Audio Playback/Capture/両方向、容量0を含む413取得点の確保失敗/回復も確認した。
全caseで両board STOPPED、panicなし。serial原本は/tmp/pytest-embedded/2026-10-26_18-22-12-899869/、
JUnit/transcript/serialコピーはvalidation/.pytest-results/audio-events-related*。

利点は通知不要時の配列省略、通知量に合った容量、開始失敗の明示化、稼働中の構成破壊の防止。
不利益は通知有効時の動的確保・管理情報と、queue操作の短いlockである。
既存の通常のpollEvent呼出し方を維持するが、稼働中のformat追加やFunctionの先行手動beginに依存するコードは移行が必要。

PCM bufferはsample frame数で指定する。1 frameは全channelの1 sampleで、確保するbyte数は
登録format中の最大値(channels × subslotBytes) × bufferFrames。単一formatならそのframe幅を使う。
read/writeと利用可能容量は、選択中のPCMのframe境界へ切り下げる。
選択した方向だけにringと最大packet分のDMA bufferを確保し、UAC2 Playbackだけに4 byte feedbackを加える。
既定は各256 frame。playbackBuffer/captureBufferに外部byte配列を渡す場合は、
playbackBufferBytes/captureBufferBytesへ提供する領域長も指定する。必要長は上記byte数で、
Device.endまで有効に保ちアプリが直接書き換えない。ringはCPU copy用なのでDMA alignmentは不要。
両方向の実使用領域が重なる指定、pointer/size overflow、外部buffer付きの容量0を拒否する。
pointer/byte長の片方だけを指定した場合はconfigureがInvalidArg、必要量に足りない場合はbeginがInvalidSize。
提供長が必要量より大きくてもringは要求したframe数分だけを使い、末尾を追加容量として扱わない。

容量0ならringを確保しない。Playbackは受信を捨てoverrunへ記録する。
CaptureはDMA packetを無音で埋めunderrunへ記録する。USB転送に必要なDMA buffer自体は残る。
capacity()は選択中のframe境界へ切り下げたring byte数、packetCapacity()は全登録formatのFS最大packetを入れる容量を返す。
どちらもbegin前/終了後は0。HSで必要なpacketが小さくても、FS fallback用のDMA容量は確保する。

read/writeはframe境界まで切り下げたbyte数を処理し、戻り値もbyte数。
Playbackは満杯なら新しいPCMを捨て、未読データを保持する。
Captureは入る分だけcopyして実数を返す。旧TinyUSB FIFOの上書きに依存したアプリは戻り値を確認する。
availableForWrite()で空きを確認できる。容量の小さいqueueでも部分frameを保存しない。
clearBufferはqueueだけを消し、既にarmしたpacketは取り消さない。
Captureはalt=0で事前充填でき、最初のalt=1で保持する。停止や稼働中のalt再設定ではqueueを消す。

captureの端数sample rateは各USB intervalへ分配し、不足を無音補完する。
UAC2 feedbackはUSB由来の公称clockをQ16.16で返す。外部codecのclock driftへ追従する
消費量/clock APIはまだない。この段階のdriverを非同期codecの適応feedback対応とは扱わない。
periodic転送の完了と再armはUSB workerで行う。アプリのread/writeとは短いcritical sectionで共有する。
alt変更はまずhardwareをabortし、旧完了eventをFIFO fenceで排出してからbufferを再利用する。
Device.endはworker/DMA停止後にring、packet、feedback、event配列を解放する。

利点は使う方向と選択formatに応じた確保、build flagなしの容量変更、Functionごとの独立性、
captureの明示的な部分書込み。不利益はbegin時の確保失敗と、copy/lockの費用、capture戻り値確認が必要な点。
Device + Function + addStream/addFormat + begin、read/write/pollEventという通常の流れは維持する。
旧CFG_TUD_AUDIOの有効化と固定buffer flagは拒否する。形式はaddFormatで、queueはconfigureで指定する。

未完の範囲は方向内のchannel数変更、PCM形状ごとに異なるrate集合、外部clock feedback、
callback/borrowed PCMの所有権API、OS音声アプリ、P4/S2/S31/HSである。
方向ごとにchannel数を固定し、subslot/有効bit数をalternateで切り替える。同じFunctionではclockを共有する。


独自PCM driverの関連試験audio-pcm-related.xmlは**3 passed、0 skipped、0 unfinished、145.99秒**。
PCM・event制御・438取得点の失敗/回復を含み、両board STOPPED、panicなし。
serial原本は/tmp/pytest-embedded/2026-10-26_18-51-19-326855/、コピーとJUnit/transcriptはvalidation/.pytest-results/audio-pcm-related*。
Audioのみ/Deviceのみの最小buildとnmも成功し、旧TinyUSB Audioと未使用独自Audioの不在を確認した。
通常収集は16件。この関連3件の後に実行した全16件の結果は下記を参照。


## Audio置換後の全体確認

2026-10-27。標準の`--combo all`で**16 passed、0 skipped、0 unfinished、823.56秒**。
全16 caseで両board STOPPED、32本のserialにpanicなし。438取得点の確保失敗/回復、
UAC1/UAC2 PCMとevent、CDC/Vendor/HID/MIDI/MSC、descriptor/registryと取消し・転送境界を含む。
serial原本は`/tmp/pytest-embedded/2026-10-26_18-54-11-801467/`、コピーとJUnit/transcriptは`validation/.pytest-results/runtime-audio-all*`。
Audioのみ/Deviceのみの最小build/nmも成功。TinyUSB treeは51 file / 15 compiled sourcesで一致し、
旧tests/examplesはmainと同一、mainはv3の祖先。残るclass/core/DCDと他SoC/HSの置換・検証は未完である。


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
この節の保存領域変更の時点では、descriptor/beginの一方向一形式制限を残していた。
formatの入替えAPI、alternate選択、range応答領域等は続けて整理する。

設定・確保失敗・Audio event/control・PCMの関連3件は**3 passed、0 skipped/error/failure/unfinished、242.85秒**。
774開始取得点の失敗/回復、heap346704→346704 byte、実ISO休止4構成も成功。
6 serial全てSTOPPED、panic/watchdog resetなし。S2/P4 compile成功。
全29 sourceの依存/nmに旧stack/SDK fallbackなし、S2/P4の未選択Audioもリンクされない。
原本`/tmp/pytest-embedded/2026-10-28_21-22-05-002801/`、結果は`.pytest-results/audio-storage*`。
これは関連3件の実行で、全35件を再実行した結果ではない。

## 停止後のformat入替え

Playback/Captureの両streamに`esp_err_t clearFormats()`を追加した。
停止中にformat設定領域を解放し、同じFunctionとstream参照でformatを登録し直せる。
空でもESP_OKで、追加の確保を行わない。稼働中（DescriptorsOnlyを含む）はInvalidStateで、
設定・control・領域を変更しない。Device.lastErrorも戻り値へ合わせる。

```cpp
device.end();
if (playback.clearFormats() != ESP_OK) return;
if (!playback.addFormat({44100, 2, 2, 16})) return;
if (!device.begin()) return;
```

`clearBuffer()`はPCMデータ、`clearFormats()`はformat設定を消す別の操作。
clearFormatsは方向の選択を解除しない。選択済みの各streamへformatを登録してからbeginする。
空設定のbeginは拒否される（現descriptor検証ではInvalidSize）。clear後は旧control metadataも無効化する。
もう一方のstreamのformatとAudioConfig、Function登録自体は保持する。
通常のaddFormat→configure→beginの利用と、end後に設定を保持する既定動作は変えない。
利点はオブジェクトを再生成せず設定を入れ替え、不要な設定領域を解放できること。
不利益はclearと再追加の間に空の設定となり、再追加の確保失敗を処理する必要があること。
複数formatのalternate対応を追加した変更ではない。

clearの反復解放、空設定beginの拒否、metadata modeの変更拒否、再追加後の開始を検証した。
UAC2/UAC1で同じFunction/stream参照の48 kHz→44.1 kHz→48 kHzを各1系列、計6回wireで確認。
各構成のPCMとrateを照合し、10構成で稼働中のclear拒否を確認した。
既存のISO休止4構成、event/control、設定追加14箇所と開始774箇所の確保失敗回復も成功。
関連4件は**4 passed、0 skipped/error/failure/unfinished、200.04秒**。8 serial全てSTOPPED、panicなし。
heap346704→346704 byte、S2/P4 compile成功、全29 sourceの依存/nmに旧stack/SDK fallbackなし。
原本`/tmp/pytest-embedded/2026-10-28_21-30-54-318752/`、結果は`.pytest-results/audio-reconfigure*`。
初回のprobe期待値誤りは`audio-reconfigure-initial-expectation-error*`へ記録した。
標準combo/allの収集は36件。この結果は関連4件の実行で、全36件の一括実行ではない。

## 同じPCM形状での複数sample rate

同じstreamへ、channel数・subslot byte数・有効bit数が同じformatをrate違いで登録できる。

```cpp
if (!playback.addFormat({48000, 2, 2, 16})) return;
if (!playback.addFormat({44100, 2, 2, 16})) return;
// With both directions selected, register the same rate set for capture.
if (!device.begin()) return;
```

一つのFunctionのclockは共有するため、両方向を使う場合はrate集合を一致させる。
開始時は最初のPlayback format（PlaybackなしならCapture）のrateを使い、Hostが登録済みrateを選ぶ。
UAC1はType I descriptorへ離散rate一覧を出し、endpointのSET_CUR/GET_CURも実clockへ合わせる。
UAC2はClock SourceのCUR/RANGEへ一覧を出す。未登録rateは拒否し、現在のrateを変更しない。
PCM形状が変わらないrate変更なので、streaming alternateは従来どおり0/1。
この複数rate対応の時点では、異なるPCM形状のalternate切替を残していた。

endpointのpacketサイズとDMA容量は、登録rateの最大packet要求に合わせて確保する。
UAC2の複数rate時だけ、`2 + 12 × rate数` byteのRANGE応答領域を開始時に確保し、endで回収する。
単一rateとUAC1は追加のRANGE領域を持たない。選択済みAudio stateに管理pointerを一つ追加したが、
Function本体の296 byteは変わらない。control要求・PCM転送時に新規確保しない。
rate一覧が重複しないactive modelでは直接indexを使い、RANGEの生成をrate数に比例させる。

残る上限は保存用の8要素配列ではない。UAC1の1 byte descriptor長から1一覧82 rateまで、
UAC2の16 bit control長から5461 subrangeまでとし、超える設定を拒否する。実際のheap・FIFO・FS packet上限も適用する。
rate変更の無音期間や外部codec clockへの追従を保証するものではない。
規格とdriver上の参照は[USB Audio 1.0](https://www.usb.org/sites/default/files/audio10.pdf)と
[MicrosoftのUAC2 clock/range要件](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/usb-2-0-audio-drivers)。

S3の関連5件は**5 passed、0 skipped/error/failure/unfinished、175.61秒**。
13 rateを登録したUAC1/UAC2で、再構成せず48→44.1→48 kHzを各1系列転送し、実clockとPCMを照合。
未登録22050 Hzは拒否し、clockを維持。158 byteのUAC2 RANGEを2/3/13/14/15/64/65/127/158/200 byte要求で
内容・実長・guardまで確認した。794開始取得点の失敗/回復、設定追加14箇所、heap346704→346704、
既存の停止後format入替え・ISO休止4構成・event/controlも成功。10 serial全てSTOPPED、panicなし。
S2/P4 compileと全29 sourceの依存/nmも成功し、未選択Audioの除外を維持した。
原本`/tmp/pytest-embedded/2026-10-28_21-46-07-520296/`、`.pytest-results/audio-rates*`。
rate一覧の直接参照へ整理する前の関連5件（268.82秒）も別名で保持する。
収集は37件。この5件の結果を、全37件の一括試験や異なるPCM形状のalternate対応とは扱わない。

## PCM形状のalternate選択

同じstreamへsubslot byte数・有効bit数が異なるformatを登録できる。
同じPCM形状は一つのalternateへまとまり、各形状を最初に宣言した順に1～255を割り当てる。
同じ形状のrate追加はalternateを増やさない。0はstream停止。
現在の共有clockでは、各PCM形状と両方向が同じrate集合を持つ必要がある。
不足した組合せや方向内でchannel数が変わる設定はbegin前に拒否する。

```cpp
for (uint32_t rate : {24000u, 16000u}) {
  if (!playback.addFormat({rate, 2, 2, 16})) return; // alternate 1
  if (!playback.addFormat({rate, 2, 3, 24})) return; // alternate 2
  if (!playback.addFormat({rate, 2, 4, 32})) return; // alternate 3
}
```

両streamの`bool currentFormat(EspUsbAudioFormat &format) const`で、選択されたPCM形状と現在のrateをcopyできる。
未選択・停止・切替のdrain中はfalseで、引数を変更しない。snapshotなので次のread/writeまで形式を固定する意味ではない。
アプリはeventや照会結果に応じてPCMの処理形式を合わせる。codec/I2Sの自動設定は行わない。

ringは`bufferFrames × 登録format中の最大frame byte数`を開始時に確保する。
外部ringにもこの長さが必要。capacity/availableForWriteは現在のframe境界へ切り下げたbyte数で、
65 frame×最大8 byteの520 byte領域なら、6 byte/frame時のcapacityは516となる。
packetCapacityは全formatのFS最大packetを入れる領域の容量。実転送のMPSは選択alternateに合わせる。
FIFOも物理endpointごとの最大packetで一度だけ予約し、alternateの数だけ積み増さない。

切替時は旧DMAを停止し、完了eventをfenceで排出した後にendpointを再activateする。
同じsubslotでも有効bit数が異なる形式は別として扱い、旧PCMのringを捨てる。
最初に選んだ形式と同じ形式での事前充填は維持する。切替中のread/writeは0。
alternateごとのpacket/ring配列、新しいtask/queue、切替時のheap取得は追加しない。

S3の関連7件は**7 passed、0 skipped/error/failure/unfinished、358.65秒**。
UAC1/UAC2それぞれで16-bit/2-byte、24-bit/3-byte、32-bit/4-byte、24-bit/4-byteを選び、計16回の実ISO往復を確認。
14回、旧ringへ0xa5を事前配置し、新形式へ混入しないことを確認した。
16回の状態/境界照会、32個の不正alternate要求、2 clock rateとfeedback、816開始取得点＋14設定取得点の
失敗回復、heap346704→346704、既存のISO休止・再構成・control/coreも成功。14 logs STOPPED/panicなし。
S2/P4 compileと依存/nmも成功。原本`/tmp/pytest-embedded/2026-10-28_22-26-28-639054/`、
`.pytest-results/audio-alternates-related*`。収集は38件で、この結果は関連7件のもの。

EspUsbHost 2.10.1は列挙時にclaimしたalternateしか開始対象にしないため、新probeはSDKのclaim/releaseとISO転送を使う。
pluginの標準combo/allとnoconftestは維持する。Hostの既定FIFO限界と初期probeの誤りも別名で記録した。
間欠OUTで最初のpacketだけ受信する問題も再現し、DWC2のincomplete OUT回復を4480513で修正した。
Hostが送る全packetの連続性やOS/HS/任意の全切替競合を保証した結果とは扱わない。

新規[AudioPlayback例](../examples/AudioPlayback/AudioPlayback.ino)は公開APIだけでPlaybackを選び、
2 PCM形状×2 rate、192 byteのring、event容量0、currentFormatを示す。受信PCMを消費・表示する例で、DACは要求しない。
Core3.3.12のS3/S2/P4で3ビルド成功、各ELF/FQBNと未使用クラスの除外を確認した。
記録は`.pytest-results/audio-playback-example*`。sketch自体の実機試験やOS認定ではなくcompile確認である。


製品/probe `033f2d8`を固定した全体試験も、**38 passed、0 skipped/error/failure/unfinished、2544.60秒**で成功。
76 serial全てSTOPPED/panicなし、816開始取得点と14設定取得点の失敗回復、heap346704→346704を確認した。
実機はS3–S3で、S2/P4はcompile/依存/nm確認まで。記録は`.pytest-results/audio-alternates-all*`、
[全体の検証記録](../validation/README.ja.md)を参照。


## 外部ringと不完全なformat設定の確認

2026-10-29、製品は`033f2d8`のまま、独立probeの確認範囲を拡大した。
UAC1/UAC2で内蔵・外部ringそれぞれ4 PCM alternateを切り替え、計32回のISO交換を確認。
外部ringは非整列pointerから520 byteを使い、各切替後とend後に前後のguardを検査した。
6 byte/frame時のcapacity=516、8 byte/frame時のcapacity=520と実payloadを照合し、
外部18回・内蔵18回の照会は全て成功。関連2件は2 passed、124.49秒、4 logs STOPPED/panicなし。
`.pytest-results/audio-external-ring*`、原本`/tmp/pytest-embedded/2026-10-28_23-22-08-817368/`。

共有clockを満たさない設定は、保存時ではなくDevice.beginのdescriptor検証で拒否する。
PCM alternateのrate不足、件数は同じだがrateが異なる集合、Playback/Capture間の不一致、
方向内channel数変更はESP_ERR_INVALID_SIZE。修正後は同じFunctionを開始・終了できる。
rate集合が同じなら登録順序が異なっても受け付け、Playbackの最初のrateで開始する。
これらと、最大frame幅での外部ring重複拒否・隣接領域での成功・書込上限とguardを、
UAC1/UAC2計12条件×2回で確認した。1 passed、31.33秒、2 logs STOPPED/panicなし、
heap357280→357280。`.pytest-results/audio-capability-settings*`、
原本`/tmp/pytest-embedded/2026-10-28_23-24-48-191778/`。

今回の拡張は試験のみ。外部配列の実際の確保長をライブラリが検出するAPIを追加した結果ではない。
呼出側には引き続き最大frame幅×bufferFrames以上の有効な領域を渡す責任がある。

共有probeを使う既存のstream/sample-rate/reconfigure 3件も3 passed、88.15秒。
4構成の実bus休止、13 rateと158 byte RANGE、停止後のrate入替えを維持し、6 logs STOPPED/panicなし。
`.pytest-results/audio-external-shared-probe*`、原本`/tmp/pytest-embedded/2026-10-28_23-25-39-968972/`。
この追加試験後に全38件を再実行した結果とは扱わず、全38件の基準は前節の033f2d8を参照する。


## 外部ringのbyte長指定

```cpp
uint8_t storage[520]; // Keep alive until device.end().
EspUsbAudioConfig config;
config.playbackBufferFrames = 65;
config.playbackBuffer = storage;
config.playbackBufferBytes = sizeof(storage);
esp_err_t error = audio.configure(config);
if (error != ESP_OK) return;
if (!device.begin()) { /* Inspect device.lastError(). */ return; }
```

外部storageには、その方向の登録済み最大frame幅×bufferFrames以上が必要。
例えばstereo 16-bitと32-bitを登録した65 frameは520 byteで、先頭の16-bitだけから計算した260 byteでは不足する。
formatを後から追加できるため、最終的な必要量の検証はbegin時に行う。拒否時は外部領域へ書き込まず、
byte長を直してconfigure→beginを再試行できる。内部確保の既定利用では追加の指定は不要。
サイズ指定を含む外部領域が本当に有効かは呼出側の責任である。

configureのesp_err_tをその場で確認し、Device.beginの失敗はDevice.lastErrorで確認する。
これは他クラスのconfigureと同じ流れで、configure失敗時にDevice.lastErrorが更新される前提にはしない。


提供長を含む製品変更後の関連6件は**6 passed、0 skipped/error/failure/unfinished、369.22秒**。
12 logs全てSTOPPED/panicなし。両規格・両方向の長さ32/63 byte不足を計16回、pointer/長さの
不正な組を計16回拒否し、外部storageのguardを維持した。32 ISO交換・36 guard照会、
4構成の実bus休止・rate変更・停止後の再構成も成功。816開始取得点＋14設定取得点の失敗から回復し、
heap346704→346704。Function本体は296→304 byte（提供長2項目）、model16/control52は不変で、
heap取得点は増えていない。S2/P4 AudioPlayback compileと、S3/S2/P4各29 sourceの依存/nmも成功した。
この6件は全38件の再実行ではなく、外部長追加に対応する関連試験である。
記録は`.pytest-results/audio-buffer-extents*`、原本`/tmp/pytest-embedded/2026-10-28_23-29-42-188475/`。


## Audio＋CDCの同時通信とTX FIFO割当て

新しい`validation/s3/audio_cdc`は、UAC1/UAC2、24/48 kHz、Audio先/CDC先の4構成で
再生・録音・feedbackとCDCを同時に動かす。CDC RX97/TX131 byte、Audio各65 frameを使い、
CDCの1/63/64/65/129 byteを比較しながらline codingも230400 baudへ変更する。
PCMが進んだCDC往復を数え、単なる列挙や逐次の転送成功とは区別する。

修正前のAudio先UAC2では、DeviceはCDCを1 byte受信・送信したと報告するがHostは受信せず、
Audioだけが進行した。DWC2がendpoint 5へFIFO 5を割り当てていたため、選択IN endpointの順位から
FIFOを詰めて割り当てるよう修正した。USBのendpointアドレスは変えず、割当て・activate・flushを合わせた。
管理配列やheap取得点は増やさない。専用TX FIFOとUSB endpoint番号を同じものとして扱わない。

修正後は**1 passed、36.60秒**、2 logs STOPPED/panicなし。
4構成それぞれで752/752/938/924回のCDC往復と187回のPCM進行の重なりを確認し、
全payload・CDC byte総数・line coding・UAC2 feedbackが一致した。
実registerでもendpoint 5→FIFO 4を含む14割当てを確認した。回数は進行確認の実測値で、性能指標ではない。
S2/P4 Vendor＋CCID compileも成功。製品f5d1571を固定した全40件も40 passed、1997.81秒で成功し、
80 logs STOPPED/panicなし、816開始＋14設定失敗回復/heap一致、FIFO対応と複合進行を監査した。
全体記録は`.pytest-results/tx-fifo-all*`と[validation README](../validation/README.ja.md)を参照。
原本`/tmp/pytest-embedded/2026-10-29_02-44-45-563887/`、記録`.pytest-results/audio-cdc*`。
初回probeのbuild誤りと、修正前のwire失敗・診断結果も別名で保存した。


## Functionのendpoint番号予約幅

endpointCountは方向付きアドレスの合計ではなく、次のFunctionへ進めるendpoint番号の幅を返す。
AudioのDescriptorLayoutはIN/OUTを独立に割り当て、反対方向なら番号を共有するため、
予約幅もIN必要数とOUT必要数の最大値に合わせる。

| 構成 | 旧予約幅 | 新予約幅 |
| --- | --- | --- |
| UAC1 Playbackのみ/Captureのみ | 1 | 1 |
| UAC1 duplex | 2 | 1 |
| UAC2 Playback＋feedback | 2 | 1 |
| UAC2 Captureのみ | 1 | 1 |
| UAC2 duplex＋feedback | 3 | 2 |

単一Functionの操作やPCM APIは変わらない。後続Functionのendpoint番号は詰まるため、
Hostはdescriptorからアドレスを取得し、旧アドレスを固定しない。
Functionや転送stateのfield、map配列、heap取得点は増やさず、IN個数やFIFOの物理上限も変えない。

独立audio_functionsは4 UAC1 duplexを24 kHz/stereo 16 bitで同時に構成する。
修正前はspan=2のため番号1/3/5/7を必要とし、ESP_ERR_INVALID_SIZEで開始できなかった。
修正後はspan=1で開始でき、各組を0→3→1→2→0の順に選び、Functionごとに異なるPCM値を照合する。
全4組の8 endpointを同時に転送した試験ではなく、構成を維持したまま各組を順に転送する確認である。

開始/終了2回を含む**1 passed、67.36秒**。10 ISO交換と40状態照会に成功し、
未選択Functionは初回選択まで転送量0、全payloadの経路が一致。2 logs STOPPED/panicなし。
原本`/tmp/pytest-embedded/2026-10-29_04-00-18-485270/`、記録`.pytest-results/audio-function-span*`。
初回probeのStream型名衝突と、修正前の開始拒否も別名で保存した。

関連6件も**6 passed、441.14秒**。12 logs STOPPED/panicなし、816開始＋14設定失敗回復、
heap346704→346704、Audio本体304/model16/control52を維持した。
既存Audio＋CDCの4構成、32 PCM alternate交換、36 guard、4休止構成、rate/RANGEと停止後再構成も成功。
S2/P4 AudioPlayback compile、S3/S2/P4各29 source依存/nmと未使用機能除外も成功した。
原本`/tmp/pytest-embedded/2026-10-29_04-01-47-457798/`、記録`.pytest-results/audio-function-span-related*`。
全体収集は43件。この1＋6件を全43件一括実行やOS/S2/P4実機の結果とは扱わない。


## CH32RV調整向け：Playbackあふれ仕様の初稿（2026-10-30）

CH32RV側がESPの仕様を確認するための文書。以下は製品22afc15の現行実装を基準とする。
以下は現行挙動の記録であり、後述する合意済みの新仕様と区別する。文書整理によるコード変更・追加実機試験はない。
対象はHost→DeviceのPlayback。Captureのwriteへ同じ上書き方針を適用しない。

### 現行ESP：未読を保持し、新着の入らない末尾を捨てる

sample frameは全channelの1時刻分で、byte数F=channels×bytesPerSample（subslot幅）。
有効bit数bitsPerSampleだけからbyte数を計算しない。順序はFIFOで、readは最も古い未読frameから返す。
正常完了し、streamがactiveかつ切替処理中でない受信packetについて、次の規則で処理する。

- Bを受信byte数、Cを現在の形式に対する実効ring容量、Uを現在の未読byte数とする。
- 新着の完全なframeはA=floor(B/F)×F byte。空きもframe単位に切り下げ、K=min(A,C-U) byteを受理する。
- 既存の未読U byteを残し、新着packetの先頭K byteを末尾へ追加する。残りB-K byteを破棄する。
- 古い未読frameを追い出したり、受信packet全体の受理まで待ったりしない。次のUSB受信を継続する。
- packet末尾に不完全なframeがあれば保存しない。次packetと結合する隠れbufferも持たない。

式は内部lockで直列化された1回の処理を表す。利用側がcapacityとavailableを別々に読んで、
その間の受信/読出しまで含めた予約や同一時点のsnapshotを得られるわけではない。
実装の照合先は[AudioTransportの受信処理](../src/internal/EspUsbAudioTransport.cpp)。

| 状態（文字はsample frame） | 新着 | 処理後の未読 | 破棄 |
| --- | --- | --- | --- |
| 容量4、未読A/B/C | D/E/F | A/B/C/D | E/F |
| 容量4、満杯A/B/C/D | E/F | A/B/C/D | E/F |
| 容量2、空 | A/B/C | A/B | C |
| 容量0 | A/B | 空 | A/B |

従って、読出しが止まっていると古いPCMが残る。保存byte数には上限があるが、音の鮮度や
実時間での遅延上限を保証しない。live再生の追従性と、先に受けた未読の保持は別の要求として扱う。
readの要求長はframe単位へ切り下げ、受理済みのframeだけを返す。
ringのwrapや外部bufferでもこの順序を変えず、sample frameの途中から読出し位置をずらさない。

### statsと明示的な破棄

- B-K>0なら、そのpacketに対してoverrunCountを1増やし、overrunBytesへB-Kを加算する。
  現行のoverrunBytesにはring満杯だけでなく、packet末尾の不完全frameの破棄も含まれる。
- transferredBytesはUSBで正常受信したBを数える。アプリがreadした量やringへ入ったKとは区別する。
- 容量0でも有効なpacketを受信すれば、全量の破棄をoverrunへ記録する。
- clearBufferは未読queueを消す。既にarmしたpacketを取り消す操作ではなく、後続完了で再びPCMが入ることがある。
- resetStatsは統計の初期化で、queueの破棄とは別。counterはuint32_tで、長時間集計ではwrapも扱う。

Captureはアプリのwriteで入る分だけcopyし、実際の受理byte数を返す。未受理分は呼出側で扱う。
Playbackの既定を変える議論だけで、Captureのpartial契約まで変えない。

### Playback保持方針のAPI仕様案

2026-10-30の仕様案に対し、CH32RV側からAPIを実装しX035C8実機試験を通したとの返答を受領。
命名とDropOldest既定は合意済み。**ESP側も同日に実装し、S3–S3で内容照合試験を行った**（下記）。
先方の試験結果は先方報告であり、ESPの検証結果ではない。

```cpp
enum class EspUsbAudioOverflowPolicy : uint8_t {
  DropNewest, // 古い未読を保持し、新着の入らない末尾を捨てる
  DropOldest, // 古い未読を捨て、最新の完全なframeを保持する
};
// EspUsbAudioConfigへ追加するfield:
// EspUsbAudioOverflowPolicy playbackOverflowPolicy =
//     EspUsbAudioOverflowPolicy::DropOldest;
```

合意した仕様ではDropOldestを既定とし、ライブ再生で読出し再開後に古い音を流し続けにくくする。
現行ESPと同じ保持順が必要な用途はDropNewestを明示する。いずれも欠落のない録音は保証しない。
単純なconfigure→beginの手順は変わらないが、あふれた際の保持内容は変わるため、
既定値の変更を移行時に明示する。CH32RVでは型prefixを読み替え、field名と値名を合わせる。

configureで停止中だけ指定する。稼働中はESP_ERR_INVALID_STATE、未知のenum値は
ESP_ERR_INVALID_ARGで拒否し、失敗時は既存設定を保持する。configuration()は選択値を返す。
実行中の切替setterは設けず、end→configure→beginで変更する。

### 提案する保持範囲と統計

F、B、A、C、Uは上の現行仕様と同じ。Cは物理容量をFの倍数へ切り下げた実効容量とする。
readと受信によるqueue更新は直列化し、途中のframeや並び替えを公開しない。

| 方針 | 処理後のqueue | そのpacketで数える破棄byte数 |
| --- | --- | --- |
| DropNewest | 古いU byte＋新着の先頭K byte。K=min(A,C-U) | B-K |
| DropOldest | R=min(A,C)、D=max(0,U+R-C)。古いqueueの先頭D byteを捨て、新着の完全frame部分の末尾R byteを追加 | D+B-R |

DropOldestは「古いqueue＋新着の完全frame列」の末尾C byteを残す操作に相当する。
容量4、未読A/B/C、新着D/E/FならDropNewestはA/B/C/D、DropOldestはC/D/E/F。
容量2の空queueへA/B/Cが来た場合は、それぞれA/BとB/Cになる。
末尾の不完全frameは両方で破棄し、次packetへ持ち越さない。容量0は全量破棄し受信を続ける。

破棄があった受信packetごとにoverrunCountを1増やす。overrunBytesは上表の値を加算する。
DropOldestでは今回packetからの破棄だけでなく、過去に受理したPCMの追い出しも数える。
transferredBytesはUSBで正常受信した量のまま維持する。read量や保持量のcounterへ変えない。
clearBuffer/resetStatsの意味、Captureのpartial write契約は現行どおりとする。
追加ring、packet間の断片buffer、監視task、破棄ごとのcallbackは設けない。
policyの設定値と受信時の分岐はAudio固有の費用とし、未使用Functionへ持ち込まない。

### 実装時の合格条件と未決事項

- 内容の異なるPCMで両方の順序、満杯、wrap、packetより小さいring、容量0を照合する。
- F=6/8、Fの倍数でない物理容量、不完全な末尾を含め、常に完全frameだけを返す。
- 古いPCMの追い出し、新着破棄、断片破棄についてcount/bytesを照合する。
- 不正enum、開始中configure、end→再開始、alternate/形式変更時のqueue初期化を確認する。
- Captureのpartial契約と、追加buffer/taskを持たないことを確認する。

DropOldest既定と上記の型・field名は合意済み。保持範囲とstatsを含むこの仕様で実装した。
CH32RVの成功をESP側の合格条件の代替にしない。

### ESPでの実装と実測（2026-10-30）

受信完了の処理だけを方針で分岐し、追加ring・断片buffer・taskは持たない。policyはconfigの1 byte。
`validation/s3/audio_overflow`は、raw SDK Hostが長さと個数を決めたOUT packetへframe番号入りPCMを送り、
Deviceが読み出した番号列とstatsを、Python側で上記の式を実装したmodelと比較する。

| 条件 | 内容 |
| --- | --- |
| 両方針 | ring 520 byteへ10 frame読出し後にwrap・満杯・末尾2 byteの不完全frame。DropNewestは10-139、DropOldestは94-223を保持 |
| ringがpacketより小さい | 32 byte（8 frame）、40 byte（F=8で5 frame）。読出し後の追加で、DropNewestは2-4,48-49、DropOldestは68-72 |
| F=6と非倍数容量 | 物理520 byteを516 byte（86 frame）として扱う |
| 容量0 | 全量をoverrunへ数え、読出しは0 |
| 設定 | 既定DropOldest、未知の値はINVALID_ARGで設定保持、開始中のconfigureはINVALID_STATE |
| alternate解除 | 未読を破棄し、available 0 |

各条件でoverrunCount/overrunBytes/transferredBytesもmodelと一致した。DropNewest/DropOldest各5条件と
既定値1条件の計11開始で**1 passed、31.49秒**。記録`validation/.pytest-results/audio-overflow*`。
受信のlost 0を前提にしており、ISO packetの欠落があれば失敗として扱う。
既定変更後のAudio関連回帰（S3 audio_* 6 probe＋P4 Audio、計12件）も**12 passed、690.49秒**、panic/停止失敗なし。記録`audio-overflow-related*`。

[調整状況](V3_CH32RV_API_ALIGNMENT.ja.md)、[残作業](../TODO.ja.md#優先する残作業)を参照。
