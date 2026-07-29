# EspUsbDevice v2 移行計画

## この計画の位置付け

この文書が承認されるまでは、v2 の実装変更を開始しない。

v2 は後方互換を目的にした段階的deprecationではなく、USB runtimeとAudioを作り直す
major releaseである。v2 rewrite branchでは、移行途中のcommitが一時的にcompile不能に
なることを許容する。各Gateは「常時greenに保つ条件」ではなく、次のフェーズへ進む前に
到達するcheckpointとして扱う。

v1 maintenance branchはbuild可能な状態を維持する。v2をdefault branch上で直接開発する
場合は、この保証も置かず、Phase 8のcutover Gateで初めて全体buildを必須にする。

設計上の最終状態は [V2_ARCHITECTURE.ja.md](V2_ARCHITECTURE.ja.md) に定義する。

## 移行方針

### リリース境界

- v1系を既存Arduino USB integrationを使うmaintenance lineとして固定する。
- v2は新runtimeだけを持ち、旧runtimeとの実行時fallbackや切替macroを提供しない。
- v2開発開始前のv1最終状態にtagを付け、実機回帰の比較基準にする。
- v2の最初の対応coreはArduino-ESP32 3.3.11に固定する。
- 3.3.9/3.3.10への対応はv2のruntime完成後にcompatibility phaseで判断する。

旧経路を同居させると、強い/弱いTinyUSB symbol、PHY所有権、descriptor callbackのどれを
使っているか判定できなくなる。そのため「classごとのfallback」も行わない。

### 公開APIの方針

既存APIの維持自体を目標にしない。同じAPIが新設計でも最も単純で自然な場合は維持するが、
互換性のためにownershipの曖昧さ、特例、重複した状態、二重APIを持ち込まない。

判断の優先順位:

1. USB function、stream、bufferのownershipが明確
2. lifecycleとerrorが一貫している
3. callback contextとbuffer lifetimeが明確
4. class間で同じ概念に同じAPIを使う
5. 最小の型とmethodで基本機能を表現できる
6. そのうえで既存APIと同じ形なら維持する

維持できる可能性が高いもの:

- CDCの`available()`、`read()`、`write()`というStream的操作
- MIDIのpacket/message操作
- MSCのblock read/writeという抽象
- HIDのraw report送受信
- Vendorのbulk/control transfer

再検討するもの:

- class objectをconstructorだけで暗黙登録する方式
- `begin()`がdevice/classの両方に存在する現在のlifecycle
- singleton/global callback dispatch
- callbackとpolling APIの重複
- classごとに異なるevent/error表現
- NCMのUSB functionとnetwork serviceを1 classに持たせる構造
- HID convenience classの粒度
- Audio API全体
- WebUSB / Microsoft OS capability設定

破壊的変更を行う場合は、単なるrenameではなく、削除できる状態・特例・誤用が具体的にあることを
設計記録へ残す。旧名alias、deprecated wrapper、互換shimは原則作らない。

### 依存の境界

v2で残すplatform dependency:

- Arduino基本API
- ESP-IDFのSoC、USB PHY、FreeRTOS、heap/cache API
- ESP-IDFのnetwork API（NCM helperを使う場合のみ）

v2で切るdependency:

- Arduino `USB` library
- Arduino `esp32-hal-tinyusb`
- Arduino core同梱TinyUSB binary/configuration
- `esp_tinyusb`のdescriptor builder

TinyUSBはversionを固定したsource snapshotとしてライブラリに含める。必要なdevice stack、
class driver、ESP32 DWC2 portable driverだけをbuild対象にし、由来のcommit、license、
local patchをmanifestに記録する。

### Espressif由来Audio sourceの扱い

現在の次のファイルは、Copyright noticeだけを削除して再利用しない。

- `src/EspUsbDeviceAudio.cpp`
- `src/EspUsbDeviceAudioDescriptors.h`

Apache License 2.0では、派生物のsourceを配布する場合、該当するcopyright/attribution noticeを
保持する条件がある。したがってnoticeを外す方法は「由来コードを残したまま表記だけ消す」
ことではなく、該当ファイルを削除し、新しい設計から独立実装することとする。

新Audio実装で参照してよいもの:

- USB Audio Class仕様のdescriptor/request wire format
- USB 2.0仕様
- TinyUSBの公開header/APIと、third-partyとして保持するTinyUSB実装
- 本プロジェクトが独自に定義したfunction/stream model

新Audio実装へ移植しないもの:

- Espressif由来descriptor macro
- 旧control callbackのコードと分岐構造
- 旧global state、task、event loop
- 旧software volume helper

third-party TinyUSB内のcopyright/licenseはそのまま保持する。「Espressif表記をなくす」という
完了条件はfirst-party Audio sourceに対して適用し、正当に保持すべきthird-party noticeまで
削除しない。

## フェーズと移行ゲート

### Phase 0: v1 baselineを固定する

目的:

- 書き換え前の正常系と既知制約を再現可能にする。

作業:

- 現在のunit/compile smokeを実行する。
- S3 peer testのclass別結果を記録する。
- P4 loopback testのclass別結果を記録する。
- P4をPCへ接続したHS descriptorと実リンク速度を保存する。
- 現在のfirmware size、static RAM、runtime heapを代表構成ごとに記録する。
- descriptor dumpをbinary fixtureとして保存する。ただしv2の期待値にはせず比較資料とする。

成果物:

- `docs/V2_BASELINE.ja.md`
- descriptor fixture
- v1最終tag

Gate 0:

- HID、CDC、MIDI、MSC、Vendor、NCM、Audio speaker/mic/headsetのbaselineが存在する。
- 失敗するテストは「既知失敗」として理由が記録されている。

### Phase 0.5: 旧Audio sourceを隔離・削除する

目的:

- v2 AudioがEspressif由来実装の継続改変にならない境界を、実装開始前に作る。

作業:

- v1最終tag/maintenance branchで旧Audio sourceを保存する。
- v2 branchから`EspUsbDeviceAudio.cpp`と`EspUsbDeviceAudioDescriptors.h`を削除する。
- 旧Audio API宣言、example、testは同時に削除するか、一時的なcompile failureとして残す。
- 新Audio用の空実装や互換stubは作らない。
- Audio source provenanceと参照資料をmanifestへ記録する。

このフェーズ以後、v2 Audioの実装作業では旧ファイルをcopy元として使わない。

Gate 0.5:

- first-party `src`にEspressif由来Audioコードが残っていない。
- v1 baseline/tagから旧挙動を確認できる。
- v2がcompile不能でも、このGateでは許容する。

### Phase 1: host上で動くdescriptor modelを作る

目的:

- PHYやTinyUSBより先に、interface/endpoint/stringの所有権をcoreから切り離す。

作業:

- `DescriptorBuildContext`を追加する。
- direction別endpoint allocatorを追加する。
- FS/HSの2つのconfiguration bufferを同じfunction graphから生成する。
- device qualifier / other-speed configurationを生成する。
- descriptor validatorを追加する。
- 既存HID builderを新modelへ移す。

この段階では実機runtimeを切り替えない。純粋ロジックとしてhost unit testする。

必須検査:

- interface番号の連続性
- endpoint address重複
- endpoint数とSoC上限
- `wTotalLength`
- FS/HS bulk MPS
- configuration間でのfunction/interface対応
- string indexの参照整合性
- Audioのbandwidth計算用API

Gate 1:

- descriptor生成コードがArduino/TinyUSB headerなしでhost g++ testできる。
- HID単体とHID compositeのFS/HS fixtureが通る。
- 不正なendpoint/interface構成がbuild時に具体的なerrorで拒否される。

### Phase 2: TinyUSB sourceをライブラリ所有へ移す

目的:

- Arduino coreがprebuildしたTinyUSB configurationへの依存を切る。

作業:

- [x] 使用するTinyUSB release/commitを固定する。
- [x] source、license、provenance manifestを追加する。
- [x] ライブラリ所有の`tusb_config.h`を追加する。
- [x] device classのcompile-time上限をv2 APIの上限と一致させる。
- [x] ESP32 S2/S3/P4 DWC2 driverに必要なsourceだけをbuildする。
- [x] DMA/cache alignment設定をtarget別に定義する。
- [x] upstream更新用の検証scriptを追加する。

リンク検査:

- link mapでArduinoの`libarduino_tinyusb`が取り込まれていないこと。
- `nm`で`tinyusb_init`、`tinyusb_enable_interface`への未解決参照がないこと。
- descriptor callbackがv2実装の1組だけであること。

Gate 2:

- [x] S3/P4のcompile smokeが通る。
- [x] TinyUSB sourceのlicense/provenanceがrelease packageに含まれる。
- [x] Arduino core同梱TinyUSBの設定を変えてもv2側の`CFG_TUD_*`が変化しない。

### Phase 3: FS runtimeをS3で立ち上げる

目的:

- core初期化を使わない最小のdevice runtimeを実機確認する。

対象:

- S3
- HID keyboard単体
- Full Speedのみ

作業:

- `EspUsbRuntime`を実装する。
- PHY handle、rhport、TinyUSB taskを所有する。
- device/configuration/string/HID report callbackをv2へ移す。
- `begin()`のpartial failure cleanupを実装する。
- `end()`と同一controllerでの再`begin()`を実装する。
- Arduino USB CDC on bootとの競合を検出する。

Gate 3:

- S3でHIDが列挙し、入力reportがpeer testを通る。
- 100回のbegin/endまたはattach/detach試験でresource leakがない。
- coreの`USB.begin()`、`tinyusb_init()`が呼ばれていないことをlink mapとlogで確認できる。

### Phase 4: 非Audio classを新descriptor/runtimeへ移す

移行順:

1. Vendor
2. CDC
3. MIDI
4. MSC
5. NCM
6. HIDとのcomposite

順序の理由:

- Vendorでbulk IN/OUTとcontrol requestを最小構成で確認できる。
- CDC/MIDI/MSCでTinyUSB class driverの種類を段階的に増やせる。
- NCMはUSB以外にesp_netif/lwIPの状態を持つため最後にする。
- compositeは単体classのdescriptor/runtimeが安定してから確認する。

各classで必要な作業:

- 旧static loaderをclassのdescriptor writerへ移す。
- coreのendpoint allocator呼び出しを削除する。
- TinyUSB callbackからinstanceを引くregistryを統一する。
- classの開始失敗時に登録状態をrollbackする。
- 単体descriptor test、S3 peer test、composite testを通す。

Gate 4:

- S3の全非Audio peer testが新runtimeで通る。
- `src`から`esp32-hal-tinyusb.h`と`USB.h`のincludeが消える。
- 未移植classを含むdeviceが明示的に失敗し、coreへfallbackしない。

### Phase 5: P4 controller選択とper-speed descriptorを実装する

目的:

- P4でFS/HS controllerをAPIから選択可能にする。

作業:

- [x] `EspUsbController::{Auto, FullSpeed, HighSpeed}`を公開する。
- [x] FullSpeedをrhport 0、HighSpeedをrhport 1へmapする。
- [x] controllerと一致するPHYを確保する。
- [x] negotiated speedでconfiguration callbackを切り替える。
- [x] qualifier/other-speed requestを実装する。
- P4のcache sync、DMA alignment、endpoint/FIFO上限を検証する。

注意:

- HighSpeed controller上でFS hostへ接続する試験と、FullSpeed controllerを使う試験は別物。
- 1台P4 loopbackではUTMI PHYをhost/deviceで共有できないため、HS link試験は2台またはPCを使う。

Gate 5:

- P4 rhport 0でFS class testが通る。
- P4 rhport 1をPCへ接続してHS列挙する。
- rhport 1をFS hostへ接続した場合にFS descriptorを返す。
- bulk MPSがFS=64、HS=512である。
- 存在しないcontroller指定がfallbackせず失敗する。

### Phase 6: Audioを新しいfunction/stream modelで実装する

目的:

- Arduino Audio Card由来の固定topologyとglobal stateを廃止する。

実装順:

1. [x] Audio format/bandwidth validator
2. [x] Audio entity/stream graph
3. [x] UAC2 descriptor writer
4. [x] UAC2 class request
5. [x] playback data plane（polling API、実転送Gate待ち）
6. [x] capture data plane（polling API、実転送Gate待ち）
7. [x] control/stream state event queue（固定長、polling、drop count）
8. [x] FIFO clear lifecycleとoverrun/underrun counter
9. [x] duplex/headset（Device firmwareとdescriptor、実転送Gate待ち）
10. UAC1 descriptorとclass request（初期v2公開範囲外）
11. 複数alternate setting / sample rate
12. Audio + 他classのcomposite

実装規則:

- descriptor writerは旧`EspUsbDeviceAudioDescriptors.h`のmacroを変形・転記しない。
- control request処理は旧callbackを移植せず、entity/stream modelから新規に組み立てる。
- descriptorの一致はsourceの一致ではなく、仕様に対するbyte-level testで検証する。
- 新規ファイルのheaderには実際の著作者とproject licenseだけを記載する。
- 完成時に旧Audio sourceとの類似箇所を監査し、仕様上必須でない一致を解消する。

初期上限:

- Audio functionは1個
- playback streamは最大1個
- capture streamは最大1個
- channelはmono/stereo
- PCM Type I
- 16/24/32 bit
- sample rateは固定長list

上限はdescriptor model上の制約として表現し、旧Audio Cardのconstructor形状には戻さない。

data planeの受入条件:

- TinyUSB callbackからuser callbackを直接呼ばない。
- playback/captureに独立したbounded queue/FIFOを持つ。
- overrun/underrun countを取得できる。
- buffer lifetimeがAPIで明確である。
- USB layer内でvolume DSPを暗黙適用しない。

Gate 6A (UAC2):

- Device側ではS3/P4 compile、FS/HS descriptor、class request、FIFO/event、
  speaker/microphone/duplex Peer firmwareまでを完成条件とする。
- S3 FSとP4 HSの実streaming、control flood、counter実測はEspUsbHostのUAC2対応後に
  Peer testとして実施する。この外部GateはDevice側cutoverを妨げない。
- protocolを変えず、速度によってMPS/intervalだけが変わる。

Gate 6B (UAC1):

- 初期v2ではUAC1を公開しない。
- 将来追加する場合は、未実装selectorを先に公開せず、descriptor/class requestと
  streaming testを同時に追加する。

### Phase 7: WebUSB / Microsoft OS descriptorを独立実装する

目的:

- coreのBOS/vendor control callbackへの最後の依存を除く。

作業:

- WebUSB capabilityを独立functionではなくdevice capabilityとして実装する。
- Microsoft OS 2.0 descriptor setをv2のinterface allocation結果から生成する。
- Vendor classのcontrol requestとdevice capability requestのdispatch順を定義する。

Gate 7:

- WebUSB landing page requestが通る。
- Windowsで対象interfaceへWinUSBがbindする。
- WebUSB無効時にBOS/vendor requestを広告しない。

### Phase 8: cutoverと削除

削除対象:

- 旧`EspUsbDeviceAudio` API
- core descriptor loader群
- core endpoint allocator対応コード
- Audio専用event loop
- `startTinyUsb`
- 「Audioだけ`USB.begin()`」という特例
- v1の既知制約を前提にしたtest/documentation

`EspUsbDeviceAudio.cpp`と`EspUsbDeviceAudioDescriptors.h`はPhase 0.5で先に削除する。

更新対象:

- 全example
- README/API reference
- test plan
- compatibility matrix
- library description
- changelog/migration guide

Gate 8:

- forbidden dependency scanが0件。
- first-party Audio sourceのEspressif copyright/derivation noticeが0件。
- 全unit test、S3 peer、P4 FS loopback、P4 HS manual/peerが通る。
- clean installしたArduino CLI環境でrelease archiveだけからbuildできる。
- v1 APIを使うcompile-fail testが、意図したmigration messageを出すかmigration guideで置換先を示す。

## API移行表

| v1 | v2 |
|---|---|
| `EspUsbDeviceConfig::startTinyUsb` | 削除。v2 runtimeのみ |
| device側port/speed指定なし | `EspUsbController`を指定 |
| `EspUsbDeviceAudio(device, rate, bits, spk, mic)` | `EspUsbAudioFunction` + playback/capture stream |
| `onData()` / `onPcm()` | playback streamの`available()` / `read()` |
| `writeMic()` | capture streamの`write()` |
| Audio内`applyVolume()` | 独立DSP/helperへ分離 |
| Audio内`onEvent()` | control state/event queue |
| core由来WebUSB設定 | v2 device capability設定 |

完全なAPI名はPhase 1のmodelとPhase 6のprototypeをhost testで使ってから固定する。

## テストマトリクス

| 項目 | Host unit | S3 FS peer | P4 FS | P4 HS | P4 HS controller + FS host |
|---|---:|---:|---:|---:|---:|
| descriptor/allocator | 必須 | - | - | - | - |
| HID | 必須 | 必須 | 必須 | 必須 | 必須 |
| Vendor/CDC/MIDI/MSC | 必須 | 必須 | 必須 | 必須 | 必須 |
| NCM | 必須 | 必須 | 任意 | 必須 | 任意 |
| UAC2 playback/capture | 必須 | 必須 | 必須 | 必須 | 必須 |
| UAC1 playback/capture | 必須 | 必須 | 必須 | 任意 | 必須 |
| begin/end/resource | 一部 | 必須 | 必須 | 必須 | 必須 |

## CI変更

追加するjob:

- pure host descriptor/model tests
- S3 compile
- P4 compile
- forbidden dependency/symbol scan
- third-party license/provenance check
- firmware size regression check

実機jobを自動化できない間は、release checklistでGate 3以降の実機結果を必須artifactにする。

## 主なリスクと対策

### Arduino coreのTinyUSB symbolが混入する

対策:

- 旧helperを一度に削除する。
- link mapと`nm`をCI検査する。
- fallbackを作らない。

### TinyUSB sourceをvendorすることで保守範囲が増える

対策:

- upstream commitを固定する。
- local patchを最小化し、patch一覧をmanifest化する。
- upstream更新を通常の機能変更と分ける。

### P4のcache/DMA問題がFSテストでは見つからない

対策:

- HS bulk/isochronousの連続転送を専用Gateにする。
- alignment、cache sync、buffer ownershipをruntime APIに閉じ込める。

### Audio descriptorが列挙するがstreamingで破綻する

対策:

- descriptor testとdata-plane testを別にする。
- bandwidth、feedback/clock、alternate setting、underrunを個別に観測可能にする。
- speaker、microphone、duplexを順番に実装する。

### 移行中にdefault branchが長期間使えなくなる

対策:

- v1 maintenance branch/tagを先に固定する。
- v2は長期rewrite branchで作業し、中間commitのcompile failureを許容する。
- Phase Gate到達時だけbuild/test結果をcheckpointとして記録する。
- default branchへmergeする場合はPhase 8 Gateを満たした単一cutoverとする。
- 途中状態をdefault branchへ置く場合は、CIを一時無効化して成功に見せず、
  どのGateまで未達かを明記する。

## 実装開始前のチェックリスト

- [x] この移行計画のフェーズ分割と順序を承認
- [x] v2初期対応coreを3.3.11に固定する
- [x] TinyUSB source snapshotを同梱する
- [x] 既存APIは自然な場合だけ維持し、単純化を優先する
- [x] Audioのv1互換shimを作らない
- [x] Audio v2初期上限を承認
- [x] Espressif由来Audio sourceを削除し独立実装する
- [x] v2移行中の一時的なcompile failureを許容する
- [x] Phase 0の実機baselineを取得できるboard構成を確認

承認後の最初の作業はPhase 0であり、runtimeや公開APIの変更ではない。
