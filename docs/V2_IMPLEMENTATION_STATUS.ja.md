# EspUsbDevice v2 実装状況

更新日: 2026-07-29

## 到達点

- Phase 0: 完了。移行前の実機suiteは113件PASS。
- Phase 0.5: 完了。旧Audio実装2ファイルを削除し、provenance境界を記録。
- Phase 1: 基盤完了。host非依存のdescriptor modelとHID writerを追加。
- Phase 2: 完了。TinyUSB `53f8c53c2`を固定し、選択したdevice sourceを
  ライブラリ自身の設定でbuild。完全snapshotは`third_party`に保持し、Arduinoが見る
  `src`はS2/S3/P4の実コンパイラ依存から得た12 source + 31 headerに限定。
- Phase 3: runtimeとclass lifecycle実装済み、S3 peer Gate通過。PHY、rhport、
  TinyUSB task、基本descriptor callbackをライブラリ所有へ移した。`begin()`失敗時は
  開始済みclassを逆順rollbackし、`end()`でcallback registryを解放する。
- Phase 4: 非Audio classの実装済み、S3/P4実機回帰通過。
- Phase 5: controller選択とper-speed descriptor実装済み、P4 HS PC実測待ち。
- Phase 6: Audio format/bandwidth model、entity/stream graph、UAC1/UAC2 descriptor writer、
  endpoint/Clock/Feature control request、polling data plane、固定長control/stream event queueを
  独立実装済み。FIFO clear lifecycleと、playback/captureの転送byte・overrun・
  underrun counterも追加。新公開prototypeは`EspUsbAudioFunction` +
  playback/capture stream。UAC1をdefault、UAC2を明示選択とした。

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
  `audio_model`、`dependency_boundary`の6 suiteをPASS。
- descriptor/runtime実機unit: 265 checks PASS。TinyUSB task、PHY、callback registryを
  同一deviceで100回begin/endし、二重begin/endの冪等性を確認。HID開始後のCDC失敗を
  rollbackし、同じdevice instanceで再beginできることも含む。
- 新Audio公開APIのdescriptor実機unit: speaker / microphone / duplexと、
  mute / volume / stream state eventのpolling、stream stats lifecycleをPASS。
- S3 UAC1 peer: speaker、microphone、headsetの列挙・stream開始・PCM転送をPASS。
  speakerのmute/volumeと連続control requestもSTALL・rebootなしでPASS。
- UAC2 descriptor/class requestは明示選択時に維持し、descriptor unitをPASS。
  UAC2実streamingは対応するHost実装の準備後の共同Peer Gateとする。
- Audio + HID/CDC/Vendorのcomposite descriptor buildと`MAX_CLASSES`制約を
  `unit/composite_constraints`のS3実機testでPASS。
- S3 UAC1 HID+Audio composite Peerは、HID/Audio両interfaceのclaim、endpoint重複なし、
  keyboard入力、48 kHz mono PCM playbackを3/3 PASS。
- host-only `unit/dependency_boundary`でArduino Core TinyUSB header/initializer/
  descriptor loader/endpoint allocatorへの参照、旧Audio Card sourceの再追加、
  first-party AudioのEspressif derivation noticeを自動監査。
- Arduino compile: KeyboardをS2/S3/P4でPASS。
- Arduino compile: 更新後のdescriptor suiteをS3/P4でPASS。
- Arduino compile: Vendor/HID、CDC、MIDI、MSC、NCM、compositeをS3でPASS。
- P4 loopback HID keyboard、CDC、MIDI、MSC、Vendor/WebUSB: PASS。
- 非Audio横断suiteは98/106 PASSの時点で、複合3構成とP4 keyboard layoutの計8件を
  切り分けた。Host CDCがupload中のUSB-Serial/JTAG deviceを先に掴む問題をtarget
  address固定で修正し、layoutのserial capture待ち時間も安定化した。
- class endpoint allocatorをUSB仕様に合わせ、bulk dataのIN/OUTを同じendpoint番号へ
  集約した。CDC/NCMはnotification用1番号+data duplex用1番号、MIDI/MSC/Vendor/HIDは
  duplex 1番号。修正後、該当composite 12件、descriptor/compile/constraint unit 3件、
  MIDI/MSC/NCM/VendorとP4 loopbackの横断31件を全PASS。
- S2/S3のcontroller endpoint validatorを追加。非control INが4本を超える構成は
  SET_CONFIGURATIONまで進めず、`begin()`が`ESP_ERR_INVALID_SIZE`で拒否する。

## link audit

S3 Keyboard ELF/mapでは次を確認した。

- `esp32-hal-tinyusb.c.o`はリンクされない。
- リンカ入力にはArduinoの`libarduino_tinyusb.a`が常に列挙されるが、
  archive memberは1個も取り込まれない。
- `tinyusb_init`、`tinyusb_enable_interface*` symbolは存在しない。
- `tusb_rhport_init`、DWC2、device class driver、descriptor callbackは
  ライブラリbuild treeの実装で解決される。

## 次の作業

1. P4 rhport 0のFS実機試験と、rhport 1をPCへ接続したHS実測。
2. 必要に応じてUAC1 Audio + CDC/Vendor compositeのPeer coverageを追加。
3. UAC2対応Hostの準備後、speaker、microphone、duplexの実streamingとcounterを
   共同Peer testで実測。
