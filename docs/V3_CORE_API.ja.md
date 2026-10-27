# v3 共通coreとEP0の独自化

TinyUSBの`device/usbd.c`をビルド対象から削除し、`EspUsbDeviceCore`へ置き換える。
クラス実装と共通coreは独自側で扱い、DWC2・protocol型・一部helperは暫定依存として残す。
一つの独自coreだけを起動し、旧coreへfallbackする二重backendは持たない。

## 設定と資源

```cpp
EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
EspUsbDeviceConfig config;
config.eventQueueDepth = 16;
if (!device.begin(config)) {
  // device.lastError()で設定不正とメモリ不足を区別する。
}
```

`eventQueueDepth`は非ゼロのuint16_t。旧`CFG_TUD_TASK_QUEUE_SZ`は拒否し、この設定へ移す。
必要容量の確保に失敗すればNoMemで開始を失敗させる。型の上限は全機種での確保成功保証ではない。
同一binaryでend→設定変更→beginできる。キュー深さは待ち時間・処理速度の保証ではなく、
割込みイベントの一時保持量である。
指定深さに加えてhardware event用に1件を確保する。deferred job/fenceはこの予備枠を使わず、
投入可否の確認と投入は同じlockで行う。深さ1でもfenceがEP0 statusの記録枠を占有しない。深さを小さくすると費用は減るが、同時転送の多い構成では過負荷になりやすい。

core状態、選択configurationのinterface/endpoint map、event queue storage、EP0のDMA packetを
begin時の一つの内部DMA領域に確保する。EP0 packetは64-byte境界でmetadataから分離する。
FreeRTOS queueもその領域を利用し、通常の要求・転送・取消し・過負荷回復中にheap確保しない。
DMA停止とクラス終了後に領域を解放する。FreeRTOS自身のtask TCB等の回収時点とは区別する。

以前の固定interface map、EP0 staging、event queueの全構成向けstatic領域を廃止する。
代償としてbegin時にcore領域の確保失敗が増えるため、失敗点と回復の試験対象へ加える。
通常のDevice/Function/config/beginとread/write等の操作は維持する。

## EP0と標準要求

EP0はSetup/Data/Statusを明示して管理し、受信データの実byte数をクラスへ渡す。
INの短い返信と末尾ZLP、OUTの容量、成功していない転送結果、status方向を区別する。
新SETUPのserialとbus世代をeventへ付け、旧dataが新しい要求へ転送を投稿しないようにする。
先に完了した旧statusのACK callbackは、新SETUPが到着済みでも適用する。
SET_ADDRESSのhardware/status armは現在のDWC2 adapterへ委譲する。

device/interface/endpointの標準要求を方向・値・index・長さで検証する。
未対応configurationへの要求を拒否しても、稼働中の正しい構成は解除しない。
SET_CONFIGURATIONの解除と同じ値の再選択では、DMAを止め、クラスの所有権を返し、再初期化する。
Hostにadvertiseしていないremote wakeup featureや、EP0/ISOのHalt設定は受け付けない。

`sendControlResponse(..., 0)`は、Hostの要求長が非ゼロのINなら長さ0のdataを返す。
要求長0のno-data要求ではstatusを返す。従来の「長さ0なら常にstatus」では非ゼロINのdata段階を
飛ばしてしまうため、両者を分けた。短い返信はHost要求より少ないbyte数でも正常終了する。
OUTで要求長を保持できない受信領域は拒否する。

## eventと所有権

成功した完了eventを失ってborrowed bufferが永久に残ることを防ぐため、queue過負荷では
DMA停止、クラスresetによる所有権返却、旧eventの回収、disconnect/reconnectを行う。
従来のfence予約は非blockingで、満杯ならworker jobから再試行する。
resetが割込みで通知済みの場合は、workerがresetを処理するまで旧構成のproducer jobを進めない。

通常のクラス完了・reset/end通知はUSB worker上で扱う。
現在の内部class/DWC2接続は暫定C ABIを用い、TinyUSB由来の型名・関数名を残している。
これを最終公開APIとして追加したわけではなく、残るDWC2/helperの置換後に整理する。
`startTinyUsb`等の旧backend名を含む公開設定も最終整理の対象である。

## 検証と残り

初回の独自coreではVendor、descriptor、複数Videoの関連3件が成功した
（`core-initial.xml`、253.37秒、serial 6本STOPPED、panicなし）。

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


新SETUPと旧完了の全到着順、OUTの物理的なshort/overrun、suspend/resume・remote wakeup、
Vendor/CDC以外のクラスの転送中SET_INTERFACE・HALT/CLEAR_FEATUREでのPID/所有権回復は追加検証・実装の対象とする。
特にaltを実装しないクラスへの共通fallbackは現在alt 0のACKを扱い、
Vendor/CDCでは実際のdata interfaceとendpointを対応付けて回復する。他クラスの転送中再選択は未完了。
DWC2、残存helper・protocol定義、内部ABI、他SoC/HS、PC/OSの確認は未完了。

要求の基準は[USB-IF USB 2.0 specification](https://www.usb.org/document-library/usb-20-specification)。
実機試験は独立した`validation/`と標準pluginのcomboを用い、旧test/sampleを合否基準へ戻さない。

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


## Vendor/CDC標準要求の補強

SET_INTERFACEとHALT/CLEAR_FEATUREで転送を停止し、fence後の返却とDATA PIDの回復を行う。
詳細とcopy queue破棄の影響は[Vendorガイド](V3_VENDOR_API.ja.md#標準要求による転送の再初期化)。
指定interfaceをendpoint metadataに保持し、CDCのcontrol/data interfaceと複数Functionを区別する。
関連10件と最終4件のS3–S3試験は[試験記録](../validation/README.ja.md#vendorcdc標準要求の回復)を参照。
全体27件の記録はこの補強前の実装に対する結果であり、現在の全28件を再実行した結果ではない。


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


HIDの標準要求による方向別回復も追加済み。公開APIの影響と関連4件の実測は
[HIDガイド](V3_HID_API.ja.md#標準interfaceendpoint要求による回復)を参照。
共有bulkを使うMIDIはVendor/CDCと同じ回復経路に入るが、MIDI固有の標準要求試験はまだ追加していない。
他クラスも個別の所有権・protocol契約と検証を継続する。


MSCのSET_INTERFACEも独自recovery fenceへ接続済み。不正CBWのBOT reset要求と区別する。
[MSCガイド](V3_MSC_API.ja.md#interfaceの再初期化とbot回復)にAPIの影響と関連3件の実測を記録した。
任意Host HALT中のcommand継続、他instanceの同時進行、全競合順序を完了したという意味ではない。


## DWC2の転送状態をcoreの開始時領域へ移す

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
