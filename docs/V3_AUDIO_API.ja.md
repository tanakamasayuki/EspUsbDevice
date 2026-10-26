# v3 Audio: event通知のruntime構成

2026-10-27。Audioの独自化に先立ち、固定8件のevent配列を停止中APIへ移した。
通知を使わなければeventCapacity=0で配列を確保しない。Functionとqueueの管理情報は残る。
PCMのTinyUSB class/FIFO・global instanceはまだ置き換えておらず、PCM容量をAPI化した段階ではない。

```cpp
EspUsbDevice device;
EspUsbAudioFunction audio(device, EspUsbAudioProtocol::Uac2);

void setup() {
  auto &playback = audio.addPlaybackStream();
  playback.addFormat({48000, 2, 2, 16});
  EspUsbAudioConfig config;
  config.eventCapacity = 0; // No event polling in this application.
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

未登録Audioの手動beginはInvalidStateで拒否し、そのstreamから既存AudioのFIFOへアクセスしない。
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
最終audio-events-related.xmlは**2 passed、0 skipped、0 unfinished、77.39秒**。
Audio Playback/Capture/両方向、容量0を含む413取得点の確保失敗/回復も確認した。
全caseで両board STOPPED、panicなし。serial原本は/tmp/pytest-embedded/2026-10-26_18-22-12-899869/、
JUnit/transcript/serialコピーはvalidation/.pytest-results/audio-events-related*。

利点は通知不要時の配列省略、通知量に合った容量、開始失敗の明示化、稼働中の構成破壊の防止。
不利益は通知有効時の動的確保・管理情報と、queue操作の短いlockである。
既存の通常のpollEvent呼出し方を維持するが、稼働中のformat追加やFunctionの先行手動beginに依存するコードは移行が必要。

次はUAC1/UAC2の制御・周期転送を独自driverへ移し、選択方向/formatに応じたPCM ring・DMA・feedback資源とする。
その際にformat一覧、control state、容量上限、callback context、reset/alt-setting/endの所有権も再整理する。
今回の試験はPCMの連続転送やfeedbackの安定性、OS音声アプリ、P4/S2/S31/HSを証明しない。
