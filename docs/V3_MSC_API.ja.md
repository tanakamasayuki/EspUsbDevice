# v3 MSCのruntime構成

2026-10-27。MSC classと固定4096 byte bufferを独自BOT driverへ置き換えた。
共通EP0/coreとDWC2も本体内部で保守する。CFG_TUD_MSC等の旧flagは0指定も含めcompile errorとなる。

```cpp
EspUsbDevice device;
EspUsbDeviceMsc msc(device);
uint8_t storage[32 * 512];
EspUsbDeviceMscRamDisk volume(storage, 32);

void setup() {
  EspUsbMscConfig config;
  config.transferBytes = 64;
  if (msc.configure(config) != ESP_OK) return;
  if (!volume.attach(msc)) return;
  msc.mediaPresent(true);
  device.begin();
}
```

通常のDevice/MSC/helperの利用手順は維持する。begin(count,size)は停止中のgeometry設定で、
転送資源はDevice.beginから呼ばれる引数なしbeginで確保する。
設定変更はDevice.end→configure/geometry/callback設定→Device.beginとし、再ビルドしない。

## bufferと費用

EspUsbMscConfig.transferBytesの既定は512 byte。一つのBOT commandでIN/OUTが同時に走らないため、
一つのDMA領域をCBW、SCSI応答・データ、CSWへ順番に使う。Functionごとのstateと必要なbufferだけを確保する。
64 byteからの小さい転送でも512 byte sectorをoffset付きで処理できる。sector長とbuffer容量は独立する。

現在はtransferBytesが64の倍数、最大65472 byte。選択controllerでHSを使う場合は512の倍数を要求する。
native coreのuint16_t投稿長による上限である。
transferBufferを指定すれば内部DMA buffer確保を省略する。64 byte境界のinternal DMA対応領域を
指定容量分用意し、beginからendまで保持する。外部bufferはfreeしない。
transferCapacityは開始前/終了後0、開始後は実効容量を返す。nameは任意のFunction名で、
共通のUTF-8文字列snapshot契約に従う。名前を指定しなければその文字列領域は不要。

一つのFunctionはLUN 0を公開する。複数Functionは独立したgeometry/callback/bufferとbulk pairを持ち、
controllerのendpoint予算内で併用する。未使用MSCの固有処理を共通coreから参照しない。
他の未移行classの固定資源がなくなったという意味ではない。

## callbackと状態変更

onRead/onWrite/onStartStop、vendorID/productID/productRevisionはesp_err_t返却の停止中APIになった。
稼働中はInvalidStateで拒否し、既存設定を保つ。戻り値を無視する通常の呼出しは維持できるが、
void型のmember pointerは型の変更が必要になる。

read callbackは必須、write callbackは省略できる。write callbackなしならread-onlyとして扱う。
mediaPresent/isWritableはatomicな稼働中変更を許可する。SCSI要求の処理時に状態を確認する。
稼働中のFunction破棄はDeviceを停止し、worker/DMAが止まってからstateを解放する。
未登録MSCの手動beginや破棄で、既存Functionの通信を壊さない。

callbackはUSB worker上でlibraryのspinlockを持たずに呼ぶ。end・構成変更・Function破棄を行わない。
read/writeは指定した全byte数を返して完了する同期API。負値、0、指定数と異なる戻り値はI/O失敗となる。
TinyUSBのasync完了関数やBUSY=0による保留へ依存したコードは移行が必要。
書込みcallbackは復帰までに自身が必要とする書込みを完了する。SYNCHRONIZE CACHEはその同期契約を前提とし、
任意のアプリ側cacheを暗黙にflushする機能ではない。

## BOTとSCSI

BOTの13ケースを扱い、不正CBWは両endpointをhaltしてreset recoveryを要求する。
class resetはhardwareを停止し、旧完了eventのFIFO fenceと両endpointのCLEAR_FEATUREが済むまでCBWを再投稿しない。
reset自体ではDATA PID/haltを初期化せず、HostのCLEAR_FEATUREへ従う。

INQUIRY、REQUEST SENSE、TEST UNIT READY、READ CAPACITY(10)、READ FORMAT CAPACITIES、
MODE SENSE(6/10)のheader、START STOP、PREVENT/ALLOW、SYNCHRONIZE CACHE、
比較なしVERIFY、READ/WRITE(10)を処理する。MODE SENSEのoptional pageは実装しない。
不明opcode、存在しないLUN、範囲外LBA等は失敗として返す。read-onlyはData Protect、
mediaなしはNot Ready、callback失敗はMedium ErrorのSenseを設定する。
CFG_TUD_MSC_EP_BUFSIZEとCFG_TUD_MSC指定（0を含む）、tud_msc_*直接呼出しは削除する。

## 実験と残る条件

msc_storageは64/128/512/4096 byteと外部192 byte、write callbackなし、CDC併用を実通信する。
MSC二つの分離はmsc_botの手動Hostが各interfaceを明示取得し、容量と異なるpayloadを照合する。
BOTの13ケース、不正CBWの6条件、reset前のhalt保持、繰返しreset、Sense取得とclear、
read callback失敗/書込み保護後の正確なCSW・Senseと読出し復帰も手動Hostで確認する。

EspUsbHost 2.10.1はデータ転送のSTALLで直ちにresetし、endpoint clear後のPID同期にも制約があった。
そのため通常API probeではデータ拒否を最後に確認し、通信を明示停止する。
専用手動Hostは公開IDF APIでinterfaceを再取得してHost側DATA0へ揃え、Deviceの規格動作を検証する。
Host libraryやpytestへのpatch/conftestは追加しない。

関連4試験はmsc-driver-related.xmlで4 passed、0 skipped、0 unfinished、148.36秒。
362取得点の確保失敗/回復と、未登録MSCの拒否後の既存HID通信も含む。
全caseで両board STOPPED、panicなし。serial原本は/tmp/pytest-embedded/2026-10-26_14-59-54-646464/、コピーとJUnit/transcriptはvalidation/.pytest-results/。
初回BOT matrixは512 byte sectorで確認し、下記で511 byte sectorのpacket境界も補強した。
その他のsector長、
queue満杯と人工的な旧完了eventの注入、OSの実disk/mount、SD/OTA媒体、P4/S2/S31とHSは未検証。

利点は再ビルドなしの容量変更、複数MSC、read-onlyの自然な構成と未使用固有処理のリンク除外。
不利益はbegin時の動的確保、小さいbufferでのcallback増加、同期callbackと停止中setterへの移行である。

規格参照: [USB MSC BOT 1.0](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf)、
[SCSI command field参考のUSB UFI 1.0](https://www.usb.org/sites/default/files/usbmass-ufi10.pdf)。


## USB packet境界とsector境界

最終OUTのhardware armはbus packet分の領域を確保し、storage callbackへはSCSIで要求された分だけ渡す。
Hostの過剰な最終packetの残りを次sectorへ書かず、CSW residueにも処理済みstorage byteだけを数える。
512の倍数ではないsectorにも対応するための処理であり、sector長をUSB MPSへ暗黙に切り上げない。
511 byte sectorを加えたBOT13ケースと、既存512 byte sectorを実通信で確認した。
msc-packet-boundaries.xmlは2 passed、0 skipped、0 unfinished、93.48秒。
最終の過剰byteを隣のsectorへ書かないことも、全14件の実行で直接確認した。

5最小build（Deviceのみ/Vendor/CDC/MIDI/MSC）とnm確認も成功した。
MSC未使用の4件にEspUsbDeviceMsc/EspUsbMscStateはなく、全5件にTinyUSB CDC/Vendor/HID/MIDI/MSCはない。
MSCのみのbuildには他の独自Functionや共通bulk streamを含まず、未使用の借用取消しもリンクしない。
MSC自身のBOT resetに必要なhardware abortはMSC使用時だけリンクする。


最終の標準`--combo all`はruntime-msc-all.xmlで**14 passed、0 skipped、0 unfinished、772.40秒**。
362取得点の失敗/回復、511/512 byte sectorの各BOT13ケースと隣接sectorのguard、
独立2 MSCと既存CDC/Vendor/HID/MIDIの回帰を含む。全caseで両board STOPPED、panicなし。
serial原本は/tmp/pytest-embedded/2026-10-26_17-57-02-044220/、コピーとJUnit/transcriptはvalidation/.pytest-results/runtime-msc-all*。


## interfaceの再初期化とBOT回復

SET_INTERFACE alt 0で対象MSCの両DMAを停止し、旧完了eventのfence後にDATA0へ戻す。
通常のCommand/Data/Statusの途中なら未完のBOT transactionを破棄し、新しいCBWを待つ。
保存済みのSCSI senseやmedia設定は維持する。storage callbackで処理済みの書込みはロールバックしない。
Hostは中断したコマンドの成功や書込み全体の原子性を仮定できず、必要な範囲を確認・再送する。

不正CBW後や、Phase Errorを返した後のReset Recovery要求は維持する。
SET_INTERFACEや単なるHALT解除をBOT resetの代用として扱わない。
BOT reset途中のSET_INTERFACEでも、未完のCLEAR_FEATURE手順を完了扱いにしない。
[USB-IF BOT 1.0](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf)の§3.1、§5.3.4、§6.6.1を基準に、
class resetでのHALT/PID保持、両endpointの解除、不正CBWからの回復を区別する。

再初期化にも既存recovery jobとepoch付きfenceを使い、新たなtaskや転送時heap確保は増やさない。
公開APIの形は維持する。MSCを選んだ場合に状態フラグと回復コードが追加される。
任意のタイミングのHost起因HALT/CLEARだけで進行中SCSI commandを継続する条件、
SET_INTERFACEとbus reset/endの全到着順、実media/OSの検証は残る。


## MSC interface再選択とBOT回復

2026-10-27。SET_INTERFACE alt 0をMSCのepoch付きDMA停止/fenceへ接続した。
通常のCommand待ち・CSW待ち・部分READ/WRITEを破棄して新しいCBWを受ける。
不正CBW後のBOT reset要求と、BOT reset途中の未完CLEAR_FEATUREは維持する。
WRITEは処理済み64 byteが残り、残りのsectorは変更されないことを実測した。
2つのMSCそれぞれで再初期化と容量/readbackを確認した。他instanceの同時進行コマンドの維持は未検証。

`msc-interface-verified.xml`はmsc_bot、msc_storage、allocation_failureの
**3 passed、0 skipped、0 unfinished、252.88秒**。6 serial log全てSTOPPED、panicなし。
712取得点で失敗/回復、heap before/after一致。原本は
`/tmp/pytest-embedded/2026-10-27_08-04-42-101108/`、コピー/JUnit/transcriptは
`validation/.pytest-results/msc-interface-verified*`。
既存BOT 13ケース、不正CBW 6条件、511 byte sector、media/sense、複数MSCも継続成功した。
最初の試験ではBOT reset中のSET_INTERFACEがHALTを早期解除する条件を検出し、未完の解除手順を保持した。
次の失敗はHost試験が前回の511 byte geometryを保持したためで、Host開始時に既定512 byteへ戻した。
各失敗transcriptも`.pytest-results/msc-interface-before-halt-preserve*`と`msc-interface-host-geometry*`に残す。

TinyUSB manifestは24 file / 2 compiled sourcesで一致。旧tests/examplesはmainと同じ。
全29件の一括再試験、任意Host HALTからの進行中SCSI継続、bus reset/endとの全競合順序、OS/実mediaは未完了。


## CDCとの同時通信

2026-10-29、製品`e329ef6`のまま独立MSC probeに同時通信の試験を追加した。
CDCはRX97/TX131 byteのring、64 byteのcopy転送で1/63/64/65/129 byteを連続往復し、
別のHost taskとmainからCDC・MSCを同時に使う。Deviceには専用taskを追加せず、loopでCDCをechoする。
MSCは64/4096 byte stagingそれぞれで1/2/7/8 blockを読み書きし、payload・offset・guardを比較した。

1 passed、59.01秒、2 logs STOPPED/panicなし。CDCは112/105往復、7148/6762 byteを送り、
Host受信とDevice送受信の総byte数は一致した。MSC呼出し中のCDC完了は29/14回で、
逐次で両方を成功させただけの試験ではない。MSCのmediaPresent(false)でNot Ready/Medium Not Presentを
返す間もCDCは5往復以上進み、メディア復帰後に再度MSC payloadを照合した。
回数や転送量は合否の最小進行量を確認するための値で、性能指標ではない。

共有probeの既存storage/raw BOT 2件も2 passed、81.69秒、4 logs STOPPED/panicなし。
製品・公開API変更は不要だった。標準comboとnoconftestを維持し、全体収集は39件。
serial原本は`/tmp/pytest-embedded/2026-10-28_23-44-07-209913/`（同時通信）、
`/tmp/pytest-embedded/2026-10-28_23-45-20-737310/`（既存2件）。
JUnit/transcript/serialと監査は`.pytest-results/msc-cdc-concurrent*`、`msc-cdc-regression*`。
39件の一括実行や、OS・HS実機・MSC HALT中のCDC継続まで確認した結果ではない。


## 公開APIのblock callback例

新規[MscCdcBlocks](../examples/MscCdcBlocks/MscCdcBlocks.ino)は、8 KiB RAM backing、
512 byte stagingとCDCを明示的に構成する。filesystemを含まないraw block例で、
callbackのoffset/size処理と、USB workerで更新する呼出し数をCDC側で読む流れを示す。
追加のDevice taskは作らない。S3/S2/P4でcompile/ELF/FQBN/全29 source依存の確認に成功し、
未選択Audio/NCM/Video/CCID/keymap/FAT helperのリンク除外も確認した。
記録は`.pytest-results/msc-cdc-example*`。例自体のUSB実機・OS検証済みとはしない。


## 媒体変更通知のAPI仕様案

2026-10-30、**未実装・レビュー用**。現行ESPのmediaPresentはatomic flagの変更のみ。
CH32RV側は以下のfalse→true通知を実装済み。ESPも利用者の呼出しを変えずに通知を加える案。

```cpp
// 現在の呼出し方を維持する。新しい明示通知APIは今回追加しない。
msc.mediaPresent(false);
// 媒体の準備。進行中I/Oや保存先の寿命は以下の契約に従う。
msc.mediaPresent(true);
```

- Host構成済みの間のfalse→trueで、そのMSCのLUN 0に媒体変更のpendingを立てる。
- true→true、false→falseは変更通知を作らない。true→falseは不在を表し、単独では交換通知を作らない。
- 開始前や未構成中の変更は通知を予約しない。初回列挙でpresent=trueでも交換扱いにしない。
- pending中の次の有効なLUN 0のSCSI commandをUNIT ATTENTION（sense key 06、ASC/ASCQ 28/00）で失敗させ、pendingを消費する。
  INQUIRYとREQUEST SENSEは除外し、先に来てもpendingを消費しない。不正CBWや存在しないLUNでも消費しない。
- 通知失敗後のREQUEST SENSEは06/28/00を返し、通常のsense消費規則でclearする。
  REQUEST SENSE自体をUNIT ATTENTIONで失敗させない。次の通常commandは最新のpresentを調べる。
- 通知前に複数回挿入されてもpendingは1件にまとめる。再び不在になっていても保留した変更は1回通知し、その後はNot Ready/Medium Not Present（02/3A/00）を返す。
- USB reset、構成解除、endでpendingとsenseを初期化する。BOT reset recoveryでは未通知pendingを維持し、回復だけを新しい媒体変更と扱わない。

ASC/ASCQの意味は[T10の割当表](https://www.t10.org/lists/asc-num.htm)に従う。
上記の通知時期・消費条件は本ライブラリの提案契約であり、OSのfilesystem cache再読や
任意のOSでの自動再mountを保証するものではない。

### I/Oと所有権の境界

mediaPresentは同期的なI/O停止APIではなく、voidの呼出し方を維持する。
提案では受理済みcommandを強制取消しせず、次のcommand受付でpresenceとpendingを判定する。
開始済みread/writeは保持した媒体状態で完了し、途中のpresence変更だけで成功した部分を
巻き戻す保証はしない。この境界は現行処理との照合・変更が必要で、実装済みとは扱わない。

このsetterだけで保存先pointerを破棄・交換してよいとはしない。callbackとその参照先は
Device.end成功まで有効に保つ。callback/geometryの交換は引き続き停止中だけとする。
稼働中に物理媒体を抜けるbackendは、callback内で安全にアクセス失敗を返す責任を持つ。
安全なI/O静止・flush・保存先交換の完了通知を行う新APIは今回の範囲外。
容量やsector長を変える交換はend→再設定→beginとし、同一geometryの論理的な再挿入から扱う。
アプリとHostが同じfilesystemを同時更新する権限管理も、このflagでは解決しない。

利点は既存の挿抜操作のままHostへ変更を知らせられること。不利益は次のcommandが意図的に
一度失敗することと、I/O停止・保存先解放まで行うAPIと誤解しない配慮が必要なこと。
追加費用はMSC固有のpending状態とcommand境界の判定に限定し、監視taskや媒体copyを加えない。

合格条件はfalse→true→TUR失敗→REQUEST SENSE→TUR成功、同値設定、初回present、
INQUIRY/REQUEST SENSE先行、不在、複数変更の集約、通知前の再抜去、reset/end/BOT回復、
read/write途中の変更と正確なBOT residue/CSW。OSでのmount/ejectは別試験にする。
[調整状況](V3_CH32RV_API_ALIGNMENT.ja.md)を参照。
