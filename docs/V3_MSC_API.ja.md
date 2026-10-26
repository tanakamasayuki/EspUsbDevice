# v3 MSCのruntime構成

2026-10-27。MSC classと固定4096 byte bufferを独自BOT driverへ置き換えた。
共通EP0/DCDはまだ暫定TinyUSBである。TinyUSB MSC classのCFG_TUD_MSCは0へ固定する。

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
暫定coreのuint16_t投稿長による上限であり、最終coreの上限ではない。
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
CFG_TUD_MSC_EP_BUFSIZEと非zeroのCFG_TUD_MSC指定、tud_msc_*直接呼出しは削除する。

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
