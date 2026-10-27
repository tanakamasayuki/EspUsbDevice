# v3 Video: 実行時設定と借用frame

Videoのクラス処理は`EspUsbVideoTransport`が持ち、共通EP0/coreとDevice専用DWC2も本体内部で保守する。
明示的なDevice/Function/config/beginという基本方針を維持する。

```cpp
EspUsbDevice device;
EspUsbDeviceVideo camera(device, "Camera");
EspUsbVideoConfig config;
config.transport = EspUsbVideoTransport::Bulk; // default: Isochronous
config.payloadBytes = 512; // 2-byte UVC headerを含む
camera.configure(config);
camera.setFrameSize(320, 240);
camera.setMaxFrameSize(32000);
camera.onFrameResult([](const EspUsbTransferResult &result) {
  // 全ての終了理由で、ここからresult.bufferを再利用できる。
});
device.begin();
// hostのcommitと、ISOの場合はstreaming alternate選択を待つ。
uint64_t id = 0;
// camera.submitFrame(encodedFrame, encodedBytes, id);
// camera.cancelFrame(id); // 非同期。返却通知までframeを保持する。
```

## 設定と費用

`EspUsbVideoConfig`はFunctionごとに指定する。`payloadBytes`は3..65535 byte、ISOでは最大1024。
ISO packetは省略時にpayload容量から導出し、FS1023/HS1024を上限とする。
`isochronousPacketBytes`を明示する場合も3..1024。ISOの実payloadはstaging容量とpacketの小さい方。
BulkのUSB packetはFS64/HS512で、payloadは複数packetに分割できる。
controllerのFIFO/endpoint能力が足りない構成は開始に失敗する。APIの数値上限は全てのSoCでの成功保証ではない。

begin時に選択Functionの状態と一つのDMA staging bufferだけを確保し、通常frame転送中には確保しない。
frame自体は借用してpayloadごとにstagingへコピーする。複数Videoも各Functionの状態で扱い、単一global ownerを廃止した。
未使用Videoの大きなstatic bufferは持たない。core/task/driver registry等の共通費用は別に残る。

外部stagingは`payloadBuffer`と`payloadBufferBytes`で指定できる。
内部DMA RAM、64-byte alignment、容量を64-byte単位へ切り上げたspanが必要。
利用者がbeginからendまで保持し、driverによる書換え中に触らない。
`payloadBufferSize()`は確保中の論理容量を返し、begin前/end後は0。設定値は`configuration()`で読む。
configure、format/size/fps/最大frame、callback登録は開始後の変更を拒否し、end後に再設定する。

## frameの所有権と通知

`sendFrame()`の通常操作は維持する。詳細な結果が必要な場合は`submitFrame(..., id)`と`onFrameResult()`を使う。
一つのFunctionで一つのframeを借用する。frameは非NULL・非空、MJPEGは最大frame以下、YUY2はwidth×height×2と一致が必要。
成功、Cancelled、BusReset、TransferError、Stoppedの各終了で一度返却する。
`actualBytes`はUVC headerを除く確認済みframe byte数で、abort中など送信量を確定できない場合は`actualBytesKnown=false`。
成功専用の`onFrameComplete()`も維持する。両方を登録するとresult→success callbackの順になる。

`cancelFrame(id)`はworkerへ取消しを依頼する。戻り値が成功でも返却完了ではない。
古いid/0はNotFound。USB転送を止め、古い完了eventを排出してから返却する。
alternate変更と再commitでも旧frameの返却を待つ。onCommitは所有権返却の通知ではない。
callbackはUSB worker上で実行し、軽い処理と次frame投稿にとどめる。
Deviceのend、Functionの破棄、callback差し替えなどのlifecycle操作をcallbackから行わない。
同時投稿は直列化され、既存frameがある場合はInvalidState。frame返却通知前の解放・変更は禁止。

稼働中Functionの外側からの破棄はDeviceを停止し、worker/DMA停止後に状態を解放する。
未登録/二重begin/別runtimeの稼働中beginはInvalidState、startErrorで理由を返す。

## 形式と交渉

一形式・一解像度・一固定frame intervalをadvertiseする。MJPEGまたはYUY2、寸法1..4096、fps1..255。
YUY2は偶数幅かつ固定frame実サイズが必要。未知format、空frame、32-bit bit rate overflowを拒否し、失敗時は旧設定を保持する。
MJPEGの最大frame省略値は非圧縮サイズの1/8。これはencoderの出力サイズ保証ではなく、必要なら明示する。

UVC 1.5の48-byte Probe/Commitと、26/34-byteの短いSETを受け付ける。
Probeは対応する固定形式・intervalとpayload上限へ正規化し、Commitは非対応値を拒否する。
ISOはalternate 0で停止、1で送信。Bulkはalternate 0で送信する。
Bulkの途中payload終端にshort packetが必要な場合はZLPを送る。

## 移行の利点と影響

`CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE`と`CFG_TUD_VIDEO_STREAMING_BULK`は廃止し、同一binaryのFunction設定で選べる。
TinyUSB Videoを有効化する旧build flagはcompile errorにして二重driverを防ぐ。
容量/方式照会はstaticからinstanceへ変わり、callback登録もesp_err_tを返す。
既存の単純なsendFrameと成功通知の操作感は維持するが、中断でもframeを回収する利用側はonFrameResultへ移る。
代償としてbegin時の資源不足と外部DMA領域の契約を扱う必要がある。

## 検証範囲

独自driver初回の`video-driver-initial.xml`は関連2件成功（161.26秒）。
従来のVideo設定/metadata heap回復、257/511/512/1021 byteのISO転送と成功通知、
未登録Functionの拒否、64 KiB frame送信中の破棄、descriptor構築を確認した。

2026-10-27の独自Video driver最終関連試験: `video-driver-cancel.xml`、
**7 passed、0 skipped、0 unfinished、212.48秒**。
ISO/Bulk各64/65/512-byte payloadの6構成を同一binaryの実行時APIで選び、
各構成で4 frameの全byte一致、成功結果のpointer/id/byte数、64 KiB frameの取消しと古いticket拒否、
取消し直後の再投稿、送信中破棄によるStopped一度だけの返却を確認した。
65-byte構成は64-byte alignment・128-byte以上の外部DMA領域を利用する。
既存の独立descriptor probeを含む。14 serial log全てSTOPPED、panicなし。
serial原本は`/tmp/pytest-embedded/2026-10-27_01-53-40-022879/`、コピー/JUnit/transcriptは`validation/.pytest-results/video-driver-cancel*`。
VideoOnlyとDeviceOnlyの最小buildも成功し、nmでTinyUSB Video symbol不在、
DeviceOnlyには独自Video実装もリンクされないことを確認した（`*-video-nm.txt`）。
通常収集は25件。全体の直近成功はNCM時点の19件で、今回の関連7件と区別する。
Videoの追加検証結果は末尾を参照。
共通EP0/core/DCDと他SoC/HSは未完了であり、v3全体の完成ではない。

PC/OS、JPEG decode、他SoC/HS、cancel/reset/recommit競合の網羅は未確認。

## Videoの確保失敗・複数Function・制御要求

2026-10-27。`video-driver-extended.xml`は**9 passed、0 skipped、0 unfinished、175.58秒**。
Video ISO/Bulkの内部/外部staging、2 Function、65535-byte stagingを含む670取得点を
一つずつ失敗させ、全点でNoMem、全資源回収、再開始とheap回復を確認した。
`video_control`は2 Functionを同時に列挙し、MJPEGの65-byte payloadとYUY2の128-byte payloadを実通信した。
26/34/48-byte Probe/Commit、GET_LEN/INFO/MIN/MAX/RES/DEF、22件の不正要求のSTALLと復帰、
不正frame/NULL/busy/別Functionのticket拒否、片方のalt/recommit/cancel中に他方のframeを保持・完了できることを確認。
62-byte frameのBulk ZLP終端、両方の送信中endとStopped一度だけの返却、外部DMA領域前後のcanaryも含む。
ProbeのGET_MIN payloadを3、GET_RESを1に直し、受け付ける範囲と一致させた。
この関連9件には既存のISO/Bulk各3構成と独立descriptor試験も含む。
18 serial log全てSTOPPED、panicなし。原本は`/tmp/pytest-embedded/2026-10-27_02-07-12-940635/`、
コピー/JUnit/transcriptは`validation/.pytest-results/video-driver-extended*`。
通常収集26件。全体の直近成功19件とは区別する。
実USB bus resetと取消し/recommitの競合全順序、PC/OS、HS/他SoCは引き続き未確認。


## Videoとlegacy classソース除去後の全体試験

2026-10-27。TinyUSBの無効化済みclass実装10本と関連headerを削除した。
残るvendor選択は27 file / 5 compiled sources。共通EP0/core/DCDとAudio/CDC/HID/MIDIのprotocol定義は残る。
検証scriptと更新scriptのmanifestも揃え、削除したNCMのlocal patchを現manifestから外した。
出所とMIT表示は維持する。既に無効だったソースの削除なのでFlash/速度の改善を主張しない。

標準`--combo all`の`runtime-video-all.xml`は**26 passed、0 skipped、0 unfinished、1703.91秒**。
670取得点の確保失敗/回復、全独自classの既存経路、複数Video制御、ISO/Bulk各3構成を一括実行した。
52 serial log全てSTOPPED、panicなし。Video deviceのnmでもlegacy class driver symbol不在を確認した。
serial原本は`/tmp/pytest-embedded/2026-10-27_02-12-17-085198/`、コピー/JUnit/transcriptは
`validation/.pytest-results/runtime-video-all*`。symbolは`video-control-cleanup-nm.txt`。
Core 3.3.12 / Host 2.10.1 / plugin 1.10.0、S3–S3のみ。conftestや独自pytest hookは使わない。

次は共通EP0/core/DCDを独自実装へ置き換える。reset/新SETUP/STALL/status、
標準要求、event順序とDMA停止、suspend/remote wakeupを分けて検証する。
他SoC/HS、PC/OS、長時間試験、出荷exampleと最終API整理は未完了。


## bulk endpointのHALTとCLEAR

bulk endpointへのSET_FEATURE ENDPOINT_HALTでは、DMAと古い完了eventを退役させ、
借用中frameを`TransferError`で一度だけ返却する。送信中の量を確定できなければ
`actualBytesKnown=false`とし、確認済み部分だけを`actualBytes`へ入れる。
HALT中は`streaming()`がfalse、`submitFrame()`はInvalidStateでid=0、
単純な`sendFrame()`はfalseになる。拒否した投稿の所有権を受け取らず、返却callbackも発行しない。

CLEAR_FEATUREでは対象endpointだけDATA0へ戻し、既存のCommit設定のまま再開する。
HALTなしのCLEAR中にframeがあればCancelledで返却する。HALTによる退役がまだ続いている場合、
後から届いたCLEARでTransferErrorをCancelledへ上書きしない。
繰り返しSET/CLEARで返却が重複したり、別VideoのframeやPIDを初期化したりしない。
再開後は新しいframeを投稿する。中断したframeの途中からの継続は保証しない。

既存のfence/job/stateを利用し、追加のqueueやbufferは確保しない。
利用側の通常の呼出し方は変わらず、HALT時の受付拒否と借用frameの回収が明確になる。
代わりに、エラー返却後はHostの回復を待って再投稿する必要がある。
ISOのalternate制御は従来どおりで、このbulk回復経路の対象にはしない。

試験は2 Videoを別Functionとして使う。Hostの対象PIDをDATA0へそろえた準備後、
新しいSET_INTERFACEや再Commitに頼らずHALT/CLEARで復帰させる。
privateなendpoint busy照会はDMAへの登録後を狙う試験専用であり、公開利用例には含めない。
途中payloadを2 FS packetだけ読んだ条件も確認し、Hostの古いPIDを引き継ぐSDKの制約と分ける。


## Video bulk HALTの実測

2026-10-28。修正前はSET_FEATURE成功後も借用frameが返らず、`HALTED 0`となった
（`video-halt-baseline.xml`、41.34秒）。修正後の境界単独は31.29秒で成功。
関連control 2件、ISO/bulkの6 runtime構成、allocation_failure、control_coreの
**10 passed、0 skipped/failed/error/unfinished、154.16秒**を確認した。

2 Videoで16回のSET/CLEAR、途中payload読出し2回、復帰後/別Functionの正常frame 10回を確認。
各Videoのloan返却は計8件（Success 5、Cancelled 1、TransferError 2）、重複/誤id/残留loanなし。
送信中HALTの4件はactualBytesKnown=false。HALT中の投稿は拒否し、CLEAR後は再Commitなしで再開した。
774取得点の失敗/回復、heap 346564→346564 byte、20 serial全てSTOPPED、panicなし。
S2/P4はcompile-only成功。全体の収集は32件で、今回の製品変更後は関連10件を実行した。

標準pluginは同じsketchを複数のtest関数で再利用するため、試験sketchの通常のstartで
カウンタを初期化する。stopで旧loanを退役してから次を開始し、conftestによるresetは使わない。
この初期化不足で一括実行が一度失敗した記録も残した。

原本は`/tmp/pytest-embedded/2026-10-27_20-05-52-165319/`。
JUnit/transcript/serialコピーは`validation/.pytest-results/video-halt-final*`、集計は`video-halt-audit.txt`。
全EP0順序、SET/CLEARの全競合とbus reset/end、OSや他SoC/HS実通信は別の未検証範囲として残す。
