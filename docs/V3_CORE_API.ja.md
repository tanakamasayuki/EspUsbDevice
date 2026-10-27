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


## 初回SETUP DMAのcache引渡し

開始時に初期化したSETUP領域はCPU cacheがdirtyな可能性があるため、hardware初期化前に
clean、invalidateを順に実行する。既存のbackend hookを使い、cacheのないS3/S2ではno-opとなる。
metadataと別の64 byte lineだけを同期し、成功した後にDWC2 runtime pointerを公開する。
失敗時はcontrollerを起動せず、coreの領域とqueueを返してESP_FAILで開始を失敗させる。
NoMemとは区別し、次のbeginで再試行できる。

S3専用probeではno-op hookを置換し、clean失敗とinvalidate失敗を一つずつ注入する。
どちらも未公開・未回収0で戻ることと、通常hookへ戻した後の実通信を確認する。
これはP4でcache coherencyを実測した証拠ではなく、P4/HSの実機試験は残る。


## DWC2初回cache引渡しの補強

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


## 構成を跨ぐqueue内の完了event

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


## 再選択した構成への古い完了event混入

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


## 再構成前のDMA停止とlatched IRQ

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

## DWC2停止・IRQ同期の全体確認

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

## native class登録表の独自化

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

## class登録表独自化後の全体確認

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

## controller境界と未使用のvendor基盤の撤去

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

## controller境界移行後の全体確認

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

## native bus APIとruntime CDC/MIDI記述子

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

## native busとCDC/MIDI記述子移行後の全体確認

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

## MIDI逆向き非対称cableの追加確認

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

## native USB wire型とruntime VBUS policy

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

## native USB型とVBUS設定の全体確認

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
