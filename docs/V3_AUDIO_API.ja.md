# v3 Audio: PCM転送とevent通知のruntime構成

2026-10-27。Audio classを独自driverへ移し、TinyUSB Audio class/FIFOと単一global instanceを撤去した。
通知とPCM ringの容量を停止中APIで指定する。Function、control/model、queueの管理情報は残る。
共通EP0/coreとDWC2も本体内部で保守する。

```cpp
EspUsbDevice device;
EspUsbAudioFunction audio(device, EspUsbAudioProtocol::Uac2);

void setup() {
  auto &playback = audio.addPlaybackStream();
  playback.addFormat({48000, 2, 2, 16});
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
追加したstream自体や一形式の制約を再整理した段階ではなく、formatを入れ替えるAPIは後続課題。

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

PCM bufferはsample frame数で指定する。1 frameは全channelの1 sampleで、byte数は
channels × subslotBytes × bufferFrames。24 bit PCMでも容量がframe境界からずれない。
選択した方向だけにringと最大packet分のDMA bufferを確保し、UAC2 Playbackだけに4 byte feedbackを加える。
既定は各256 frame。playbackBuffer/captureBufferに外部byte配列を渡せる。必要長は上記byte数で、
Device.endまで有効に保ちアプリが直接書き換えない。ringはCPU copy用なのでDMA alignmentは不要。
両方向の実使用領域が重なる指定、pointer/size overflow、外部buffer付きの容量0を拒否する。

容量0ならringを確保しない。Playbackは受信を捨てoverrunへ記録する。
CaptureはDMA packetを無音で埋めunderrunへ記録する。USB転送に必要なDMA buffer自体は残る。
capacity()は確保したring byte数、packetCapacity()はselected formatのFS最大packet byte数を返す。
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

未完の範囲は複数formatのalternate選択、format/control stateの固定配列、外部clock feedback、
callback/borrowed PCMの所有権API、OS音声アプリ、P4/S2/S31/HSである。
現在は方向ごとに一形式だけ対応し、worker再armの連続性は実機で評価する。


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
