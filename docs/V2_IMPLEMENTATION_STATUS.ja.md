# EspUsbDevice v2 実装状況

更新日: 2026-07-29

## 到達点

- Phase 0: 完了。移行前の実機suiteは113件PASS。
- Phase 0.5: 完了。旧Audio実装2ファイルを削除し、provenance境界を記録。
- Phase 1: 基盤完了。host非依存のdescriptor modelとHID writerを追加。
- Phase 2: 完了。TinyUSB `53f8c53c2`を固定し、選択したdevice sourceを
  ライブラリ自身の設定でbuild。完全snapshotは`third_party`に保持し、Arduinoが見る
  `src`はS2/S3/P4の実コンパイラ依存から得た12 source + 31 headerに限定。
- Phase 3: runtimeとclass lifecycle実装済み、S3 peer Gate再確認待ち。PHY、rhport、
  TinyUSB task、基本descriptor callbackをライブラリ所有へ移した。`begin()`失敗時は
  開始済みclassを逆順rollbackし、`end()`でcallback registryを解放する。
- Phase 4: 非Audio classの実装済み、S3 peer Gate再確認待ち。
- Phase 5: controller選択とper-speed descriptor実装済み、P4 HS PC実測待ち。
- Phase 6: Audio format/bandwidth model、entity/stream graph、UAC2 descriptor writer、
  Clock/Feature control request、polling data plane、固定長control/stream event queueを
  独立実装済み。FIFO clear lifecycleと、playback/captureの転送byte・overrun・
  underrun counterも追加。新公開prototypeは`EspUsbAudioFunction` +
  playback/capture stream。実streaming Gateは未完了。

## 現在使える経路

- HID単体・複合HID
- Vendor bulk/control、WebUSB URL
- CDC、MIDI、MSC、NCM
- S2/S3 FullSpeed controller
- P4 `EspUsbController::{Auto, FullSpeed, HighSpeed}`のruntime選択

`Auto`はS2/S3でFullSpeed、P4でHighSpeedを選ぶ。P4ではFullSpeedをrhport 0、
HighSpeedをrhport 1へmapし、対応するPHYを確保する。

P4 HighSpeed controllerではnegotiated speedをdescriptor callback時に読み、
FS/HS configurationを切り替える。bulk MPSはFS 64 / HS 512で、device qualifierと
other-speed configurationもライブラリ側から返す。

## 意図的に停止する経路

旧`EspUsbDeviceAudio` APIは実装を削除済みで、互換shimやArduino Coreへのfallbackは
作らない。新Audioは`EspUsbAudioFunction`を使う。

旧宣言とexampleは移行中の一時的なcompile failureを許容する。旧runtimeへは戻さない。

## 検証結果

- host unit: `tinyusb_vendor`、`tinyusb_config`、`descriptor_model`、`keymap`、
  `audio_model`の5件PASS。
- descriptor実機unit: 61 checks PASS。`end()`後の別device開始と、class途中失敗時の
  callback registry rollbackを含む。
- 新Audio公開APIのdescriptor実機unit: speaker / microphone / duplexと、
  mute / volume / stream state eventのpolling、stream stats lifecycleをPASS。
- S3 peer: spec準拠UAC2のAudio OUT 98-byte endpointと4-byte feedback endpointの
  列挙PASS。EspUsbHost 2.5.0はUAC1 Type-I parserのみのため、UAC2 format解釈と
  streaming開始はhost parser対応待ち。
- Arduino compile: KeyboardをS2/S3/P4でPASS。
- Arduino compile: 更新後のdescriptor suiteをS3/P4でPASS。
- Arduino compile: Vendor/HID、CDC、MIDI、MSC、NCM、compositeをS3でPASS。
- P4 loopback HID keyboard、CDC、MIDI、MSC、Vendor/WebUSB: PASS。
- S3 peer HID keyboard: device upload前に設定済み`/dev/ttyACM1`が消失し、
  3件は環境ERROR。firmwareのtest failureではない。

## link audit

S3 Keyboard ELF/mapでは次を確認した。

- `esp32-hal-tinyusb.c.o`はリンクされない。
- リンカ入力にはArduinoの`libarduino_tinyusb.a`が常に列挙されるが、
  archive memberは1個も取り込まれない。
- `tinyusb_init`、`tinyusb_enable_interface*` symbolは存在しない。
- `tusb_rhport_init`、DWC2、device class driver、descriptor callbackは
  ライブラリbuild treeの実装で解決される。

## 次の作業

1. S3 peer portを復旧してPhase 3のHID実機Gateを再確認。
2. begin/end反復とpartial failure cleanupを検証。
3. S3のCDC、MIDI、MSC、Vendor、NCM、composite peer testを再実行。
4. P4 rhport 0のFS実機試験と、rhport 1をPCへ接続したHS実測。
5. UAC2-aware hostまたはPCでspeaker streamingを実測。
6. UAC2-aware hostでspeaker、microphone、duplexの実streamingとcounterを実測。
