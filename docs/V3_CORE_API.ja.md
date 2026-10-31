# v3 共通coreとEP0

この文書は、全Functionが共有するUSBの土台（core、EP0、controller、DWC2）について書く。

- [現在の仕様](#現在の仕様)：いま有効な決定。利用者とコード保守者はここを読む。
- [未解決の事項](#未解決の事項)：決めていない点、確かめていない点。
- [経緯の記録](#経緯の記録)：段階ごとの変更理由と実測。当時の「未完了」は現在の状態ではない。

開始modeとworker設定の使い方は[移行ガイド](V3_MIGRATION.ja.md)、
借用転送の契約は[Vendorガイド](V3_VENDOR_API.ja.md)を参照する。

## 現在の仕様

更新：2026-10-31。

### 構成

TinyUSBの`device/usbd.c`は使わない。USBの処理は本体内の次の層で行う。
旧coreへ切り替える仕組み（二重backend）は持たない。

| 層 | 主なファイル（`src/internal/`） | 役割 |
| --- | --- | --- |
| core | `EspUsbDeviceCore.*` | EP0、標準要求、event queue、世代の管理 |
| runtime | `EspUsbRuntime.*` | PHYの取得、controllerの選択、USB workerの起動 |
| controller境界 | `EspUsbController.h` | coreとDWC2の間の型付きの入口 |
| DWC2 | `EspUsbDwc2Device.c`、`EspUsbDwc2Phy.c`、`EspUsbDwc2Registers.h`、`EspUsbDwc2Hardware.h` | register、FIFO、DMA |
| platform | `EspUsbDwc2Platform.*` | SoCごとのport表、IRQ、cache同期 |
| wire型 | `EspUsbWire.h` | SETUP（8 byte）、interface（9 byte）、endpoint（7 byte）のlayout |

- DWC2はTinyUSB由来（MIT/ST BSD）の保守forkで、本体で保守する。自動のvendor更新は終えた。
  clean-roomで書き直したものではない。[由来と保守方法](../third_party/tinyusb/PROVENANCE.ja.md)。
- 内部headerは利用者APIではない。TinyUSB内部APIに直接依存するコードは、v3では動作を保証しない。
- 旧build flag（`CFG_TUD_*`など）は`EspUsbLegacyFlags.h`でcompile errorにする。0を指定しても拒否する。

### 対応するcontroller

`EspUsbDeviceConfig::controller`の既定は`Auto`。runtimeがport表から選ぶ。

| SoC / port | 速度 | endpoint番号 | 非control IN endpoint | FIFO（32 bit word） |
| --- | --- | --- | --- | --- |
| ESP32-S2 / S3 | FS | 0〜6 | 4本 | 256 |
| ESP32-P4 FS port | FS | 0〜6 | 4本 | 256 |
| ESP32-P4 HS port | HS（480 Mbps） | 0〜15 | 7本 | 1024 |

- `Auto`はHS portがあればHSを選ぶ。P4ではHS、S2/S3ではFS。
- `FullSpeed`/`HighSpeed`を明示すると、その速度のportを選ぶ。該当するportがなければ`ESP_ERR_NOT_SUPPORTED`。
- 未知のSoCを既存のportへ暗黙に割り当てない。S31は現在のSDKでは未対応。
- 表の数は`src/internal/EspUsbDwc2Platform.cpp`の`espusb_dwc2_ports`。上の「非control IN」はEP0を除いた数。
  descriptorの検査、bulk二重buffer、ISO IN FIFOの予算は、すべてこの表を参照する。
- 上限を超える構成は、`begin()`が`ESP_ERR_INVALID_SIZE`で拒否する。PHY・IRQ・workerは開始しない。

HSで変わる値（2026-10-31、P4/P4XのHS直結で確認）：

- bulkのMPS（最大packet長）は512になる。FSでは64。
- MSCの`transferBytes`はbulk MPSの倍数でなければならない。HSで64や256を指定すると`begin()`が`ESP_ERR_INVALID_SIZE`。
- CCIDの明示`messageBytes`がBulk-OUT MPS未満なら、`begin()`が`ESP_ERR_INVALID_SIZE`。既定0（自動）は`max(271, MPS)`になる。
- 借用RX（`readDirect`）の容量はMPSの倍数が必要。HSでは512の倍数。
- CDC/Vendor/MIDIの`transferBytes`はMPS未満でも受け付ける。
- HIDのinterrupt endpointはHSで1024 byteまで使える。

詳しい記録は[P4の機種差とbench](V3_P4_VARIANTS_AND_BENCH.ja.md#hsで変わる値)。

### 開始時に確保する資源

```cpp
EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
EspUsbDeviceConfig config;
config.eventQueueDepth = 16;  // 既定16。0は不可
if (!device.begin(config)) {
  // device.lastError()で、設定の誤りとメモリ不足を区別する。
}
```

- `begin()`で、core状態、選んだ構成のinterface/endpoint表、event queue、EP0のDMA領域、
  DWC2の転送recordを**一つの内部DMA領域**にまとめて確保する。`end()`でまとめて返す。
- 転送・取消し・過負荷からの回復の途中では、heapを確保しない。
- DWC2の転送recordは、全alternateのdescriptorに現れるendpoint方向の数とEP0の2方向分だけ作る。
  使わない方向のrecordは作らない。
- EP0のSETUP用64 byteは、metadataと別のcache lineに置く。
- 確保に失敗したら`ESP_ERR_NO_MEM`。途中まで確保した資源は戻す。
- Functionを一つも登録しない`begin()`はcontrollerを起動せず、DWC2の領域も確保しない。

`eventQueueDepth`の意味：

- 割込みから受け取ったeventを一時的に保持する数。処理速度や待ち時間の保証ではない。
- 型は`uint16_t`、0は拒否。大きい値は確保失敗（NoMem）になり得る。
- 指定した数とは別に、hardware event用の予備を1件確保する。
  理由：深さ1のとき、標準要求のfenceがEP0 statusの枠を奪って再列挙が起きたため。
- 小さくするとRAMは減るが、同時に多く転送する構成ではqueueがあふれやすい。

### EP0と標準要求

- EP0はSETUP・DATA・STATUSの段階を区別して扱い、受信した実byte数をclassへ渡す。
- 新しいSETUPが来たら、古い要求のdataを新しい要求へ混ぜない。SETUPの通し番号とbus世代で区別する。
- 標準要求（device/interface/endpoint）は、方向・値・index・長さを検査する。
  advertiseしていないremote wakeupや、EP0・ISO endpointへのHALT設定は拒否する。
- 未対応のconfiguration値への要求は拒否するが、動いている構成は解除しない。
- SET_CONFIGURATIONで解除または同じ値を再選択したときは、DMAを止め、借用bufferを返し、classを初期化し直す。

`sendControlResponse(..., 0)`の意味：

| Hostの要求 | 長さ0の応答 |
| --- | --- |
| IN、要求長が1以上 | 長さ0のDATA段階（ZLP）を送る |
| 要求長0（no-data） | STATUS段階だけを返す |

理由：以前は「長さ0なら常にSTATUS」だったため、要求長のあるINでHostがDATAを待ち続けた。
要求より短い応答も正常に終わる。OUTで受信領域が要求長より小さい場合は拒否する。

### event、世代、所有権

- classの完了通知、reset、end通知はUSB worker（FreeRTOS task）で処理する。SOFだけはISRから呼ぶ。
- **queueがあふれた場合**：成功した完了eventを捨てると借用bufferが戻らなくなる。
  そこでDMAを止め、classをresetしてbufferを返し、古いeventを回収してから、切断・再接続する。
- **構成世代**：class全体をresetするたびに世代を進める。古い世代の完了eventは、endpointの状態を変える前に捨てる。
  同じendpoint番号を再利用しても、古い完了が新しい借用を返すことはない。
- **reset開始**：USBRSTを受けた時点で、coreのbus世代を進め、構成・接続・suspendを無効にする。
  workerがreset処理を終えるまで、新しいjobやDMAの登録を受け付けない。
- **再構成前のDMA停止**：`close_all`は有効なendpointの停止を待ち、残った転送完了割込み（XFRC）も消す。
  endpoint割込みとtask側の操作は同じhardware lockで同期する。
- deferred jobやfenceの予約はblockしない。満杯ならworkerのjobから再試行する。

### IRQとcache

- IRQは`begin()`で一度だけ登録し、`end()`で解放する。稼働中の停止・再開はmask/unmaskで行う。
  登録に失敗したら`begin()`は`ESP_FAIL`を返す。
- P4では、DMAを始める前にCPU cacheを同期する（S2/S3はcacheがないので何もしない）。
  - IN：送るdataをcacheからmemoryへ書き戻す（clean）。
  - OUT：受信領域のdirtyな行も書き戻す（clean）。理由は下記。
  - 開始時：SETUP領域をclean→invalidateしてから、DWC2へ渡す。失敗したら`ESP_FAIL`で、次の`begin()`で再試行できる。
- OUTのcleanは`09470d6`（2026-10-31）で追加した。
  P4XのHS借用RXで、DMAの途中に追い出されたdirty行が受信dataを上書きし、65535 byte中9216 byte以降が壊れた。

### ISO OUTの取りこぼし対策

DWC2のISO OUTは、armするときに「次の(micro)frameが偶数か奇数か」を指定する。
Hostが1 intervalを空けて送ると、この偶奇が合わず、incomplete ISO OUT割込みになる。

- 現在（`aade7e0`、2026-10-31）：ISRの中で、同じDMA bufferのまま偶奇を反転してarmし直す。
  受信dataはないので、古いdataを送る心配はなく、回数の上限も設けない。
- 以前（`4480513`）：DMAを止めて失敗の完了をworkerへ返し、workerがarmし直していた。
  HSでは125 µsのmicroframeに間に合わず、次のpacketも失っていた。
- ISO OUTを開いたときだけ、incomplete OUT割込みを有効にする。EP0、bulk、interrupt OUTは対象外。

### VBUSの検出

`EspUsbDeviceConfig::vbusSensing`（既定false）。

| 値 | 動作 |
| --- | --- |
| false | B-session validを強制する。検出回路のないboard向け（従来の動作） |
| true | boardのhardware VBUS-valid信号を使う |

- trueは、board側に検出回路と信号の配線がある場合だけ選ぶ。GPIOや回路を自動で用意するものではない。
- 変更は`end()`の後、次の`begin()`の前に行う。
- 旧`CFG_TUD_VBUS_DETECT_HW`などのbuild flagは拒否する。

```cpp
EspUsbDeviceConfig config;
config.vbusSensing = true;  // 検出回路と配線があるboardだけ
if (!device.begin(config)) { /* lastError()を処理する */ }
```

旧P4（v1.3）のHS口では、BSesVldがVBUSの有無を示さなかった（[記録](V3_P4_VARIANTS_AND_BENCH.ja.md#hs直結の列挙とvbus2026-10-31)）。
実ケーブル抜去によるVBUS検出の試験はしていない。

## 未解決の事項

決めていない点、確かめていない点。

- 新しいSETUPと古いstatus/dataの到着順の全組合せ。OUTの物理的なshort/overrun。
- hardware故障時のDMA停止待ちのtimeout。
- IRQのmask/unmask自体が失敗する場合の故障注入。
- 進行中のSET_CONFIGURATION/SETUP/reset/endの全順序。
- 実機での確認範囲：S3–S3（FS）、P4の両controllerのFS loopback、P4/P4XのHS直結、P4X–Linux PC。
  S2は実機未確認（compileのみ）。S31は未対応。Windows/macOSは未確認。
- 実ケーブル抜去によるVBUS検出（`vbusSensing=true`）。

要求の基準は[USB-IF USB 2.0 specification](https://www.usb.org/document-library/usb-20-specification)。
実機試験は独立した`validation/`と標準pluginのcomboを使い、旧test/sampleを合否基準へ戻さない。

## 経緯の記録

以下は段階ごとの実装と実測の記録で、日付順に並ぶ。各節の「未完了」「残り」「暫定」は当時の状態を示す。
現在の仕様は[上](#現在の仕様)を正とする。
現在のbackendは本体内で保守し、privateの投稿口は32 bitの総長を扱う。

### 独自coreの初回（2026-10-26〜27）

TinyUSBの`device/usbd.c`をビルド対象から外し、`EspUsbDeviceCore`へ置き換えた。
以前の固定interface map、EP0 staging、全構成向けのevent queue static領域を廃止し、begin時の一つの領域へ移した。
代償としてbegin時の確保失敗点が増えるため、失敗と回復を試験対象に加えた。
借用転送の実効契約と費用は[Vendorガイド](V3_VENDOR_API.ja.md#65535-byteを跨ぐ借用転送)、
現在の作業状態は[引き渡し](V3_HANDOFF.ja.md)を参照する。

初回の独自coreではVendor、descriptor、複数Videoの関連3件が成功した
（`core-initial.xml`、253.37秒、serial 6本STOPPED、panicなし）。

### 共通coreの関連検証

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

新SETUPと旧完了の全到着順、OUTの物理的なshort/overrun、suspend/resume・remote wakeup、
Vendor/CDC以外のクラスの転送中SET_INTERFACE・HALT/CLEAR_FEATUREでのPID/所有権回復は追加検証・実装の対象とする。
特にaltを実装しないクラスへの共通fallbackは現在alt 0のACKを扱い、
Vendor/CDCでは実際のdata interfaceとendpointを対応付けて回復する。他クラスの転送中再選択は未完了。
DWC2、残存helper・protocol定義、内部ABI、他SoC/HS、PC/OSの確認は未完了。

要求の基準は[USB-IF USB 2.0 specification](https://www.usb.org/document-library/usb-20-specification)。
実機試験は独立した`validation/`と標準pluginのcomboを用い、旧test/sampleを合否基準へ戻さない。

### 独自coreでの全体試験

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

### Vendor/CDC標準要求の補強

SET_INTERFACEとHALT/CLEAR_FEATUREで転送を停止し、fence後の返却とDATA PIDの回復を行う。
詳細とcopy queue破棄の影響は[Vendorガイド](V3_VENDOR_API.ja.md#標準要求による転送の再初期化)。
指定interfaceをendpoint metadataに保持し、CDCのcontrol/data interfaceと複数Functionを区別する。
関連10件と最終4件のS3–S3試験は[試験記録](../validation/README.ja.md#vendorcdc標準要求の回復)を参照。
全体27件の記録はこの補強前の実装に対する結果であり、現在の全28件を再実行した結果ではない。

### Vendor controlの公開actualBytes

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

HIDの標準要求による方向別回復も追加済み。公開APIの影響と関連4件の実測は
[HIDガイド](V3_HID_API.ja.md#標準interfaceendpoint要求による回復)を参照。
共有bulkを使うMIDIはVendor/CDCと同じ回復経路に入るが、MIDI固有の標準要求試験はまだ追加していない。
他クラスも個別の所有権・protocol契約と検証を継続する。

MSCのSET_INTERFACEも独自recovery fenceへ接続済み。不正CBWのBOT reset要求と区別する。
[MSCガイド](V3_MSC_API.ja.md#interfaceの再初期化とbot回復)にAPIの影響と関連3件の実測を記録した。
任意Host HALT中のcommand継続、他instanceの同時進行、全競合順序を完了したという意味ではない。

### DWC2の転送状態をcoreの開始時領域へ移す

DWC2の全endpoint向けstatic転送配列、常駐controller stateとSETUP bufferを撤去する。
独自coreがbegin時の一つのDMA領域にDWC2 storageも含め、deinit・割込み停止の後に解放する。
DWC2用の別heap取得点は増やさない。開始時に確保できなければ既存のNoMem経路で失敗する。

descriptorの全alternateに現れるendpoint方向をmaskへ集め、EP0のIN/OUTを加えた数だけ
転送recordを確保する。OUT番号が1/3、IN番号が1/2/3/4のような疎な構成も扱う。
slotはOUT群・IN群へ詰め、未選択方向のopen/transferを拒否する。
controllerが持つ物理endpointの停止・IRQ clearは引き続き全体に行う。
SETUP DMAの64 byteはmetadataと別のcache lineへ置き、metadataを巻き込むinvalidateを避ける。

FunctionなしのDeviceは従来からbackendを起動せず、DWC2の確保も0。
DFU RuntimeだけならEP0の2方向、HID INのみなら3、Vendorなら4、着脱可能な2 CCIDなら8 recordとなる。
alternate切替中のheap取得はなく、bus resetでも選択済み領域を初期化して再利用する。

公開APIは変えない。代償はbegin時の領域増加と、endpoint lookupにmaskの計数が入ること。
全機種分の常駐費用を持つ形から、稼働中構成が費用を払う形へ移す。
FIFO予約量・hardware register操作・暫定C ABIとprotocol型は今回維持し、TinyUSB依存の全撤去とはしない。
P4の初回SETUP DMA前のcache同期、HS・他SoCの実機確認も個別に検証する。

S3実測ではDWC2領域がEP0のみ128 byte、Vendor（4 record）192 byte、
着脱可能な2 CCID（8 record、疎なOUT配置）256 byte。metadataからSETUP DMAを隔離するため、
領域全体を64 byte単位へ丸める。record数の違いが同じ確保byte数になる場合もある。
これは現在の領域の測定で、v2との速度比較や全機種での同じサイズを保証する値ではない。
S3/S2のnmで旧xfer_status・_dcd_data・_dcd_usbbuf symbolの不在とruntime storage経路の存在を確認した。

### DWC2 runtime storageでの全体試験

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

### 初回SETUP DMAのcache引渡し

開始時に初期化したSETUP領域はCPU cacheがdirtyな可能性があるため、hardware初期化前に
clean、invalidateを順に実行する。既存のbackend hookを使い、cacheのないS3/S2ではno-opとなる。
metadataと別の64 byte lineだけを同期し、成功した後にDWC2 runtime pointerを公開する。
失敗時はcontrollerを起動せず、coreの領域とqueueを返してESP_FAILで開始を失敗させる。
NoMemとは区別し、次のbeginで再試行できる。

S3専用probeではno-op hookを置換し、clean失敗とinvalidate失敗を一つずつ注入する。
どちらも未公開・未回収0で戻ることと、通常hookへ戻した後の実通信を確認する。
これはP4でcache coherencyを実測した証拠ではなく、P4/HSの実機試験は残る。

### DWC2初回cache引渡しの補強

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

### 構成を跨ぐqueue内の完了event

SET_CONFIGURATIONで同じ値を再選択しても、endpointとclass transportを初期化し直す。
この間に古い完了eventがqueueへ残ると、再openした同じendpoint番号の新しい転送へ適用され得る。
coreはclass全体のresetごとに構成世代を進め、完了eventの投入時にその世代を記録する。
non-control endpointの古い世代は、Busy/Claimedを変更する前に捨てる。
EP0は従来のSETUP serial/bus世代で扱い、deferred fenceの後片付けは構成世代だけでは捨てない。

S3の新しいconfiguration_epoch試験はworkerだけを止め、実Hostの構成再選択SETUPの後ろに
新規投稿jobと古いTX/RX完了を並べる。修正前はHostのdata転送前に新規借用がSuccessで返り、
RXは未受信のbuffer内容を成功結果として通知した。修正後は実data完了まで保持する。

公開APIやborrowed bufferの契約は変えない。eventごとにuint32_t一つとcore側の世代を加えるため、
begin時のqueue領域は増える。転送時のheap取得点は増やさない。
これは投入済みeventを再構成後に適用しないための補強であり、DCDの遅れたISR/latched IRQや
DMA armごとの識別をすべて解決したものではない。そこは独自DCDへの移行と合わせて検証する。

### 再選択した構成への古い完了event混入

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

### 再構成前のDMA停止とlatched IRQ

queueに投入済みのeventだけでなく、hardwareに残った旧XFRCも区別する必要がある。
S3でSET_CONFIGURATIONのSETUP後にDCD IRQを止め、旧INを実Hostで完了させた。
SDK registerのxfercompl=1を確認した後、再構成・新規armを行ってIRQを再開すると、
修正前は新規TXがHostの新しいINより早くSuccessで返った。
この完了は再構成後にISRが投入するため、構成世代filterだけでは識別できなかった。

DWC2 close_allは有効endpointの停止完了を待ち、非制御endpointのactive/interrupt状態と
転送recordを解放してから再openを許す。EPENAがすでに0でも残ったXFRCを消す。
IRQ処理はDAINTMSKを反映し、endpoint IRQ・ISO不完全転送処理とtask側endpoint操作を
同じhardware lockで同期する。SOFのclass callback呼出しはlock外に保ち、逆順のlock取得を避ける。

新しいconfiguration_epochはqueue旧完了と実latched IRQを交互に実行する。
公開API、転送時heap、確保点数は変えない。代償は再構成時の実DMA停止待ちと、
endpoint処理中のcritical section増加。hardware故障時の停止待ちtimeoutや全EP0到着順などは別の検証対象とする。

### DWC2停止・IRQ同期の全体確認

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

### native class登録表の独自化

独自coreは選択されたclassだけを`UsbClassDriver`として登録し、C++の
`selectedUsbClassDrivers`から借用する。TinyUSBの`usbd_class_driver_t` layoutと
`usbd_app_driver_get_cb` hookは使用しない。coreが呼ばないname/xfer_isr枠も除き、
32bit環境では登録tableが1 driverあたり8 byte小さくなる。取得点数や転送中heapは増やさない。
classの完了/controlはworker、SOFは従来どおりISR。登録/解除はcore停止中という契約を維持する。

native class用のendpoint宣言は`internal/EspUsbDeviceEndpoint.h`へ限定した。
classから`device/usbd_pvt.h`を直接includeせず、vendorのOS/FIFO/class tableを引き込まない。
coreと残存DWC2 adapterは引き続き同headerを使う。endpoint/controlのC symbol名、
wire typeやprotocol headerの依存は残り、全ABI撤去や全TinyUSB撤去とは区別する。

利用者APIと移行手順は変わらない。不利益は段階移行中のendpoint宣言が残存bridgeの宣言と
重なること。coreのtranslation unitで両headerをincludeし、型の不一致をcompile errorにする。
両方の意味を変えるときはまとめて変更する。driverの任意ISR dispatchは公開しない。

### class登録表独自化後の全体確認

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

### controller境界と未使用のvendor基盤の撤去

`internal/EspUsbController.h`がcoreと残存DWC2 adapterの専用境界となる。
型付き`EspUsbControllerConfig`でbulk INの二重bufferとVBUS policyを渡し、initはdevice専用の
port/highSpeedを受け取る。旧DCD symbolとvendor config ID/unionは使わない。
controller eventは独自型とし、deferred fenceはcore内の予約ID 255で区別する。
controller入口は無効/範囲外eventを拒否し、worker内のjobだけがdeferred payloadを使う。
control ACK observerも独自coreから専用hookへ通知する。

使われない`usbd_*`互換entry、vendor DCD/private、OS/FIFO、umbrella headerを撤去した。
ESPは従来同様internal DMAで動かし、無効だったslave/FIFO経路とrecordのFIFO pointerを削除する。
controllerにinternal DMAがなければ初期化を明示的に拒否する。新SoCの非DMA経路を追加する場合は
専用実装が必要だが、今回対応するS2/S3/P4の転送方式は変えない。
転送時のheapや確保点数は増やさず、recordから32bit pointer一つを減らす。
最終storage量は既存の64byte丸めとdescriptor選択数に従い、必ず4byte単位で減るとは限らない。

利用者API/移行手順は変わらない。内部TinyUSB APIへ直接依存する非公開利用は対象外。
故障注入probeは独自header/symbolへ変更し、queue深さ・cache失敗・旧完了/latched IRQの条件を保つ。
型や名前を変更しただけでDWC2が独自実装になったとは扱わない。MIT由来は維持し、
vendor選択は24→17 files、compiled sourceは2本のまま。wire/protocol定義、device向け残存headerと
DWC2 register/port処理の置換は続く。

### controller境界移行後の全体確認

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

### native bus APIとruntime CDC/MIDI記述子

class/core間の宣言を`EspUsbDeviceBus.h`と`EspUsbDeviceEndpoint.h`へ限定し、
内部symbolを`espusb_device_*`へ移した。vendor `device/usbd.h`の旧init/deinit/task pump、
固定queue深さ16の互換入口、使われないuser SOF/suspend/resume hookは残さない。
classのSOF callbackは従来どおりISRから呼び、借用bufferの完了/取消しはworkerで返す。
control ACK observerはcontrollerからのhookではなく独自EP0 engineのbus契約へ移した。
通常のDevice/Function/config/begin、公開control observerや送受信APIに変更はない。
旧TinyUSB内部APIへ直接依存する利用はv3で継続保証しない。
この内部symbolの分離は、同じcontrollerで二つのUSB stackを同時稼働できるという意味ではない。

CDCはIAD/control/data/endpoint descriptorをinstanceのinterface/string/endpoint番号から直接生成する。
MIDIは1～16 cableの方向別jack graphをruntime設定から生成する。入力/出力のcable数が異なっても、
必要なjackだけを生成し、MS長を最初から確定する。旧macro結果への長さ・文字列番号の後書きをなくす。
対称1/16、16対1、複数instance、FSの既存通信試験を維持し、public配置とwire layoutは保つ。
既存DescriptorBufferへ書き込み、記述子生成中のheap取得点を追加しない。

利点はclass設定がvendor descriptor macroの前提から離れ、不要な定義や旧stackの入口も減ること。
代償はCDC/MIDI descriptorのfield定義を独自側で保守すること。MITの由来はPROVENANCEへ記録する。
利用者の移行操作はない。private TinyUSB hookを使っていた独自拡張は見直しが必要となる。
Audio/CDC/MIDIとdevice headerを撤去し、vendor選択は17→13 files、compiled sourceはDWC2の2本を維持する。
wire type、HID table、DWC2 register/port/sourceとcapability定義は引き続き置換対象。

### native busとCDC/MIDI記述子移行後の全体確認

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

### MIDI逆向き非対称cableの追加確認

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

### native USB wire型とruntime VBUS policy

`internal/EspUsbWire.h`がSETUP 8 byte、interface 9 byte、endpoint 7 byteのlayoutと
byte単位のLE読み取りを定義する。request type/recipient、endpoint typeはmaskで読み、
vendor bitfield・type alias・config/compiler headerをnative core/classへ引き込まない。
controller speed/resultはwire byteではなく内部enum。公開の借用結果型EspUsbTransferResultは維持し、
低位の完了結果は別のEspUsbControllerResultへ分ける。C/C++双方でsizeof/offsetをcompile時確認する。

DWC2境界のdescriptor/requestも同じ独自型へ移した。残存DWC2内のregisterと一般helperは
まだvendorに依存するため、完全撤去とは区別する。US keyboard tableは必要なlookup行だけを
MIT表示付きで保持し、最後のvendor HID headerを削除した。表の値と基本keyboard APIは変えない。
vendor選択は13→12 files、compiled sourceはDWC2関連2本のまま。

`EspUsbDeviceConfig::vbusSensing`を追加した。既定falseは従来どおりB-session validを強制し、
trueはboardのhardware VBUS-valid判定を使う。既存のselfPowered、消費電力の記述とは独立する。
変更はend後、次のbegin前に行う。board側の検出回路と信号routingが必要であり、
このboolはGPIOの割り当てや検出回路を自動追加しない。実ケーブル抜去による検出は別途の実機検証対象。
旧CFG_TUD_VBUS_DETECT_HW/DEFAULTによる指定は明示拒否し、APIへ移す。

```cpp
// 旧: build_opt.h / build flagsでCFG_TUD_VBUS_DETECT_HW=1
EspUsbDeviceConfig config;
config.vbusSensing = true;  // 検出回路・routingがあるboardだけで選ぶ
if (!device.begin(config)) { /* lastError()を処理する */ }
```

利点は同じbinaryでpolicyを選べることと、classの型がvendor configから離れること。
代償は独自layoutとlookupの保守、configのbool一つ、旧private type/build flag依存利用の修正。
公開の要求field・sendControlResponseなどの操作は維持する。rawRequestをTinyUSB型へcastする拡張は
v3で継続保証せず、公開fieldと返信APIを使う。転送時heapや新たな取得点は増やさない。

### native USB型とVBUS設定の全体確認

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

### ESP DWC2 platformとIRQ所有の整理

DWC2から汎用TinyUSB common/compiler/MCU/optionsと旧ESP port shimを撤去した。
`EspUsbDwc2Platform`がSoCの物理port情報、USB_WRAP、cacheとIRQを管理し、
`EspUsbDwc2Support`は必要な整数/layout操作だけを持つ。共通core/classもDWC2も
TinyUSB configをimportしない。旧build flagは`EspUsbLegacyFlags.h`で拒否するだけで、
class数・buffer・queue・VBUSを設定する別経路にはしない。0指定も不要なので撤去対象。
vendor選択は12→4 files。残る2 C sourceと2 register/adapter headerにはPHY reset/turnaroundと
endpoint registerアルゴリズムがあり、これらの依存撤去はまだ完了していない。

IRQは一つのactive controllerが一つのSDK登録を所有し、begin時だけ取得する。
稼働中のdisable/enableはmask/unmaskで再利用し、end時はISRを停止・登録解放してから
controller/core storageを回収する。従来は登録/解放を繰り返し、登録結果を検査していなかった。
登録失敗はbeginのESP_FAILと既存rollbackへ渡す。回復時のunmaskに失敗した場合は
producer jobを再開せず、既存overflow回復を再試行する。
試験用にSDK割り込み取得境界をprivate weak関数に分け、製品のtest flagは追加しない。
mask中もSDK handleを保持するため、その資源はendまで生存する。代わりに再取得の失敗と
稼働中のIRQ登録用heap取得を避けられる。mask/unmask SDK自身の故障注入は今回の対象外。

RuntimeのAuto/FS/HS選択はportの能力情報から決める。S2/S3はFS、P4 AutoはHS、FS明示はport0を維持。
HSがport1とは仮定せず、将来HS-onlyのport0にも拡張できる。S31は現SDKでは追加していない。
未知SoCを暗黙の既存portへ割り当てず、未対応として扱う。PHYの取得/電源は引き続きESP-IDFが所有する。
P4はSDK cache capabilityと64byte lineを確認し、strongなmsync実装を使う。S2/S3はweak no-opを維持。
legacy Host/slave/他MCUやcompliance test modeの未公開分岐は持ち込まない。
公開Device/Function API・borrowed契約・新規移行手順は変わらない。

### ESP platform移行後の全体確認

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

### DWC2を本体で保守する段階へ移行

2026-10-28。残るvendor 4 fileを`internal/EspUsbDwc2Device.c`、`EspUsbDwc2Phy.c`、
`EspUsbDwc2Hardware.h`、`EspUsbDwc2Registers.h`へ整理し、自動vendor更新を終了した。
これはMIT/BSD由来の保守forkで、clean-roomで全アルゴリズムを書き直したという意味ではない。
[由来と保守方法](../third_party/tinyusb/PROVENANCE.ja.md)を参照。
register headerは2297→491行とし、Deviceで使うview/constantsだけを残す。
26箇所のoffset/stride assertとS3 SDK構造との主要offset比較を追加した。

周期endpointのinterval計算も整理した。従来のHS全endpointへの指数計算はbulkの
bInterval=0で負のshiftを起こし得た。FS ISOの指数解釈と、256以上の値の切捨ても修正した。
不正な周期値はFIFO確保やendpointの変更前に拒否する。counterをuint16_tにしたが、
32bit targetのtransfer recordサイズは元のpadding内に収まり16 byteのまま。
FS interruptの範囲とHS指数表現は[USB-IF CCID仕様](https://www.usb.org/sites/default/files/DWG_Smart-Card_CCID_Rev110.pdf)、
FS ISOの指数表現は[Microsoft公開仕様](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-gipusb/238f579d-fce4-47ee-8ee1-5651c59ff563)も参照した。

利点はruntime構成とhardware処理を同じ保守境界で変更でき、vendor更新時のpatch再適用が
不要になること。不利益はupstream hardware修正の追跡・選別を自分たちで担うことと、
従来通っていた不正descriptorが拒否されること。公開APIの追加変更はない。
control_coreの単独試験は1 passed、32.31秒。14 interval条件と不正open 64回の後の
正常open、heap回復を確認した。最初のfixtureはbus reset前のFIFO未初期化で失敗し、
private probeでFIFO初期化を明示して修正した。HSの算術確認とHS実通信の確認は区別する。
全体試験結果は末尾を参照。

### native DWC2整理後の全体確認

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

### class HALT整理後の全33件確認

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

### reset開始時の世代無効化

2026-10-28。実USBRST IRQの入口ではmounted=1だった構成が、元のIRQ処理の直後も
mounted=1/job-ready=1で残ることを再現した。ENUMDNEまでcoreへresetを伝えていなかったためである。
closed endpointのmax_size=0 guardは存在するが、それだけでは旧jobやendpoint再登録を拒否できない。

USBRSTのhardware lock内でcoreのbus世代とresetting flagを更新し、構成・接続・suspendを無効にする。
ENUMDNEからの最終BUS_RESET eventは引き続きworkerで扱い、driver resetと借用buffer返却が終わるまで
新しいjobを実行しない。開始と終了で内部世代は進むが、classのBusReset返却を二重には発行しない。
coreのclaim/open/xferと、DWC2のopen/ISO予約・有効化/xfer/abort/SET_ADDRESSは同じready条件を使う。
DWC2側はhardware lock内で再確認し、reset処理と新しい登録が競合しないようにする。
DMA停止と全endpoint closeによるcleanupの入口は維持する。

公開APIは変更しない。利点はreset開始から古い構成で送信できる時間をなくすこと。
代償はreset中の受付拒否と各hardware操作での短い状態確認であり、追加task/queue/bufferは確保しない。
実resetの観測はprivate IRQ allocatorを使う試験内のwrapperで行い、IRQ内で待機やログ出力はしない。
Host/Deviceの通常のstart/stopと、試験sketchが明示するworker park/releaseだけを使う。

修正前は1 failed（288.91秒）、mounted=1/jobs=1を確認した。
修正後はqueue深度4/16/31の3 resetで、mounted=0/jobs=0、endpoint再登録・data DMA・EP0 DMAの拒否、
各2旧loanの一度だけのBusReset返却と各2新転送が成功。1 passed、0 skipped/unfinished、119.10秒。
2 serial全てSTOPPED、panicなし。原本`/tmp/pytest-embedded/2026-10-28_07-43-39-489897/`、
コピー/JUnit/transcriptは`validation/.pytest-results/reset-start-guard*`、集計は`reset-start-guard-audit.txt`。
初回probeのheader不足によるcompile errorと、実IRQでの製品回帰は区別して保存した。
任意の進行中SET_CONFIGURATION/SETUP/reset/endの全順序までを実証したわけではない。
全体34件の結果は次節を参照。

### reset開始guard後の全34件確認

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

### v3開始modeとworker設定

library metadata/headerを3.0.0へ変更した。release/tagは作らず、基本のbegin/Function操作は維持する。
startTinyUsbを型付きstartModeへ置換し、DescriptorsOnlyとUsbを区別する。空のtask()入口は削除した。
started()は資源所有、ready()はHost構成済みを表す。内部worker名もEspUsbRuntime/UsbRuntimeへ整理した。
workerのtaskStackBytes/taskPriorityを公開し、taskCoreId/eventQueueDepthと同じbegin configで設定する。
既定4096 byte・最高task priority・affinityなしは維持し、priority -1を既定指定とする。
範囲外値はPHY取得前に拒否する。DescriptorsOnlyではworkerを使わず、その設定値も適用しない。
[API移行ガイド](V3_MIGRATION.ja.md)に理由・影響・呼出し例を記載した。

開始modeの変更後はcontrol_core/Vendorの2 passed（432.33秒）。worker設定とstartedの補強後は
control_core 1 passed（89.59秒）。metadata-onlyでの資源有無、invalid enum/stack/priority/core、
4096/8192 byte設定と実workerのpriority/core、通常EP0と実通信を確認した。
初回worker fixtureのinitializer_list型不一致によるcompile errorも区別して記録する。
このAPI整理後の全34件とS2/P4 compileは次に実行する。

### 開始mode・worker API整理後の一括確認

製品`d876d50`を固定した全34件が4651.38秒で成功した。0 skipped/error/failure/unfinished。
DescriptorsOnlyの無worker、不正enum/stack/priority/coreの拒否、実priority/core、
startedとready、終了後の回収を同じ実行で確認した。
68 serial全てSTOPPED、panicなし。774確保点の失敗/回復とheap前後同量。
S2/P4 compileと全29 sourceの依存・symbol確認も成功。
記録は[validationの最新全体結果](../validation/README.ja.md#開始modeworker-api整理後の全34件)。
他SoC/HS実機・OS・任意の全EP0競合順序までの確認とはしない。

### ISO OUTの欠落frame回復

> 2026-10-31の`aade7e0`で、この方式（DMAを止めて失敗をworkerへ返す）は、ISR内で偶奇を反転して再armする方式に置き換えた。
> 現在の動作は[ISO OUTの取りこぼし対策](#iso-outの取りこぼし対策)を参照。

S3の間欠ISO OUTでHostの送信は継続するのにDeviceは最初の96 byteだけ受信した。
観測値はGINTSTS=00a08428（incomplete OUT bit21）、mask=801c3804（bit21無効）、
OUTCTL=80048064、OUTSIZE=00080064、OUTINT=00002000。baselineは
`.pytest-results/audio-alternates-out-gap-{baseline,registers}*`へ保持した。

4480513でISO OUTを開いた場合だけincomplete OUT割込みを有効化した。
対象frameを過ぎた有効なISO OUTのDMAを既存の停止手順で止め、失敗・0 byteの完了をqueueへ返す。
Audioは失敗packetをringへ渡さず再armする。EP0/bulk/interrupt OUTを対象にしない。
新しいtask/timer/queueや転送recordの追加はない。ISOを有効にしてHostがpacketを送らない間は、
missを処理するIRQ/workerの費用がある。全HS/全intervalの実機確認ではなく、S3 FSのAudio間隔1で確認した。
関連7件には通常の連続PCMとSDK Hostの間欠ISO、core標準要求、816取得点の回復を含む。
手順の照合には[Linux DWC2のincomplete OUT処理](https://code.googlesource.com/linux/torvalds/linux/+/56bf6fc266ca14d2b9276c8a62e4ff6783bfe68b/drivers/usb/dwc2/gadget.c)も参照した。

### HS直結での確認と二つの修正（2026-10-31）

P4/P4XをHSで直結し、既存のS3 peer試験をHS comboでも実行した。
FS前提だった値はlinkのMPSから導くように試験側を直し、S3では従来の値のままにした。

| combo | 結果 |
| --- | --- |
| s3（FS回帰） | 51 passed、34分22秒 |
| p4_p4x_hs（旧P4 Host→P4X Device） | 59 passed、40分31秒 |
| p4x_p4_hs（P4X Host→旧P4 Device） | 58 passed、1 failed（ISO OUT）、66分38秒 |

この過程で、core/DWC2の不具合を二つ直した。

1. `09470d6`：OUT DMAの開始時にもcacheをcleanする。P4XのHS借用RXで、65535 byte中9216 byte以降が0xa5のまま残り、
   完了は成功・実長65535と報告していた。DMA中に追い出されたdirty行が受信dataを上書きしていた。
   P4/P4_PSRAM/P4X-PC/HS直結の全32件で回帰を確認。S3はcache同期が空の実装なので影響しない。
2. `aade7e0`：ISO OUTの偶奇不一致を、ISR内で反転して再armする。修正前は旧P4 Deviceの`audio_overflow`が5回中2回失敗し、
   修正後は10回連続で成功した。HS両方向のaudio/video各17件、S3のaudio 9件、旧P4 loopbackのaudio 4件も成功した。

詳細は[P4の機種差とbench](V3_P4_VARIANTS_AND_BENCH.ja.md#s3-peer試験をhs直結へ広げた結果2026-10-31)。
